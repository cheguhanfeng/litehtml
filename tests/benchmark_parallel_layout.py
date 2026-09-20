"""Run the bounded native isolated-document experiment, not UE acceptance.

Build with -DLITEHTML_BUILD_PARALLEL_LAYOUT_PROBE=ON. The C++ process owns the
threads; Python only freezes inputs, launches once and summarizes raw samples.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--exe', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path(__file__).resolve().parents[2] / 'LiteHTMLViewDemo/Tests/performance107')
args = p.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
source = (args.fixture / 'business.html').read_text(encoding='utf-8')
source = re.sub(r'<script\b[^>]*>.*?</script>', '', source, flags=re.S)
source = re.sub(r'<link\b[^>]*>', '', source)
css = '\n'.join((args.fixture / f).read_text(encoding='utf-8') for f in ('common.css', 'native-layout.css'))
source = source.replace('</head>', '<style>' + css + '</style></head>')
fixture = args.output / 'frozen-business.html'
fixture.write_text(source, encoding='utf-8')
start = time.perf_counter()
run = subprocess.run([str(args.exe.resolve()), str(fixture.resolve()), str((args.output / 'samples.csv').resolve())],
                     capture_output=True, timeout=90)
(args.output / 'run.log').write_bytes(run.stdout + run.stderr)
report = dict(exitCode=run.returncode, wallSeconds=time.perf_counter()-start,
              scope='Native private-document decomposition; fallback fonts, no JS/GDI/paint or production commit',
              workers='0 = owner full document; 1/2/4 = that many dedicated native threads, owner waits',
              startup='Pool/document creation and vocabulary prewarm excluded from steady-state samples; milliseconds in run.log',
              limitations=['Fixed vocabulary prewarmed because global string IDs are not proven safe during concurrent insertion',
                           'Full page shell duplicated per worker; output covers row/image/span boxes, text, IDs and list height only',
                           'All four private documents remain allocated in every mode; no per-mode memory comparison',
                           'No production tree commit, positioned/overflow/hit-test state, paint output or UE font callbacks',
                           'Extraction and deterministic result merge are included; source DOM snapshot extraction is not implemented'],
              hashes={str(path.resolve()):hashlib.sha256(path.read_bytes()).hexdigest() for path in
                      [args.exe, args.exe.parent / 'litehtml.dll', Path(__file__), Path(__file__).with_name('parallel_layout_probe.cpp'),
                       fixture, *(args.fixture / f for f in ('business.html', 'common.css', 'native-layout.css'))]})
if run.returncode == 0:
    samples = list(csv.DictReader((args.output / 'samples.csv').open()))
    groups = []
    for count in (200, 400, 800):
        for width in (1920, 800):
            for workers in (0, 1, 2, 4):
                rows = [row for row in samples if (int(row['rows']), int(row['width']), int(row['workers'])) == (count,width,workers)]
                values = {key:statistics.median(float(row[key]) for row in rows) for key in rows[0] if key.endswith('Ms')}
                groups.append(dict(rows=count,width=width,workers=workers,samples=len(rows),medians=values))
    report['results'] = groups
    report['validation'] = '108 paired states; three candidates each; reverse order, quantity replacement, widths 1920 -> 800 -> 1920'
    for group in groups:
        if group['width'] == 1920:
            print(group['rows'], group['workers'], round(group['medians']['totalMs'], 3), flush=True)
(args.output / 'summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
if run.returncode:
    raise SystemExit('Native probe failed; see run.log')
