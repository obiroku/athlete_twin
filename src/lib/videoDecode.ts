import {
  createFile,
  Endianness,
  MP4BoxBuffer,
  MultiBufferStream,
  type Movie,
  type Sample,
  type VisualSampleEntry,
} from "mp4box";
import type { DecodedFrame, FrameRateReport } from "./types";

// FRAME RATE / TIMESTAMP HANDLING — read this before touching this file.
//
// iPhone slow-motion clips are the case this whole module exists for: the
// container's declared frame rate and the "movie" timeline (which applies
// edit-list stretching for slow-mo playback) are both lies for our purposes.
// A 240fps capture played back "slowed" can report a nominal rate that's off
// by 4-8x depending on how a given tool interprets the edit list.
//
// The only thing we trust is the raw per-sample composition time (cts) in
// the *media* timescale, taken directly from mp4box's sample records,
// deliberately bypassing any edit-list-based retiming. We convert each
// sample's cts to microseconds ourselves and hand that to
// EncodedVideoChunk. Effective fps is then derived from the actual deltas
// between consecutive decoded frame timestamps — never from mvhd/tkhd rate
// fields, never hardcoded.

interface TrackConfig {
  codec: string;
  codedWidth: number;
  codedHeight: number;
  description: Uint8Array | undefined;
}

function getCodecDescription(entry: VisualSampleEntry | undefined): Uint8Array | undefined {
  // WebCodecs needs the raw avcC/hvcC box payload (without the 8-byte box
  // header) as the decoder config `description`.
  const box = entry?.avcC ?? entry?.hvcC;
  if (!box) return undefined;

  const stream = new MultiBufferStream();
  stream.endianness = Endianness.BIG_ENDIAN;
  box.write(stream);
  // box.write() includes the 8-byte box header (size + fourcc); the
  // decoder description should be just the payload after that. Bound the
  // slice by the stream's virtual length, not the backing buffer's
  // capacity, since MultiBufferStream over-allocates and grows in chunks.
  return new Uint8Array(stream.buffer, 8, stream.byteLength - 8);
}

async function demux(file: File): Promise<{ chunks: EncodedVideoChunk[]; config: TrackConfig }> {
  const arrayBuffer = await file.arrayBuffer();
  const mp4boxBuffer = MP4BoxBuffer.fromArrayBuffer(arrayBuffer, 0);

  const mp4boxFile = createFile();
  const chunks: EncodedVideoChunk[] = [];
  let config: TrackConfig | null = null;

  await new Promise<void>((resolve, reject) => {
    mp4boxFile.onError = (module: string, message: string) =>
      reject(new Error(`mp4box error (${module}): ${message}`));

    mp4boxFile.onReady = (info: Movie) => {
      const track = info.videoTracks[0];
      if (!track) {
        reject(new Error("No video track found in file"));
        return;
      }
      if (!track.video) {
        reject(new Error("Video track missing width/height info"));
        return;
      }
      config = {
        codec: track.codec,
        codedWidth: track.video.width,
        codedHeight: track.video.height,
        // filled in from the first sample once extraction starts
        description: undefined,
      };
      mp4boxFile.setExtractionOptions(track.id, undefined, { nbSamples: Infinity });
      mp4boxFile.start();
    };

    mp4boxFile.onSamples = (_trackId: number, _user: unknown, samples: Sample[]) => {
      for (const sample of samples) {
        if (config && !config.description) {
          config.description = getCodecDescription(
            sample.description as unknown as VisualSampleEntry,
          );
        }
        // sample.cts / sample.timescale is the raw media-timescale
        // composition timestamp — this is what survives slow-mo edit-list
        // shenanigans untouched. Convert straight to microseconds.
        const timestampUs = Math.round((sample.cts / sample.timescale) * 1_000_000);
        const durationUs = Math.round((sample.duration / sample.timescale) * 1_000_000);
        chunks.push(
          new EncodedVideoChunk({
            type: sample.is_sync ? "key" : "delta",
            timestamp: timestampUs,
            duration: durationUs,
            data: sample.data as BufferSource,
          }),
        );
      }
      resolve();
    };

    mp4boxFile.appendBuffer(mp4boxBuffer, true);
    mp4boxFile.flush();
  });

  if (!config) throw new Error("Failed to read track configuration");

  chunks.sort((a, b) => a.timestamp - b.timestamp);
  return { chunks, config };
}

// Effective fps from actual decoded timestamp deltas. Uses the median delta
// rather than the mean so a handful of dropped/duplicated frames don't skew
// the estimate — but `jittery` flags when the spread is wide enough that a
// single fps number is misleading and the raw deltas should be inspected.
export function computeFrameRateReport(timestampsUs: number[]): FrameRateReport {
  if (timestampsUs.length < 2) {
    return { effectiveFps: 0, medianDeltaUs: 0, sampleCount: 0, jittery: true };
  }
  const deltas: number[] = [];
  for (let i = 1; i < timestampsUs.length; i++) {
    deltas.push(timestampsUs[i] - timestampsUs[i - 1]);
  }
  const sorted = [...deltas].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  const medianDeltaUs =
    sorted.length % 2 === 0 ? (sorted[mid - 1] + sorted[mid]) / 2 : sorted[mid];

  const meanDelta = deltas.reduce((a, b) => a + b, 0) / deltas.length;
  const variance = deltas.reduce((sum, d) => sum + (d - meanDelta) ** 2, 0) / deltas.length;
  const stdDevUs = Math.sqrt(variance);
  // Somewhat arbitrary threshold: if frame-to-frame spacing varies by more
  // than 20% of the median, treat the fps estimate as untrustworthy.
  const jittery = medianDeltaUs > 0 && stdDevUs / medianDeltaUs > 0.2;

  const effectiveFps = medianDeltaUs > 0 ? 1_000_000 / medianDeltaUs : 0;

  return { effectiveFps, medianDeltaUs, sampleCount: deltas.length, jittery };
}

export interface ExtractProgress {
  decoded: number;
  total: number;
}

// Decodes every frame in a local video file via WebCodecs and returns them
// with true presentation timestamps. Deliberately does not use
// requestVideoFrameCallback, <video> seeking, or any nominal fps — see the
// module comment above.
export async function extractFrames(
  file: File,
  onProgress?: (p: ExtractProgress) => void,
): Promise<{ frames: DecodedFrame[]; frameRate: FrameRateReport }> {
  const { chunks, config } = await demux(file);

  const frames: DecodedFrame[] = [];
  const pendingBitmaps: Promise<void>[] = [];
  let decodeError: Error | null = null;

  const decoder = new VideoDecoder({
    output: (videoFrame) => {
      // Snapshot to an ImageBitmap immediately and close the VideoFrame —
      // VideoFrame holds a limited pool of GPU/decoder buffers and must be
      // released promptly or decoding stalls.
      const timestampUs = videoFrame.timestamp;
      const pending = createImageBitmap(videoFrame)
        .then((bitmap) => {
          frames.push({ index: frames.length, timestampUs, bitmap });
          onProgress?.({ decoded: frames.length, total: chunks.length });
        })
        .finally(() => videoFrame.close());
      pendingBitmaps.push(pending);
    },
    error: (e) => {
      decodeError = e as Error;
    },
  });

  decoder.configure({
    codec: config.codec,
    codedWidth: config.codedWidth,
    codedHeight: config.codedHeight,
    description: config.description,
  });

  for (const chunk of chunks) {
    decoder.decode(chunk);
  }
  await decoder.flush();
  decoder.close();
  await Promise.all(pendingBitmaps);

  if (decodeError) throw decodeError as Error;

  frames.sort((a, b) => a.timestampUs - b.timestampUs);
  frames.forEach((f, i) => (f.index = i));

  const frameRate = computeFrameRateReport(frames.map((f) => f.timestampUs));

  return { frames, frameRate };
}
