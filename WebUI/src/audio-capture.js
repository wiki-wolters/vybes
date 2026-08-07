/*
 * Time-domain microphone capture for the delay alignment wizard. The RTA
 * (AnalyzerView) only needs the AnalyserNode's frequency data; alignment
 * needs the raw sample stream, so an AudioWorklet forwards Float32 chunks
 * to the main thread and stop() concatenates them into one recording.
 *
 * The getUserMedia constraints mirror the RTA's: any echo cancellation,
 * noise suppression or AGC would mangle the chirps we correlate against.
 */

export const micSupported =
  typeof navigator !== 'undefined' && !!navigator.mediaDevices?.getUserMedia;

// The worklet processor is tiny, so it ships inline as a Blob module - no
// separate asset to serve from the ESP's filesystem.
const PROCESSOR_SOURCE = `
class CaptureProcessor extends AudioWorkletProcessor {
  process(inputs) {
    const input = inputs[0];
    if (input && input[0] && input[0].length > 0) {
      // Copy: the engine reuses the input buffer between calls
      this.port.postMessage(new Float32Array(input[0]));
    }
    return true;
  }
}
registerProcessor('vybes-capture', CaptureProcessor);
`;

export class MicRecorder {
  constructor() {
    this.stream = null;
    this.context = null;
    this.node = null;
    this.chunks = [];
    this.recording = false;
  }

  /** Ask for the mic and start accumulating samples. Throws on denial. */
  async start() {
    this.stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: false,
        noiseSuppression: false,
        autoGainControl: false,
      },
      video: false,
    });
    this.context = new (window.AudioContext || window.webkitAudioContext)();
    await this.context.resume();

    const blob = new Blob([PROCESSOR_SOURCE], { type: 'application/javascript' });
    const url = URL.createObjectURL(blob);
    try {
      await this.context.audioWorklet.addModule(url);
    } finally {
      URL.revokeObjectURL(url);
    }

    this.node = new AudioWorkletNode(this.context, 'vybes-capture', {
      numberOfInputs: 1,
      numberOfOutputs: 0,
    });
    this.chunks = [];
    this.recording = true;
    this.node.port.onmessage = (e) => {
      if (this.recording) this.chunks.push(e.data);
    };
    this.context.createMediaStreamSource(this.stream).connect(this.node);
  }

  get sampleRate() {
    return this.context ? this.context.sampleRate : 0;
  }

  /** Seconds captured so far */
  get durationS() {
    let n = 0;
    for (const c of this.chunks) n += c.length;
    return this.context ? n / this.context.sampleRate : 0;
  }

  /** Stop capturing and return { samples, sampleRate }. */
  stop() {
    this.recording = false;
    let total = 0;
    for (const c of this.chunks) total += c.length;
    const samples = new Float32Array(total);
    let offset = 0;
    for (const c of this.chunks) {
      samples.set(c, offset);
      offset += c.length;
    }
    const sampleRate = this.sampleRate;
    this.dispose();
    return { samples, sampleRate };
  }

  /** Release the mic and audio context (safe to call twice). */
  dispose() {
    this.recording = false;
    if (this.node) {
      this.node.port.onmessage = null;
      this.node.disconnect();
      this.node = null;
    }
    if (this.stream) {
      this.stream.getTracks().forEach((t) => t.stop());
      this.stream = null;
    }
    if (this.context) {
      this.context.close();
      this.context = null;
    }
    this.chunks = [];
  }
}
