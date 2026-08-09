/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// VYBES FORK of cores/teensy4/usb_audio.cpp (upstream sha in
// core_fork/usb_audio.cpp.upstream.sha1, installed by core_fork_sync.py).
// Changes, all marked with "VYBES":
//  1. usb_audio_rx_hook(): a weak extern "C" hook called with every incoming
//     isochronous packet before any buffering. A project can define a strong
//     version to consume raw packets (AsyncAudioInputUSB does this).
//  2. Deep receive queue: the stock code buffered at most 2 audio blocks
//     (~5.8ms) ahead of AudioInputUSB::update() and targeted a 64-sample
//     occupancy, so millisecond-scale bursts in host packet delivery
//     overflowed it (drops) and scheduling gaps drained it (silence).
//     Replaced with an 8-block ring targeting 3 blocks (~8.7ms latency,
//     ~14.5ms of burst absorption).
//  3. The feedback loop gets a derivative damping term (the stock
//     integrator-only loop is an undamped oscillator) and loses the +3500
//     underrun kick, which the linear loop now covers.

#include <Arduino.h>
#include "usb_dev.h"
#include "usb_audio.h"
#include "debug/printf.h"

#ifdef AUDIO_INTERFACE

bool AudioInputUSB::update_responsibility;
audio_block_t * AudioInputUSB::incoming_left;
audio_block_t * AudioInputUSB::incoming_right;
audio_block_t * AudioInputUSB::ready_left;
audio_block_t * AudioInputUSB::ready_right;
uint16_t AudioInputUSB::incoming_count;
uint8_t AudioInputUSB::receive_flag;

struct usb_audio_features_struct AudioInputUSB::features = {0,0,FEATURE_MAX_VOLUME/2};

extern volatile uint8_t usb_high_speed;
static void rx_event(transfer_t *t);
static void tx_event(transfer_t *t);

/*static*/ transfer_t rx_transfer __attribute__ ((used, aligned(32)));
/*static*/ transfer_t sync_transfer __attribute__ ((used, aligned(32)));
/*static*/ transfer_t tx_transfer __attribute__ ((used, aligned(32)));
DMAMEM static uint8_t rx_buffer[AUDIO_RX_SIZE] __attribute__ ((aligned(32)));
DMAMEM static uint8_t tx_buffer[AUDIO_RX_SIZE] __attribute__ ((aligned(32)));
DMAMEM uint32_t usb_audio_sync_feedback __attribute__ ((aligned(32)));

uint8_t usb_audio_receive_setting=0;
uint8_t usb_audio_transmit_setting=0;
uint8_t usb_audio_sync_nbytes;
uint8_t usb_audio_sync_rshift;

uint32_t feedback_accumulator;

volatile uint32_t usb_audio_underrun_count;
volatile uint32_t usb_audio_overrun_count;

// VYBES: deep receive queue of ready blocks between the USB interrupt and
// AudioInputUSB::update(). Occupancy targets TARGET blocks after the pop, so
// steady-state input latency is ~(TARGET+0.5) blocks and bursts of up to
// (QUEUE-TARGET) blocks are absorbed without dropping samples.
#define USB_AUDIO_RX_QUEUE_BLOCKS  8
#define USB_AUDIO_RX_TARGET_BLOCKS 3
static audio_block_t *rx_queue_left[USB_AUDIO_RX_QUEUE_BLOCKS];
static audio_block_t *rx_queue_right[USB_AUDIO_RX_QUEUE_BLOCKS];
static uint8_t rx_queue_head; // next slot the USB interrupt fills
static uint8_t rx_queue_tail; // next slot update() drains
volatile uint8_t usb_audio_rx_queue_count; // non-static so sketches can monitor it
static volatile uint8_t rx_queue_flush_pending; // set by usb_audio_configure()
static int32_t rx_feedback_prev_err;

// VYBES: weak per-packet hook. A project defines a strong version and returns
// nonzero to consume the packet, bypassing the queue and feedback loop
// entirely (the consumer then owns rate matching; feedback_accumulator stays
// at whatever it was, nominal 44.1kHz after usb_audio_configure()).
extern "C" {
int __attribute__((weak)) usb_audio_rx_hook(const int16_t *lr, unsigned int frames)
{
	(void)lr;
	(void)frames;
	return 0;
}
}


static void rx_event(transfer_t *t)
{
	if (t) {
		int len = AUDIO_RX_SIZE - ((rx_transfer.status >> 16) & 0x7FFF);
		printf("rx %u\n", len);
		usb_audio_receive_callback(len);
	}
	usb_prepare_transfer(&rx_transfer, rx_buffer, AUDIO_RX_SIZE, 0);
	arm_dcache_delete(&rx_buffer, AUDIO_RX_SIZE);
	usb_receive(AUDIO_RX_ENDPOINT, &rx_transfer);
}

static void sync_event(transfer_t *t)
{
	// USB 2.0 Specification, 5.12.4.2 Feedback, pages 73-75
	//printf("sync %x\n", sync_transfer.status); // too slow, can't print this much
	usb_audio_sync_feedback = feedback_accumulator >> usb_audio_sync_rshift;
	usb_prepare_transfer(&sync_transfer, &usb_audio_sync_feedback, usb_audio_sync_nbytes, 0);
	arm_dcache_flush(&usb_audio_sync_feedback, usb_audio_sync_nbytes);
	usb_transmit(AUDIO_SYNC_ENDPOINT, &sync_transfer);
}

void usb_audio_configure(void)
{
	printf("usb_audio_configure\n");
	usb_audio_underrun_count = 0;
	usb_audio_overrun_count = 0;
	feedback_accumulator = 739875226; // 44.1 * 2^24
	// VYBES: blocks queued by a previous session (re-enumeration) are stale.
	// AudioStream::release is protected, so flag the flush for
	// AudioInputUSB::update() to perform instead of draining here.
	rx_feedback_prev_err = 0;
	rx_queue_flush_pending = 1;
	if (usb_high_speed) {
		usb_audio_sync_nbytes = 4;
		usb_audio_sync_rshift = 8;
	} else {
		usb_audio_sync_nbytes = 3;
		usb_audio_sync_rshift = 10;
	}
	memset(&rx_transfer, 0, sizeof(rx_transfer));
	usb_config_rx_iso(AUDIO_RX_ENDPOINT, AUDIO_RX_SIZE, 1, rx_event);
	rx_event(NULL);
	memset(&sync_transfer, 0, sizeof(sync_transfer));
	usb_config_tx_iso(AUDIO_SYNC_ENDPOINT, usb_audio_sync_nbytes, 1, sync_event);
	sync_event(NULL);
	memset(&tx_transfer, 0, sizeof(tx_transfer));
	usb_config_tx_iso(AUDIO_TX_ENDPOINT, AUDIO_TX_SIZE, 1, tx_event);
	tx_event(NULL);
}

void AudioInputUSB::begin(void)
{
	incoming_count = 0;
	incoming_left = NULL;
	incoming_right = NULL;
	ready_left = NULL;
	ready_right = NULL;
	receive_flag = 0;
	// update_responsibility = update_setup();
	// TODO: update responsibility is tough, partly because the USB
	// interrupts aren't sychronous to the audio library block size,
	// but also because the PC may stop transmitting data, which
	// means we no longer get receive callbacks from usb.c
	update_responsibility = false;
}

static void copy_to_buffers(const uint32_t *src, int16_t *left, int16_t *right, unsigned int len)
{
	uint32_t *target = (uint32_t*) src + len; 
	while ((src < target) && (((uintptr_t) left & 0x02) != 0)) {
		uint32_t n = *src++;
		*left++ = n & 0xFFFF;
		*right++ = n >> 16;
	}

	while ((src < target - 2)) {
		uint32_t n1 = *src++;
		uint32_t n = *src++;
		*(uint32_t *)left = (n1 & 0xFFFF) | ((n & 0xFFFF) << 16);
		left+=2;
		*(uint32_t *)right = (n1 >> 16) | ((n & 0xFFFF0000)) ;
		right+=2;
	}

	while ((src < target)) {
		uint32_t n = *src++;
		*left++ = n & 0xFFFF;
		*right++ = n >> 16;
	}
}

// Called from the USB interrupt when an isochronous packet arrives
// we must completely remove it from the receive buffer before returning
//
#if 1
void usb_audio_receive_callback(unsigned int len)
{
	unsigned int count, avail;
	audio_block_t *left, *right;
	const uint32_t *data;

	len >>= 2; // 1 sample = 4 bytes: 2 left, 2 right

	// VYBES: offer the raw packet to the project hook first
	if (usb_audio_rx_hook((const int16_t *)rx_buffer, len)) return;

	AudioInputUSB::receive_flag = 1;
	data = (const uint32_t *)rx_buffer;

	count = AudioInputUSB::incoming_count;
	left = AudioInputUSB::incoming_left;
	right = AudioInputUSB::incoming_right;
	if (left == NULL) {
		left = AudioStream::allocate();
		if (left == NULL) return;
		AudioInputUSB::incoming_left = left;
	}
	if (right == NULL) {
		right = AudioStream::allocate();
		if (right == NULL) return;
		AudioInputUSB::incoming_right = right;
	}
	while (len > 0) {
		avail = AUDIO_BLOCK_SAMPLES - count;
		if (len < avail) {
			copy_to_buffers(data, left->data + count, right->data + count, len);
			AudioInputUSB::incoming_count = count + len;
			return;
		}
		// VYBES: incoming block is full - queue it and start a fresh one
		if (avail > 0) {
			copy_to_buffers(data, left->data + count, right->data + count, avail);
			data += avail;
			len -= avail;
		}
		if (usb_audio_rx_queue_count >= USB_AUDIO_RX_QUEUE_BLOCKS) {
			// queue overrun, PC sending too fast: drop this block by
			// rewinding the fill position and overwriting it in place
			usb_audio_overrun_count++;
			printf("!");
			AudioInputUSB::incoming_count = 0;
			return;
		}
		rx_queue_left[rx_queue_head] = left;
		rx_queue_right[rx_queue_head] = right;
		rx_queue_head = (rx_queue_head + 1) % USB_AUDIO_RX_QUEUE_BLOCKS;
		usb_audio_rx_queue_count++;
		left = AudioStream::allocate();
		if (left == NULL) {
			AudioInputUSB::incoming_left = NULL;
			AudioInputUSB::incoming_right = NULL;
			AudioInputUSB::incoming_count = 0;
			return;
		}
		right = AudioStream::allocate();
		if (right == NULL) {
			AudioStream::release(left);
			AudioInputUSB::incoming_left = NULL;
			AudioInputUSB::incoming_right = NULL;
			AudioInputUSB::incoming_count = 0;
			return;
		}
		AudioInputUSB::incoming_left = left;
		AudioInputUSB::incoming_right = right;
		count = 0;
	}
	AudioInputUSB::incoming_count = count;
}
#endif

void AudioInputUSB::update(void)
{
	audio_block_t *left = NULL, *right = NULL;

	// VYBES: pop one block pair from the deep queue and measure occupancy
	__disable_irq();
	if (rx_queue_flush_pending) {
		// USB re-enumerated: drop everything queued by the old session
		rx_queue_flush_pending = 0;
		while (usb_audio_rx_queue_count > 0) {
			audio_block_t *l = rx_queue_left[rx_queue_tail];
			audio_block_t *r = rx_queue_right[rx_queue_tail];
			rx_queue_tail = (rx_queue_tail + 1) % USB_AUDIO_RX_QUEUE_BLOCKS;
			usb_audio_rx_queue_count--;
			if (l) release(l);
			if (r) release(r);
		}
	}
	if (usb_audio_rx_queue_count > 0) {
		left = rx_queue_left[rx_queue_tail];
		right = rx_queue_right[rx_queue_tail];
		rx_queue_tail = (rx_queue_tail + 1) % USB_AUDIO_RX_QUEUE_BLOCKS;
		usb_audio_rx_queue_count--;
	}
	int32_t occupancy = (int32_t)usb_audio_rx_queue_count * AUDIO_BLOCK_SAMPLES
	                    + incoming_count;
	uint8_t f = receive_flag;
	receive_flag = 0;
	__enable_irq();
	if (f) {
		// VYBES: PD control of the host rate. The plant is a double
		// integrator (occupancy integrates the rate error, the accumulator
		// integrates our correction), so the stock proportional-only loop
		// was an undamped oscillator. err + 16*d(err) is near critically
		// damped at the 344.5Hz update rate (settles in a few seconds).
		int32_t err = USB_AUDIO_RX_TARGET_BLOCKS * AUDIO_BLOCK_SAMPLES - occupancy;
		feedback_accumulator += err + 16 * (err - rx_feedback_prev_err);
		rx_feedback_prev_err = err;
	}
	if (!left || !right) {
		usb_audio_underrun_count++;
		//printf("#"); // buffer underrun - PC sending too slow
	}
	if (left) {
		transmit(left, 0);
		release(left);
	}
	if (right) {
		transmit(right, 1);
		release(right);
	}
}

















#if 1
bool AudioOutputUSB::update_responsibility;
audio_block_t * AudioOutputUSB::left_1st;
audio_block_t * AudioOutputUSB::left_2nd;
audio_block_t * AudioOutputUSB::right_1st;
audio_block_t * AudioOutputUSB::right_2nd;
uint16_t AudioOutputUSB::offset_1st;

/*DMAMEM*/ uint16_t usb_audio_transmit_buffer[AUDIO_TX_SIZE/2] __attribute__ ((used, aligned(32)));


static void tx_event(transfer_t *t)
{
	int len = usb_audio_transmit_callback();
	usb_audio_sync_feedback = feedback_accumulator >> usb_audio_sync_rshift;
	usb_prepare_transfer(&tx_transfer, usb_audio_transmit_buffer, len, 0);
	arm_dcache_flush_delete(usb_audio_transmit_buffer, len);
	usb_transmit(AUDIO_TX_ENDPOINT, &tx_transfer);
}


void AudioOutputUSB::begin(void)
{
	update_responsibility = false;
	left_1st = NULL;
	right_1st = NULL;
}

static void copy_from_buffers(uint32_t *dst, int16_t *left, int16_t *right, unsigned int len)
{
	// TODO: optimize...
	while (len > 0) {
		*dst++ = (*right++ << 16) | (*left++ & 0xFFFF);
		len--;
	}
}

void AudioOutputUSB::update(void)
{
	audio_block_t *left, *right;

	// TODO: we shouldn't be writing to these......
	//left = receiveReadOnly(0); // input 0 = left channel
	//right = receiveReadOnly(1); // input 1 = right channel
	left = receiveWritable(0); // input 0 = left channel
	right = receiveWritable(1); // input 1 = right channel
	if (usb_audio_transmit_setting == 0) {
		if (left) release(left);
		if (right) release(right);
		if (left_1st) { release(left_1st); left_1st = NULL; }
		if (left_2nd) { release(left_2nd); left_2nd = NULL; }
		if (right_1st) { release(right_1st); right_1st = NULL; }
		if (right_2nd) { release(right_2nd); right_2nd = NULL; }
		offset_1st = 0;
		return;
	}
	if (left == NULL) {
		left = allocate();
		if (left == NULL) {
			if (right) release(right);
			return;
		}
		memset(left->data, 0, sizeof(left->data));
	}
	if (right == NULL) {
		right = allocate();
		if (right == NULL) {
			release(left);
			return;
		}
		memset(right->data, 0, sizeof(right->data));
	}
	__disable_irq();
	if (left_1st == NULL) {
		left_1st = left;
		right_1st = right;
		offset_1st = 0;
	} else if (left_2nd == NULL) {
		left_2nd = left;
		right_2nd = right;
	} else {
		// buffer overrun - PC is consuming too slowly
		audio_block_t *discard1 = left_1st;
		left_1st = left_2nd;
		left_2nd = left;
		audio_block_t *discard2 = right_1st;
		right_1st = right_2nd;
		right_2nd = right;
		offset_1st = 0; // TODO: discard part of this data?
		//serial_print("*");
		release(discard1);
		release(discard2);
	}
	__enable_irq();
}


// Called from the USB interrupt when ready to transmit another
// isochronous packet.  If we place data into the transmit buffer,
// the return is the number of bytes.  Otherwise, return 0 means
// no data to transmit
unsigned int usb_audio_transmit_callback(void)
{
	static uint32_t count=5;
	uint32_t avail, num, target, offset, len=0;
	audio_block_t *left, *right;

	if (++count < 10) {   // TODO: dynamic adjust to match USB rate
		target = 44;
	} else {
		count = 0;
		target = 45;
	}
	while (len < target) {
		num = target - len;
		left = AudioOutputUSB::left_1st;
		if (left == NULL) {
			// buffer underrun - PC is consuming too quickly
			memset(usb_audio_transmit_buffer + len, 0, num * 4);
			//serial_print("%");
			break;
		}
		right = AudioOutputUSB::right_1st;
		offset = AudioOutputUSB::offset_1st;

		avail = AUDIO_BLOCK_SAMPLES - offset;
		if (num > avail) num = avail;

		copy_from_buffers((uint32_t *)usb_audio_transmit_buffer + len,
			left->data + offset, right->data + offset, num);
		len += num;
		offset += num;
		if (offset >= AUDIO_BLOCK_SAMPLES) {
			AudioStream::release(left);
			AudioStream::release(right);
			AudioOutputUSB::left_1st = AudioOutputUSB::left_2nd;
			AudioOutputUSB::left_2nd = NULL;
			AudioOutputUSB::right_1st = AudioOutputUSB::right_2nd;
			AudioOutputUSB::right_2nd = NULL;
			AudioOutputUSB::offset_1st = 0;
		} else {
			AudioOutputUSB::offset_1st = offset;
		}
	}
	return target * 4;
}
#endif




struct setup_struct {
  union {
    struct {
	uint8_t bmRequestType;
	uint8_t bRequest;
	union {
		struct {
			uint8_t bChannel;  // 0=main, 1=left, 2=right
			uint8_t bCS;       // Control Selector
		};
		uint16_t wValue;
	};
	union {
		struct {
			uint8_t bIfEp;     // type of entity
			uint8_t bEntityId; // UnitID, TerminalID, etc.
		};
		uint16_t wIndex;
	};
	uint16_t wLength;
    };
  };
};

int usb_audio_get_feature(void *stp, uint8_t *data, uint32_t *datalen)
{
	struct setup_struct setup = *((struct setup_struct *)stp);
	if (setup.bmRequestType==0xA1) { // should check bRequest, bChannel, and UnitID
			if (setup.bCS==0x01) { // mute
				data[0] = AudioInputUSB::features.mute;  // 1=mute, 0=unmute
				*datalen = 1;
				return 1;
			}
			else if (setup.bCS==0x02) { // volume
				if (setup.bRequest==0x81) { // GET_CURR
					data[0] = AudioInputUSB::features.volume & 0xFF;
					data[1] = (AudioInputUSB::features.volume>>8) & 0xFF;
				}
				else if (setup.bRequest==0x82) { // GET_MIN
					//serial_print("vol get_min\n");
					data[0] = 0;     // min level is 0
					data[1] = 0;
				}
				else if (setup.bRequest==0x83) { // GET_MAX
					data[0] = FEATURE_MAX_VOLUME;  // max level, for range of 0 to MAX
					data[1] = 0;
				}
				else if (setup.bRequest==0x84) { // GET_RES
					data[0] = 1; // increment vol by by 1
					data[1] = 0;
				}
				else { // pass over SET_MEM, etc.
					return 0;
				}
				*datalen = 2;
				return 1;
			}
	}
	return 0;
}

int usb_audio_set_feature(void *stp, uint8_t *buf) 
{
	struct setup_struct setup = *((struct setup_struct *)stp);
	if (setup.bmRequestType==0x21) { // should check bRequest, bChannel and UnitID
			if (setup.bCS==0x01) { // mute
				if (setup.bRequest==0x01) { // SET_CUR
					AudioInputUSB::features.mute = buf[0]; // 1=mute,0=unmute
					AudioInputUSB::features.change = 1;
					return 1;
				}
			}
			else if (setup.bCS==0x02) { // volume
				if (setup.bRequest==0x01) { // SET_CUR
					AudioInputUSB::features.volume = buf[0];
					AudioInputUSB::features.change = 1;
					return 1;
				}
			}
	}
	return 0;
}


#endif // AUDIO_INTERFACE
