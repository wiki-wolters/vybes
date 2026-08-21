#ifndef SD_WAV_PLAYER_H
#define SD_WAV_PLAYER_H

// Stereo WAV playback from SD with all card access in loop() context.
//
// The stock AudioPlaySdWav reads the card inside update() - audio interrupt
// context - which would make it the only interrupt-context SD user in a
// firmware where everything else (FIR loads, the recorder, file listings)
// reads the card from loop(). SdFat's volume state is not reentrant, so that
// mix invites filesystem corruption. This player instead refills a small
// FIFO of audio-pool blocks from service() (call it from loop()); update()
// only pops blocks, so it never touches the card.
//
// The FIFO borrows from the existing audio block pool - no new buffers, in
// keeping with the RAM2 budget (see the FIR pool notes in fir_filters.ino).
// A loop() stall longer than the FIFO (~70ms) - e.g. a FIR load during a
// preset switch - underruns audibly but recovers by itself; position keeps
// counting only actually-played frames.
//
// Accepts 16-bit stereo PCM at the audio sample rate (what SdRecorder
// writes). Anything else is rejected with a "format" error.

#include <Arduino.h>
#include <Audio.h>
#include <SD.h>

class SdWavPlayer : public AudioStream {
public:
    SdWavPlayer() : AudioStream(0, nullptr) {}

    // Open and start a file. On failure returns false and sets errCode to a
    // static string: notfound, format. Any current playback is stopped.
    bool play(const char* path, const char** errCode);

    // Stop and release everything. Safe to call at any time.
    void stop();

    // Refill the FIFO from the file; call from loop(). Also closes the file
    // once playback has drained to the end.
    void service();

    bool isActive() const { return playing || pendingClose; }
    uint32_t positionSeconds() const { return framesPlayed / (uint32_t)AUDIO_SAMPLE_RATE; }
    uint32_t lengthSeconds() const { return framesTotal / (uint32_t)AUDIO_SAMPLE_RATE; }
    const char* fileName() const { return name; }

    // True exactly once after a file has played to its end (not on stop()).
    bool consumeFinishedEvent();

    virtual void update() override;

private:
    static const int RING_PAIRS = 24; // ~70ms of buffered audio

    int ringCountFree() const;
    void drainRing();
    bool queueOneBlockPair();
    bool parseHeader(const char** errCode);

    File file;
    char name[64] = "";

    // Single-producer (service, loop) / single-consumer (update, ISR) FIFO.
    audio_block_t* ringL[RING_PAIRS];
    audio_block_t* ringR[RING_PAIRS];
    volatile uint8_t ringHead = 0; // written by update()
    volatile uint8_t ringTail = 0; // written by service()

    volatile bool playing = false;
    volatile bool eofQueued = false;    // last block is in the ring
    volatile bool finished = false;     // ring drained after eofQueued
    bool pendingClose = false;          // service() still has to close the file
    bool finishedEventPending = false;

    uint32_t dataRemaining = 0; // bytes of the data chunk still unread
    uint32_t framesTotal = 0;
    volatile uint32_t framesPlayed = 0;
    uint32_t underruns = 0;
};

#endif // SD_WAV_PLAYER_H
