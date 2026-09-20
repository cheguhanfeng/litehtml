"""Text reflow with a retained 200/400/800-row business list; report mutation plus render.

No UE, JS or GDI costs: do not treat these timings as end-to-end acceptance.
Run the same script against saved before/after DLLs; keep every measured sample.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import re
import statistics
import time

p = argparse.ArgumentParser()
p.add_argument('--dll', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--fixture', type=Path, default=Path(__file__).resolve().parents[2] / 'LiteHTMLViewDemo/Tests/performance107')
args = p.parse_args()
dll = c.CDLL(str(args.dll.resolve()))
def bind(name, result, *params):
    fn = getattr(dll, 'litehtml_layout_' + name)
    fn.restype, fn.argtypes = result, list(params)
    return fn
ptr = c.c_void_p
create = bind('create', ptr, ptr)
destroy = bind('destroy', None, ptr)
load = bind('load_html', c.c_int, ptr, c.c_char_p, c.c_char_p, c.c_float, c.c_float)
render = bind('render', c.c_int, ptr, c.c_float, c.c_int)
get = bind('get_element_by_id', ptr, ptr, c.c_char_p)
release = bind('element_destroy', None, ptr)
replace = bind('element_set_inner_html', c.c_int, ptr, c.c_char_p)
reset = bind('reset_style_invalidation_stats', None, ptr)
stats = bind('get_style_invalidation_stats', None, ptr, ptr)
placement = bind('element_get_placement', c.c_int, ptr, ptr)
source = (args.fixture / 'business.html').read_text(encoding='utf-8')
source = re.sub(r'<script\b[^>]*>.*?</script>', '', source, flags=re.S)
source = re.sub(r'<link\b[^>]*>', '', source)
css = '\n'.join((args.fixture / f).read_text(encoding='utf-8') for f in ('common.css', 'native-layout.css'))
source = source.replace('</head>', '<style>' + css + '</style></head>')
results = []
for count in (200, 400, 800):
    callbacks = (ptr * 64)()
    service = create(callbacks)
    assert service and load(service, source.encode(), b'', 1920, 1080)
    rows = get(service, b'rows')
    html = ''.join(f'<div class="row" id="item-{i}"><img src="../fixtures/image_fixture.png"><span class="name">精密组件 Precision component {i:04}</span><span class="state">待处理 Pending</span><span class="qty">{20+i%73}</span></div>' for i in range(count))
    assert replace(rows, html.encode()) and render(service, 1920, 0)
    targets = [get(service, name) for name in (b'detail-title', b'detail-qty', b'action-state')]
    samples = []
    for iteration in range(12):
        reset(service)
        start = time.perf_counter_ns()
        for j, target in enumerate(targets):
            assert replace(target, (f'Updated label {iteration} field {j}' if iteration%2 else 'Longer updated label that can wrap into multiple lines').encode())
        mutated = time.perf_counter_ns()
        assert render(service, 1920, 0)
        end = time.perf_counter_ns()
        values = (c.c_uint64 * 15)()
        stats(service, values)
        if iteration >= 2:
            samples.append(dict(mutationMs=(mutated-start)/1e6, renderMs=(end-mutated)/1e6, totalMs=(end-start)/1e6, matchedNodes=sum(values[3:6])))
    geometry = []
    for name in [f'item-{i}'.encode() for i in range(count)] + [b'detail-title',b'detail-qty',b'action-state']:
        node = get(service, name)
        rect = (c.c_float * 4)()
        assert node and placement(node, rect)
        geometry.append(list(rect))
        release(node)
    for node in targets + [rows]: release(node)
    destroy(service)
    result = dict(rows=count, samples=samples, medians={key:statistics.median(s[key] for s in samples) for key in samples[0]}, geometrySha256=hashlib.sha256(json.dumps(geometry).encode()).hexdigest())
    results.append(result)
    print(count, result['medians'], flush=True)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(dict(dll=str(args.dll.resolve()), dllSha256=hashlib.sha256(args.dll.read_bytes()).hexdigest(), fixtureSha256=hashlib.sha256(source.encode()).hexdigest(), scriptSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), results=results), indent=2), encoding='utf-8')
