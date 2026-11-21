"""Extract the CD audio tracks listed in a CUE sheet to Ogg Vorbis files.

Usage: extract_audio.py AKUMA.cue OUTPUT_DIR [--ffmpeg PATH] [--quality 5]
Writes OUTPUT_DIR/track02.ogg, track03.ogg... (numbers match the CD tracks).
"""
import argparse
from pathlib import Path
import re
import subprocess

SECTOR = 2352
FRAMES_PER_SECOND = 75


def parse_cue(cue):
    """Return (bin_name, {track_number: first_sector}) for the audio tracks."""
    bin_name = None
    track = None
    kind = None
    starts = {}
    for line in cue.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        match = re.match(r'FILE\s+"(.+)"\s+BINARY', line, re.I)
        if match:
            bin_name = match.group(1)
            continue
        match = re.match(r"TRACK\s+(\d+)\s+(\S+)", line, re.I)
        if match:
            track, kind = int(match.group(1)), match.group(2).upper()
            continue
        match = re.match(r"INDEX\s+01\s+(\d+):(\d+):(\d+)", line, re.I)
        if match and track is not None:
            minutes, seconds, frames = map(int, match.groups())
            starts[track] = (minutes * 60 + seconds) * FRAMES_PER_SECOND + frames
            if kind != "AUDIO":
                starts[track] = (starts[track], False)
            else:
                starts[track] = (starts[track], True)
    if bin_name is None or not starts:
        raise ValueError("CUE sem FILE ou TRACK/INDEX.")
    return bin_name, starts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cue", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--quality", default="5", help="qualidade Vorbis (0-10)")
    args = parser.parse_args()

    bin_name, starts = parse_cue(args.cue)
    image = args.cue.parent / bin_name
    size_sectors = image.stat().st_size // SECTOR
    numbers = sorted(starts)
    args.output.mkdir(parents=True, exist_ok=True)

    with image.open("rb") as stream:
        for index, number in enumerate(numbers):
            first, audio = starts[number]
            if not audio:
                continue
            last = starts[numbers[index + 1]][0] if index + 1 < len(numbers) else size_sectors
            stream.seek(first * SECTOR)
            raw = stream.read((last - first) * SECTOR)
            target = args.output / f"track{number:02d}.ogg"
            subprocess.run(
                [args.ffmpeg, "-y", "-loglevel", "error", "-f", "s16le", "-ar", "44100",
                 "-ac", "2", "-i", "-", "-c:a", "libvorbis", "-q:a", args.quality, str(target)],
                input=raw, check=True)
            print(f"{target.name}: {(last - first) / FRAMES_PER_SECOND:.0f}s, "
                  f"{target.stat().st_size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
