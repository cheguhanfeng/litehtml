"""Bounded full-document benchmark for the opt-in integrated layout pool.

Deterministic nonzero font metrics, actual original-tree commit, all preparation
and commit inside render timing. No UE/JS/GDI or input-to-paint acceptance.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

p = argparse.ArgumentParser()
p.add_argument('--dll', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--worker', type=int)
p.add_argument('--tag-queries', action='store_true', help='Include host control tag count/index queries after render')
args = p.parse_args()
fixture = Path(__file__).resolve().parents[2] / 'LiteHTMLViewDemo/Tests/performance107'
if args.worker is None:
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for workers in (0, 1, 2, 4, 8, 16, 32):
        env = dict(os.environ, LITEHTML_LAYOUT_WORKERS=str(workers),
                   LITEHTML_LAYOUT_PROFILE_PATH=str((args.output / f'profile-{workers}.jsonl').resolve()))
        path = args.output / f'workers-{workers}.json'
        run = subprocess.run([sys.executable, __file__, '--dll', str(args.dll.resolve()), '--output', str(path.resolve()),
                              '--worker', str(workers)] + (['--tag-queries'] if args.tag_queries else []),
                             env=env, capture_output=True, timeout=90)
        (args.output / f'workers-{workers}.log').write_bytes(run.stdout + run.stderr)
        if run.returncode: raise SystemExit(f'workers={workers} failed: see log')
        result = json.loads(path.read_text(encoding='utf-8'))
        results.append(result)
        print(workers, [(r['rows'], round(r['medianTotalMs'], 3)) for r in result['results']], flush=True)
    reference = [r['geometrySha256'] for r in results[0]['results']]
    assert all([r['geometrySha256'] for r in item['results']] == reference for item in results)
    summary = dict(scope=__doc__, results=results, geometryMatches=True, profileEnabled=True,
                   note='One process per worker budget; no affinity pinning; 2 warmup + 10 samples; no retries for favorable timing')
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    raise SystemExit(0)

dll = c.CDLL(str(args.dll.resolve()))
ptr = c.c_void_p
def bind(name, result, *params):
    fn = getattr(dll, 'litehtml_layout_' + name)
    fn.restype, fn.argtypes = result, list(params)
    return fn
create = bind('create', ptr, ptr)
destroy = bind('destroy', None, ptr)
load = bind('load_html', c.c_int, ptr, c.c_char_p, c.c_char_p, c.c_float, c.c_float)
render = bind('render', c.c_int, ptr, c.c_float, c.c_int)
get = bind('get_element_by_id', ptr, ptr, c.c_char_p)
release = bind('element_destroy', None, ptr)
replace = bind('element_set_inner_html', c.c_int, ptr, c.c_char_p)
placement = bind('element_get_placement', c.c_int, ptr, ptr)
tag_count = bind('get_elements_by_tag_count', c.c_int, ptr, c.c_char_p)
tag_at = bind('get_element_by_tag', ptr, ptr, c.c_char_p, c.c_int)
class Font(c.Structure):
    _fields_ = [('family', ptr), ('size', c.c_float)]
class Metrics(c.Structure):
    _fields_ = [(name, c.c_float) for name in ('font_size','height','ascent','descent','x_height','ch_width','sub_shift','super_shift')] + [('draw_spaces', c.c_int)]
@c.CFUNCTYPE(ptr, c.POINTER(Font), c.POINTER(Metrics), ptr)
def create_font(font, metrics, user):
    size = font.contents.size
    metrics[0] = Metrics(size, size*1.2, size*.8, size*.2, size*.5, size*.5, 0, 0, 1)
    return max(1, round(size*10))
@c.CFUNCTYPE(c.c_float, c.c_char_p, ptr, ptr)
def text_width(text, font, user):
    return len(text.decode('utf-8')) * (font / 10) * .5
callbacks = (ptr * 64)()
callbacks[1] = c.cast(create_font, ptr)
callbacks[3] = c.cast(text_width, ptr)
source = (fixture / 'business.html').read_text(encoding='utf-8')
source = re.sub(r'<script\b[^>]*>.*?</script>', '', source, flags=re.S)
source = re.sub(r'<link\b[^>]*>', '', source)
css = '\n'.join((fixture / f).read_text(encoding='utf-8') for f in ('common.css','native-layout.css'))
source = source.replace('</head>', '<style>' + css + '</style></head>')
results = []
for count in (200, 400, 800):
    service = create(callbacks)
    assert service and load(service, source.encode(), b'', 1920, 1080)
    rows = get(service, b'rows')
    samples = []
    for iteration in range(12):
        order = range(count) if iteration % 2 == 0 else reversed(range(count))
        html = ''.join(f'<div class="row" id="item-{i}" data-id="{i}"><img src="../fixtures/image_fixture.png"><span class="name">'
                       f'{"精密组件 Precision component " if i%3==0 else "仓储配件 Item "}{i:04}</span><span class="state">'
                       f'{"待处理 Pending" if i%3==0 else "已同步 Ready"}</span><span class="qty" id="qty-{i}">{20+(i+iteration)%73}</span></div>' for i in order).encode()
        begin = time.perf_counter_ns()
        assert replace(rows, html)
        parsed = time.perf_counter_ns()
        for i in range(count):
            node = get(service, f'qty-{i}'.encode()); assert node; release(node)
        queried = time.perf_counter_ns()
        assert render(service, 1920, 0)
        rendered = time.perf_counter_ns()
        if args.tag_queries:
            # Same tag enumeration pattern as native progress/form/video draw.
            for tag in (b'hr', b'progress', b'meter', b'input', b'textarea', b'select', b'button', b'video'):
                for index in range(tag_count(service, tag)):
                    node = tag_at(service, tag, index); assert node; release(node)
        end = time.perf_counter_ns()
        if iteration >= 2: samples.append(dict(parseMs=(parsed-begin)/1e6, queryMs=(queried-parsed)/1e6,
                                               renderMs=(rendered-queried)/1e6, tagQueryMs=(end-rendered)/1e6,
                                               totalMs=(end-begin)/1e6))
    geometry = []
    for i in range(count):
        for name in (f'item-{i}', f'qty-{i}'):
            node = get(service, name.encode()); rect = (c.c_float * 4)()
            assert node and placement(node, rect) and rect[2] > 0 and rect[3] > 0
            geometry.append(list(rect)); release(node)
    results.append(dict(rows=count, samples=samples, medianTotalMs=statistics.median(s['totalMs'] for s in samples),
                        medians={key:statistics.median(s[key] for s in samples) for key in samples[0]},
                        geometrySha256=hashlib.sha256(json.dumps(geometry).encode()).hexdigest()))
    release(rows); destroy(service)
args.output.write_text(json.dumps(dict(workers=args.worker, tagQueries=args.tag_queries, results=results,
    dllSha256=hashlib.sha256(args.dll.read_bytes()).hexdigest(), scriptSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    fixtureSha256=hashlib.sha256(source.encode()).hexdigest(), fontMetrics='nonzero deterministic font callback, width = Unicode codepoints * font size / 2'), indent=2), encoding='utf-8')
