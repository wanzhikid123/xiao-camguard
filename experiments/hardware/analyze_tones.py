"""Inspect the physical 880 Hz PC stimulus in a captured mono 16 kHz WAV.

This measures tone capture, not speech intelligibility or subjective noise quality.
The baseline harness plays four half-second pulses at 5, 6, 7 and 8 seconds.
"""
import argparse
import array
import json
import math
from pathlib import Path
import statistics
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    args = parser.parse_args()
    with wave.open(str(args.wav), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, 16000):
            parser.error("Expected 16 kHz mono 16-bit PCM")
        if source.getnframes() > 16000 * 180:
            parser.error("Capture exceeds the bounded test length")
        samples = array.array("h", source.readframes(source.getnframes()))
    size = 4000
    cosine = [math.cos(2 * math.pi * 880 * i / 16000) for i in range(size)]
    sine = [math.sin(2 * math.pi * 880 * i / 16000) for i in range(size)]
    amplitudes = []
    for start in range(0, len(samples) - size + 1, size):
        window = samples[start:start + size]
        re = sum(value * weight for value, weight in zip(window, cosine))
        im = sum(value * weight for value, weight in zip(window, sine))
        amplitudes.append(2 * math.hypot(re, im) / size)
    background = statistics.median(amplitudes[4:16])
    best = None
    # Allow up to 1 s of output/capture latency; preserve all measured windows.
    for offset in range(18, 25):
        on = [statistics.mean(amplitudes[offset + i * 4:offset + i * 4 + 2]) for i in range(4)]
        off = [statistics.mean(amplitudes[offset + i * 4 + 2:offset + i * 4 + 4]) for i in range(4)]
        ratio = statistics.median(on) / max(statistics.median(off), background, 0.01)
        all_four = min(on) > max(1, background * 10)
        # A three-pulse alignment can have a slightly higher median ratio.
        # Prefer a match containing all four expected physical pulses before
        # ranking by contrast; retain the same detection thresholds.
        if best is None or (all_four, ratio) > (best["all_four_detected"], best["on_off_ratio"]):
            best = dict(start_seconds=offset / 4, on_amplitudes=on, off_amplitudes=off,
                        on_off_ratio=ratio, all_four_detected=all_four)
    report = dict(frequency_hz=880, window_seconds=0.25, background_amplitude=background,
                  amplitudes=amplitudes, pulse_match=best,
                  passed=best["on_off_ratio"] >= 4 and min(best["on_amplitudes"]) > max(1, background * 10),
                  speech_and_subjective_noise_unverified=True)
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
