# Dynamic DOM and Layout Lifecycle Fixes

[简体中文](../../zh-CN/bugfixes/2026-09-20-dynamic-layout.zh-CN.md)

## Triggers and root causes

Repeated replacement of dynamic lists, text-only updates, flex/grid rebuilds, and access to detached nodes exercise several invalidation and layout paths. Call traces and differential tests confirmed that stale weak render references could accumulate or mask valid placements, flex containers did not register themselves, and anonymous blocks could be initialized more than once. Frequent queries and rebuilds also repeatedly allocated indexes, font keys, parsing scratch storage, and list nodes.

## Fix

Render reference registration and cleanup now follow a common path, with node identity and invalidation checks preserved. ID and tag queries use weak-reference indexes that are invalidated when the DOM changes. The C API now exposes attribute enumeration and other bridge operations. The font cache uses a complete structured key. Parsing scratch storage and layout lists are reused or transferred. Layout is retained only for numeric text updates that meet strict conditions and are proven to have the same measured dimensions; general mutations still follow the full path.

The document-level layout worker pool and isolated-document probe remain available for diagnostics. `LITEHTML_LAYOUT_WORKERS` defaults to `0`. Every prior end-to-end run with a positive worker budget was slower than serial execution, so the results do not justify enabling parallel layout by default.

## Verification

On 2026-09-20, the complete candidate source built successfully in Release, and the CTest target `litehtml-layout-api-tests` passed (42 internal cases). Coverage included node identity, render references, hit testing for split links, flex/grid geometry, index updates, text updates, styles, and comparisons between serial and parallel execution. On the same day, all 153 D3D12 automation tests for the companion UE plugin passed, with zero failures or skipped runs and an exit code of 0.

This batch archives the source and existing diagnostics. It does not establish a new performance result. Real-world tail response latency and script interruption under heavy load are still tracked in the companion repository. Regression checks should keep comparing geometry, drawing, and click hit testing against a full reflow; cache hit counts alone are insufficient.
