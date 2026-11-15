# Reducing Allocations in Style Storage and Default Backgrounds

[简体中文](../../zh-CN/bugfixes/2026-09-20-style-storage.zh-CN.md)

## Symptoms and evidence

The list replacement in issue 107 repeatedly parses and computes styles. The call chain showed that the property tree allocated a node for each entry, temporary background vectors were copied through const references, and each default background computation created seven single-element arrays. Existing native diagnostics in the companion demo measured a drop from 800 to 100 allocations across 100 actual background computations.

## Fix

Properties now use contiguous storage sorted by key, and temporary vectors produced during parsing are moved. Background properties use empty vectors under the existing `get_layer` default semantics, preserving the `image-none` layer. Before reparsing a variable shorthand, the input is saved, and iteration resumes by key after the storage moves. This avoids invalidated references into contiguous storage. Explicit properties, inheritance, `!important` precedence, and background layer counts are preserved.

## Verification and limits

On 2026-09-20, the complete candidate source was built in Release and tested with CTest. The C API integration regression suite passed, including cases for the style cascade, background resets, refreshing variable shorthands, and shared background layers. This verifies the final combination of changes; intermediate commits were not each tested independently.

Existing end-to-end samples recorded a response P95 of 51.848 ms, above the 50 ms threshold, and P50 regressed. Fewer allocations are not a substitute for end-to-end acceptance. The background diagnostic program and its CMake switch were committed in a subsequent diagnostics batch.

Regression checks: when changing contiguous storage, review every property reference that survives a write. When introducing a new default background representation, compare its behavior with `get_layer` and retain differential tests for explicit values, inheritance, and multiple layers.
