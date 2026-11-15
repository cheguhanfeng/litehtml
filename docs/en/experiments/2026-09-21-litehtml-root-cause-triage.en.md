# LiteHTML Performance Root Cause Investigation: Batch 05

[简体中文](../../zh-CN/experiments/2026-09-21-litehtml-root-cause-triage.zh-CN.md)

## Scope

This investigation uses the existing single-run data and the source as it stood at the time. It adds no samples and changes no production algorithms. The user asked for proof of a real problem and an estimate of the payoff before making changes; a costly change for a roughly 1% gain is not worthwhile. The investigation focuses on LiteHTML without assuming that LiteHTMLView has no remaining room for improvement.

The data pairs `LiteHTMLViewDemo/Saved/SinglePassAttribution/20260921-authorized-05/runs/{business,dashboard}/attribution.json` with the corresponding raw spans. Timings are instrumented wall-clock intervals, not CPU cycles. A share of cumulative time is not necessarily an achievable end-to-end gain.

## 1. Confirmed: small text updates can trigger full flow layout

Call chain: a 100 ms timer in `business.html` updates three inventory text values → the text-only branch of `document::set_inner_html` initializes text nodes and sets `m_flow_layout_dirty` → `document::render_dirty` sees that flag and calls `render` directly → `m_root_render->render` lays out the document from the root.

The business run recorded 616 flow events totaling 2303.5123 ms, with a P95 of 4.6715 ms. This accounted for approximately 22.8% of the 10107.641549 ms covered on the logic thread. Text-only updates retain the formatting parent, but flow layout cannot be limited to the affected subtree. This is a real source of coarse invalidation. The existing events do not record the node that triggered each flow, so the 616 events cannot all be attributed to the inventory timer.

If half the flow work could be avoided, the estimated savings would be about 1.15 seconds of cumulative logic work (about 11.4% of covered time). That is a conditional estimate, not a measured speedup. General incremental layout would have to handle text widths, wrapping, sibling positions, container dimensions, positioning, and scroll ranges. Its cost and risk are high, so it should not be implemented at this stage. First establish what fraction of updates can be proven to leave geometry unchanged. The existing path for same-size numeric updates has strict preconditions; removing guards is not an acceptable optimization, and the hit rate for this run is unknown.

## 2. Confirmed: style computation repeatedly calls host text measurement

Call chain: structural mutation → `invalidate_structure_styles` (marks the tree dirty and schedules scoped selector matching) → `rematch_styles` → `html_tag::compute_styles` recurses through the subtree → `el_text::compute_styles` unconditionally calls `container::text_width`. Even when text and font are unchanged, this function does not compare the measurement inputs and reuse `m_size` at this point.

The raw logic thread events were sorted by start time, then by descending end time, and a nesting stack was used to identify the nearest ancestor other than `measure` for each `gdiRun`:

| Scenario | Total GDI runs | GDI runs inside `compute` | GDI time inside `compute` |
|---|---:|---:|---:|
| business | 41633 | 41311 | 622.8531 ms |
| dashboard | 147015 | 146938 | 2114.1727 ms |

The business run had 14 `compute` events longer than 10 ms, totaling 628.4545 ms; the dashboard had 105, totaling 2225.9592 ms. The dashboard's 105 `treeCreate` events match the count of long `compute` events. Together with the timer code that adds and removes event rows, that is strong evidence that structural changes amplify style and measurement work. There is no per-node correlation to prove an exact one-to-one match.

On each call, the host's `MeasureRunWidth` uses `CreateCompatibleDC`, `CreateFontW`, and `GetTextExtentPoint32W`, then destroys the font and device context. The wall-clock timings cover the whole function, so they cannot be attributed entirely to GDI glyph measurement or to LiteHTML's own computations. The trace lacks text and font keys, node identities, and timing for individual GDI steps. It cannot establish the hit rate for repeated measurements or the actual savings from resource reuse.

Business table rebuilds replace the entire table via `rows.innerHTML`, deleting the old text nodes. Node-level measurement reuse cannot carry over to new DOM nodes. The dashboard only adds or removes event rows, so existing nodes are candidates for reuse, but the savings remain unverified. A cache is not a validated solution. Text measurement totaled 679.8 ms for business and 2265.5 ms for dashboard (about 6.7% and 17.7% of covered logic time, respectively). For business sort, delete, and pagination actions, GDI averaged about 41–44 ms from the action to `publish`, so this work may matter more for tail latency than for average load.

## 3. Directions ruled out as low value

- Business selector matching totaled 26.8 ms and `treeCreate` took 46.7 ms, each under 0.5% of covered logic time. Costly indexing or tree construction changes are not justified.
- Parsing and DOM creation and teardown together took 134.5 ms (about 1.33%). Changing or replacing Gumbo is not a priority.
- QuickJS accounted for 103.77 ms of exclusive time in business (about 1.03%). Changes to the QuickJS engine are not a priority for the business page.
- Do not restart thread pool exploration or cache only the returned width. `render` changes geometry and line boxes as well as returning a width.
- Native drawing took 2306.7 ms for business and 3130.2 ms for dashboard, including host callbacks. The instrumentation is not granular enough to identify LiteHTML's own drawing code as the root cause.

## Decision

No production performance code changes in this batch. Two findings remain worth pursuing: text invalidation forcing full flow layout, and repeated measurements on retained nodes. The former accounts for substantial work but is costly to address correctly. The latter may offer a smaller gain and directly affect long operations, but this run did not establish a hit rate. Narrow both down using existing source and data first. The evidence does not establish a double-digit end-to-end gain.

If additional counters or sampling become necessary to distinguish the causes, report the gap, propose a single bounded run, and wait for the user's decision. This report does not authorize a new run. Any later implementation must identify the work it eliminates, define when font, text, and layout data become invalid, and include meaningful regression cases and end-to-end acceptance checks. A 1% microbenchmark gain or a lower allocation count alone is not sufficient.
