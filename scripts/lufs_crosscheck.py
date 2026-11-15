"""Compare this project's loudness / true-peak results with FFmpeg's ebur128 filter.

    python3 scripts/lufs_crosscheck.py            (after scripts/build.sh)

build/lufs_crosscheck renders programme WAVs and prints our Integrated loudness and true-peak
maximum; FFmpeg measures the same files independently. Tolerances: I +-0.1 LU, TP +-0.3 dB.
Skips (exit 0) when ffmpeg is not installed. Standard library only.
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, 'build', 'lufs_crosscheck')
OUT = os.path.join(ROOT, 'build', 'crosscheck')


def ffmpeg_measure(path):
    res = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                         capture_output=True, text=True)
    text = res.stderr[res.stderr.rfind('Summary:'):]
    i = re.search(r'I:\s+(-?[\d.]+|-inf)\s+LUFS', text)
    p = re.search(r'True peak:\s+Peak:\s+(-?[\d.]+|-inf)\s+dBFS', text)
    if not i or not p:
        sys.exit('lufs_crosscheck: could not parse FFmpeg output for %s' % path)
    return float(i.group(1)), float(p.group(1))


def main():
    if shutil.which('ffmpeg') is None:
        print('  SKIP ffmpeg not installed')
        return
    os.makedirs(OUT, exist_ok=True)
    lines = subprocess.run([TOOL, OUT], capture_output=True, text=True, check=True).stdout.split('\n')
    fails = 0
    for line in filter(None, lines):
        path, ours_i, ours_tp = line.split()
        ours_i, ours_tp = float(ours_i), float(ours_tp)
        ff_i, ff_tp = ffmpeg_measure(path)
        ok = abs(ours_i - ff_i) <= 0.1 and abs(ours_tp - ff_tp) <= 0.3
        fails += 0 if ok else 1
        print('  %s %-18s I ours %7.2f ffmpeg %7.2f | TP ours %6.2f ffmpeg %6.2f'
              % ('PASS' if ok else 'FAIL', os.path.basename(path)[:-4], ours_i, ff_i, ours_tp, ff_tp))
    if fails:
        sys.exit('lufs_crosscheck: %d programme(s) disagree with FFmpeg' % fails)


if __name__ == '__main__':
    main()
