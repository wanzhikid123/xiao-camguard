"""Inspect a recording using ffprobe. Audible audio and drift require listening."""
import argparse
import json
import math
from fractions import Fraction
import subprocess
from pathlib import Path


def validate(probe):
    streams = probe.get("streams", [])
    video = [item for item in streams if item.get("codec_type") == "video"]
    audio = [item for item in streams if item.get("codec_type") == "audio"]
    errors = []
    if len(video) != 1 or video[0].get("codec_name") != "mjpeg":
        errors.append("Expected one MJPEG video stream")
    if len(audio) != 1 or audio[0].get("codec_name") != "pcm_s16le":
        errors.append("Expected one 16-bit PCM audio stream")
    elif audio[0].get("sample_rate") != "16000" or audio[0].get("channels") != 1:
        errors.append("Expected 16 kHz mono audio")
    if "avi" not in probe.get("format", {}).get("format_name", "").split(","):
        errors.append("Expected AVI container")
    durations = {}
    duration_sources = {}
    for label, group in (("video", video), ("audio", audio)):
        if group:
            raw = group[0].get("duration")
            if raw and raw != "N/A":
                durations[label] = float(raw)
                duration_sources[label] = "stream_duration"
            elif group[0].get("nb_frames") not in (None, "N/A") and group[0].get("time_base"):
                durations[label] = int(group[0]["nb_frames"]) * float(Fraction(group[0]["time_base"]))
                duration_sources[label] = "frame_count_times_time_base"
            if label in durations and (not math.isfinite(durations[label]) or durations[label] <= 0):
                errors.append(f"{label} stream has no finite positive duration")
                del durations[label]
    drift = None
    if len(durations) == 2:
        drift = abs(durations["video"] - durations["audio"])
        if drift > max(1.0, durations["video"] * .05):
            errors.append("Audio/video durations differ by more than 1 s or 5%")
    return dict(passed=not errors, errors=errors, durations_s=durations, duration_sources=duration_sources,
                duration_difference_s=drift, duration_comparison_available=len(durations) == 2,
                listening_and_long_recording_test_required=True, probe=probe)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path)
    args = parser.parse_args()
    try:
        result = subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-show_format",
                                 "-of", "json", str(args.recording)],
                                text=True, capture_output=True, check=True, timeout=60)
        report = validate(json.loads(result.stdout))
        print(json.dumps(report, indent=2))
        return 0 if report["passed"] else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(2, f"Unable to inspect AVI: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
