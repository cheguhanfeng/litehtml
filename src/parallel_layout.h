#pragma once
#include "render_item.h"

namespace litehtml {
// Opt-in experiment. Successful return replaces only proven independent rows;
// positioned/extents/painting continue through the normal document pipeline.
bool render_parallel_rows(const std::shared_ptr<render_item>& parent,
                          const containing_block_context& size,
                          formatting_context* context, rendered_width& width, pixel_t& height);
}
