#include "types.h"
#include "render_flex.h"
#include "html_tag.h"

litehtml::rendered_width litehtml::render_item_flex::_render_content(pixel_t x, pixel_t y, bool /*second_pass*/,
                                                                     const containing_block_context& self_size,
                                                                     formatting_context*             fmt_ctx)
{
	if(css().get_display() == display_grid || css().get_display() == display_inline_grid)
	{
		const auto resolve_gap = [&](const css_length& gap) {
			return gap.units() == css_units_percentage ? gap.calc_percent(self_size.render_width) : pixel_t(gap.val());
		};
		const pixel_t column_gap = resolve_gap(css().get_column_gap());
		const pixel_t row_gap = resolve_gap(css().get_row_gap());

		// Keep this intentionally small grid implementation focused on the subset
		// used by embedded pages: fixed tracks, fractional tracks, repeat(), and
		// auto-fill(minmax()).  Splitting outside parentheses is important: a
		// minmax() expression is one track, not two whitespace-separated tokens.
		auto split_tracks = [](const std::string& value) {
			std::vector<std::string> result;
			int depth = 0;
			size_t start = std::string::npos;
			for(size_t i = 0; i <= value.size(); ++i)
			{
				const char ch = i == value.size() ? ' ' : value[i];
				if(ch == '(') ++depth;
				else if(ch == ')' && depth > 0) --depth;
				if(std::isspace(static_cast<unsigned char>(ch)) && depth == 0)
				{
					if(start != std::string::npos) { result.push_back(value.substr(start, i - start)); start = std::string::npos; }
				}
				else if(start == std::string::npos) start = i;
			}
			return result;
		};
		auto parse_number = [](const std::string& value) { return std::strtof(value.c_str(), nullptr); };
		auto expand_tracks = [&](const std::string& definition, pixel_t available, pixel_t gap) {
			std::vector<std::string> tracks;
			for(const auto& token : split_tracks(definition))
			{
				if(token.rfind("repeat(", 0) != 0 || token.back() != ')') { tracks.push_back(token); continue; }
				const auto comma = token.find(',');
				if(comma == std::string::npos) { tracks.push_back(token); continue; }
				std::string count = token.substr(7, comma - 7);
				count.erase(std::remove_if(count.begin(), count.end(), [](unsigned char c) { return std::isspace(c); }), count.end());
				const std::string repeated = token.substr(comma + 1, token.size() - comma - 2);
				int repetitions = std::max(1, std::atoi(count.c_str()));
				if(count == "auto-fill" || count == "auto-fit")
				{
					pixel_t minimum = 1_px;
					const auto minmax = repeated.find("minmax(");
					const auto comma_in_minmax = repeated.find(',', minmax);
					if(minmax != std::string::npos && comma_in_minmax != std::string::npos)
					{
						const std::string minimum_text = repeated.substr(minmax + 7, comma_in_minmax - minmax - 7);
						minimum = pixel_t(std::max(1.f, parse_number(minimum_text)));
					}
					repetitions = std::max(1, static_cast<int>((available.value() + gap.value()) / (minimum.value() + gap.value())));
				}
				const auto nested = split_tracks(repeated);
				for(int n = 0; n < repetitions; ++n) tracks.insert(tracks.end(), nested.begin(), nested.end());
			}
			if(tracks.empty()) tracks.push_back("1fr");
			return tracks;
		};
		auto resolve_tracks = [&](const std::vector<std::string>& tracks, pixel_t available, pixel_t gap) {
			std::vector<pixel_t> sizes(tracks.size(), 0_px);
			float fraction_total = 0.f;
			pixel_t fixed = gap * static_cast<int>(tracks.size() - 1);
			for(size_t i = 0; i < tracks.size(); ++i)
			{
				const auto& track = tracks[i];
				if(track.size() > 2 && track.compare(track.size() - 2, 2, "fr") == 0) fraction_total += std::max(0.f, parse_number(track));
				else if(track.rfind("minmax(", 0) == 0) fraction_total += 1.f;
				else if(track.size() > 1 && track.back() == '%') { sizes[i] = pixel_t(available.value() * parse_number(track) / 100.f); fixed += sizes[i]; }
				else { sizes[i] = pixel_t(std::max(0.f, parse_number(track))); fixed += sizes[i]; }
			}
			const pixel_t unit = fraction_total > 0.f ? std::max(0_px, (available - fixed) / fraction_total) : 0_px;
			for(size_t i = 0; i < tracks.size(); ++i)
				if((tracks[i].size() > 2 && tracks[i].compare(tracks[i].size() - 2, 2, "fr") == 0) || tracks[i].rfind("minmax(", 0) == 0)
					sizes[i] = unit * (tracks[i].rfind("minmax(", 0) == 0 ? 1.f : std::max(0.f, parse_number(tracks[i])));
			return sizes;
		};

		const pixel_t container_width = self_size.render_width.value;
		const auto column_tracks = expand_tracks(css().get_grid_template_columns(), container_width, column_gap);
		const auto column_sizes = resolve_tracks(column_tracks, container_width, column_gap);
		const int columns = static_cast<int>(column_sizes.size());
		// Omitted grid-template-rows creates implicit auto rows.  It must not
		// inherit the column fallback of 1fr, otherwise every ordinary one-row
		// grid (such as a table row) expands to the viewport height.
		const auto row_tracks = css().get_grid_template_rows().empty()
			? std::vector<std::string>{} : expand_tracks(css().get_grid_template_rows(), self_size.height.value, row_gap);
		const auto row_sizes = row_tracks.empty()
			? std::vector<pixel_t>{} : resolve_tracks(row_tracks, self_size.height.value, row_gap);

		// Grid item coordinates are local to this formatting context. Passing the
		// grid's own x/y here applies its document offset a second time when paint
		// placement walks the ancestor chain (the dashboard's first row was shifted
		// down by exactly this duplicated offset).
		pixel_t grid_y = 0_px;
		pixel_t natural_width = 0_px;
		std::vector<std::shared_ptr<render_item>> grid_children(m_children.begin(), m_children.end());
		std::stable_sort(grid_children.begin(), grid_children.end(), [](const auto& left, const auto& right) {
			return left->css().get_order() < right->css().get_order();
		});
		auto child_it = grid_children.begin();
		int row = 0;
		while(child_it != grid_children.end())
		{
			pixel_t row_height = row < static_cast<int>(row_sizes.size()) ? row_sizes[row] : 0_px;
			pixel_t grid_x = 0_px;
			struct row_item { std::shared_ptr<render_item> child; pixel_t x; pixel_t width; };
			std::vector<row_item> current_row;
			for(int column = 0; column < columns && child_it != grid_children.end(); ++column, ++child_it)
			{
				auto& child = *child_it;
				const auto rendered = child->render(grid_x, grid_y, self_size.new_width(column_sizes[column]), fmt_ctx);
				row_height = std::max(row_height, child->height());
				natural_width = std::max(natural_width, rendered.natural_width);
				current_row.push_back({child, grid_x, column_sizes[column]});
				grid_x += column_sizes[column] + column_gap;
			}

			// CSS Grid's normal align-items value stretches auto-height items to the
			// row's cross size. Re-rendering with an exact height also lets percentage
			// descendants (such as the dashboard bar fills) resolve against that size.
			for(const auto& item : current_row)
			{
				auto align = item.child->css().get_flex_align_self();
				if(align == flex_align_items_auto) align = css().get_flex_align_items();
				if((align == flex_align_items_normal || align == flex_align_items_stretch) &&
				   item.child->css().get_height().is_predefined() && item.child->height() < row_height)
				{
					item.child->render(item.x, grid_y,
						self_size.new_width_height(item.width, row_height,
							containing_block_context::size_mode_exact_height), fmt_ctx);
				}
			}
			grid_y += row_height + row_gap;
			++row;
		}
		m_pos.height = std::max(0_px, grid_y - (grid_children.empty() ? 0_px : row_gap));
		m_pos.move_to(x, y);
		m_pos.x += content_offset_left();
		m_pos.y += content_offset_top();
		return {std::max(natural_width, container_width), container_width};
	}

    bool    is_row_direction    = true;
    bool    reverse             = false;
    pixel_t container_main_size = self_size.render_width;

    switch(css().get_flex_direction())
    {
    case flex_direction_column:
        is_row_direction = false;
        reverse          = false;
        break;
    case flex_direction_column_reverse:
        is_row_direction = false;
        reverse          = true;
        break;
    case flex_direction_row:
        is_row_direction = true;
        reverse          = false;
        break;
    case flex_direction_row_reverse:
        is_row_direction = true;
        reverse          = true;
        break;
    }

    bool single_line   = css().get_flex_wrap() == flex_wrap_nowrap;
    bool fit_container = false;

    if(!is_row_direction)
    {
        if(self_size.height.type != containing_block_context::cbc_value_type_auto)
        {
            container_main_size = self_size.height.value - box_sizing_height();
        } else
        {
            // Direction columns, height is auto - always in single line
            container_main_size = 0;
            single_line         = true;
            fit_container       = true;
        }
        if(self_size.min_height.type != containing_block_context::cbc_value_type_auto &&
           self_size.min_height.value > container_main_size)
        {
            container_main_size = self_size.min_height;
        }
        if(self_size.max_height.type != containing_block_context::cbc_value_type_auto &&
           self_size.max_height.value > container_main_size)
        {
            container_main_size = self_size.max_height;
            single_line         = false;
        }
    }

    const auto resolve_gap = [&](const css_length& gap) {
        return gap.units() == css_units_percentage ? gap.calc_percent(self_size.render_width)
                                                   : pixel_t(gap.val());
    };
    pixel_t main_gap  = resolve_gap(is_row_direction ? css().get_column_gap() : css().get_row_gap());
    pixel_t cross_gap = resolve_gap(is_row_direction ? css().get_row_gap() : css().get_column_gap());

    /////////////////////////////////////////////////////////////////
    /// Split flex items to lines
    /////////////////////////////////////////////////////////////////
    m_lines = get_lines(self_size, fmt_ctx, is_row_direction, container_main_size, single_line, main_gap);

    pixel_t sum_cross_size = 0_px;
    pixel_t sum_main_size  = 0_px;
    pixel_t ret_width      = 0_px;

    /////////////////////////////////////////////////////////////////
    /// Resolving Flexible Lengths
    /// REF: https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths
    /////////////////////////////////////////////////////////////////
    for(auto& ln : m_lines)
    {
        if(is_row_direction)
        {
            ret_width += ln.base_size;
        }
        ln.init(container_main_size, fit_container, is_row_direction, self_size, fmt_ctx);
        sum_cross_size += ln.cross_size;
        sum_main_size   = std::max(sum_main_size, ln.main_size);
        if(reverse)
        {
            ln.items.reverse();
        }
    }
    if(m_lines.size() > 1)
    {
        sum_cross_size += cross_gap * static_cast<int>(m_lines.size() - 1);
    }

    pixel_t free_cross_size = 0_px;
    bool    is_wrap_reverse = css().get_flex_wrap() == flex_wrap_wrap_reverse;
    if(container_main_size == 0_px)
    {
        container_main_size = sum_main_size;
    }

    /////////////////////////////////////////////////////////////////
    /// Calculate free cross size
    /////////////////////////////////////////////////////////////////
    if(is_row_direction)
    {
        if(self_size.height.type != containing_block_context::cbc_value_type_auto)
        {
            pixel_t height  = self_size.height.value - box_sizing_height();
            free_cross_size = height - sum_cross_size;
        }
    } else
    {
        free_cross_size = self_size.render_width.value - sum_cross_size;
        ret_width       = sum_cross_size;
    }

    /////////////////////////////////////////////////////////////////
    /// Fix align-content property
    /////////////////////////////////////////////////////////////////
    flex_align_content align_content = css().get_flex_align_content();
    if(align_content == flex_align_content_space_between)
    {
        // If the leftover free-space is negative or there is only a single flex line in the flex
        // container, this value is identical to flex-start.
        if(m_lines.size() == 1 || free_cross_size < 0_px)
        {
            align_content = flex_align_content_flex_start;
        }
    }
    if(align_content == flex_align_content_space_around)
    {
        // If the leftover free-space is negative or there is only a single flex line in the flex
        // container, this value is identical to flex-start.
        if(m_lines.size() == 1 || free_cross_size < 0_px)
        {
            align_content = flex_align_content_center;
        }
    }

    /////////////////////////////////////////////////////////////////
    /// Distribute free cross size for align-content: stretch
    /////////////////////////////////////////////////////////////////
    if(css().get_flex_align_content() == flex_align_content_stretch && free_cross_size > 0_px)
    {
        pixel_t add = free_cross_size / pixel_t(static_cast<int>(m_lines.size()));
        if(add > 0_px)
        {
            for(auto& ln : m_lines)
            {
                ln.cross_size   += add;
                free_cross_size -= add;
            }
        }
        if(!m_lines.empty())
        {
            while(free_cross_size > 0_px)
            {
                pixel_t distributeStep = 1_px;
                for(auto& ln : m_lines)
                {
                    ln.cross_size   += distributeStep;
                    free_cross_size -= distributeStep;
                }
            }
        }
    }

    /// Reverse lines for flex-wrap: wrap-reverse
    if(css().get_flex_wrap() == flex_wrap_wrap_reverse)
    {
        m_lines.reverse();
    }

    /////////////////////////////////////////////////////////////////
    /// Align flex lines
    /////////////////////////////////////////////////////////////////
    pixel_t line_pos        = 0_px;
    pixel_t add_before_line = 0_px;
    pixel_t add_after_line  = 0_px;
    switch(align_content)
    {
    case flex_align_content_flex_start:
        if(is_wrap_reverse)
        {
            line_pos = free_cross_size;
        }
        break;
    case flex_align_content_flex_end:
        if(!is_wrap_reverse)
        {
            line_pos = free_cross_size;
        }
        break;
    case flex_align_content_end:
        line_pos = free_cross_size;
        break;
    case flex_align_content_center:
        line_pos = free_cross_size / 2_px;
        break;
    case flex_align_content_space_between:
        add_after_line = free_cross_size / pixel_t(static_cast<int>(m_lines.size() - 1));
        break;
    case flex_align_content_space_around:
        add_before_line = add_after_line = free_cross_size / pixel_t(static_cast<int>(m_lines.size() * 2));
        break;
    default:
        if(is_wrap_reverse)
        {
            line_pos = free_cross_size;
        }
        break;
    }
    for(auto& ln : m_lines)
    {
        line_pos       += add_before_line;
        ln.cross_start  = line_pos;
        line_pos       += ln.cross_size + cross_gap + add_after_line;
    }

    /// Fix justify-content property
    flex_justify_content justify_content = css().get_flex_justify_content();
    if((justify_content == flex_justify_content_right || justify_content == flex_justify_content_left) &&
       !is_row_direction)
    {
        justify_content = flex_justify_content_start;
    }

    /////////////////////////////////////////////////////////////////
    /// Align flex items in flex lines
    /////////////////////////////////////////////////////////////////
    for(auto& ln : m_lines)
    {
        pixel_t height =
            ln.calculate_items_position(container_main_size, justify_content, is_row_direction, self_size, fmt_ctx);
        m_pos.height = std::max(m_pos.height, height);
    }

    // calculate the final position
    m_pos.move_to(x, y);
    m_pos.x += content_offset_left();
    m_pos.y += content_offset_top();

    return {ret_width, ret_width};
}

std::list<litehtml::flex_line> litehtml::render_item_flex::get_lines(
    const litehtml::containing_block_context& self_size, litehtml::formatting_context* fmt_ctx, bool is_row_direction,
    pixel_t container_main_size, bool single_line, pixel_t main_gap)
{
    bool reverse_main;
    bool reverse_cross = css().get_flex_wrap() == flex_wrap_wrap_reverse;

    if(is_row_direction)
    {
        reverse_main = css().get_flex_direction() == flex_direction_row_reverse;
    } else
    {
        reverse_main = css().get_flex_direction() == flex_direction_column_reverse;
    }

    std::list<flex_line>                  lines;
    flex_line                             line(reverse_main, reverse_cross);
    line.gap_size = main_gap;
    std::list<std::shared_ptr<flex_item>> items;
    int                                   src_order     = 0;
    bool                                  sort_required = false;
    def_value<int>                        prev_order(0);

    for(auto& el : m_children)
    {
        std::shared_ptr<flex_item> item = nullptr;
        if(is_row_direction)
        {
            item = std::make_shared<flex_item_row_direction>(el);
        } else
        {
            item = std::make_shared<flex_item_column_direction>(el);
        }
        item->init(self_size, fmt_ctx, css().get_flex_align_items());
        item->src_order = src_order++;

        if(prev_order.is_default())
        {
            prev_order = item->order;
        } else if(!sort_required && item->order != prev_order)
        {
            sort_required = true;
        }

        items.emplace_back(item);
    }

    if(sort_required)
    {
        items.sort([](const std::shared_ptr<flex_item>& item1, const std::shared_ptr<flex_item>& item2) {
            if(item1->order < item2->order)
            {
                return true;
            }
            if(item1->order == item2->order)
            {
                return item1->src_order < item2->src_order;
            }
            return false;
        });
    }

    // Add flex items to lines
    for(auto& item : items)
    {
        if(!line.items.empty() && !single_line && line.main_size + main_gap + item->main_size > container_main_size)
        {
            lines.emplace_back(line);
            line = flex_line(reverse_main, reverse_cross);
            line.gap_size = main_gap;
        }
        if(!line.items.empty())
        {
            line.base_size += main_gap;
        }
        line.base_size += item->base_size;
        line.main_size += item->main_size;
        if(line.items.size() > 1)
        {
            line.main_size += main_gap;
        }
        if(!item->auto_margin_main_start.is_default())
        {
            line.num_auto_margin_main_start++;
        }
        if(!item->auto_margin_main_end.is_default())
        {
            line.num_auto_margin_main_end++;
        }
        line.items.push_back(item);
    }
    // Add the last line to the lines list
    if(!line.items.empty())
    {
        lines.emplace_back(line);
    }
    return lines;
}

std::shared_ptr<litehtml::render_item> litehtml::render_item_flex::init()
{
    auto                 doc = src_el()->get_document();
    decltype(m_children) new_children;
    decltype(m_children) inlines;

    auto convert_inlines = [&]() {
        if(!inlines.empty())
        {
            // Find last not space
            auto not_space =
                std::find_if(inlines.rbegin(), inlines.rend(),
                             [&](const std::shared_ptr<render_item>& el) { return !el->src_el()->is_space(); });
            if(not_space != inlines.rend())
            {
                // Erase all spaces at the end
                inlines.erase((not_space.base()), inlines.end());
            }

            auto anon_el = std::make_shared<html_tag>(src_el());
            auto anon_ri = std::make_shared<render_item_block>(anon_el);
            for(const auto& inl : inlines)
            {
                anon_ri->add_child(inl);
            }
            anon_ri->parent(shared_from_this());

            new_children.push_back(anon_ri->init());
            inlines.clear();
        }
    };

    for(const auto& el : m_children)
    {
        if(el->src_el()->css().get_display() == display_inline_text)
        {
            if(!inlines.empty())
            {
                inlines.push_back(el);
            } else
            {
                if(!el->src_el()->is_white_space())
                {
                    inlines.push_back(el);
                }
            }
        } else
        {
            convert_inlines();
            if(el->src_el()->is_block_box())
            {
                // Add block boxes as is
                el->parent(shared_from_this());
                new_children.push_back(el->init());
            } else
            {
                // Wrap inlines with anonymous block box
                auto anon_el = std::make_shared<html_tag>(el->src_el());
                auto anon_ri = std::make_shared<render_item_block>(anon_el);
                anon_ri->add_child(el->init());
                anon_ri->parent(shared_from_this());
                new_children.push_back(anon_ri->init());
            }
        }
    }
    convert_inlines();
    children() = new_children;

    return shared_from_this();
}

litehtml::pixel_t litehtml::render_item_flex::get_first_baseline()
{
    if(css().get_flex_direction() == flex_direction_row || css().get_flex_direction() == flex_direction_row_reverse)
    {
        if(!m_lines.empty())
        {
            const auto& first_line = m_lines.front();
            if(first_line.first_baseline.type() != baseline::baseline_type_none)
            {
                return first_line.cross_start + first_line.first_baseline.get_offset_from_top(first_line.cross_size) +
                       content_offset_top();
            }
            if(first_line.last_baseline.type() != baseline::baseline_type_none)
            {
                return first_line.cross_start + first_line.last_baseline.get_offset_from_top(first_line.cross_size) +
                       content_offset_top();
            }
        }
    }
    if(!m_lines.empty())
    {
        if(!m_lines.front().items.empty())
        {
            return m_lines.front().items.front()->el->get_first_baseline() + content_offset_top();
        }
    }
    return height();
}

litehtml::pixel_t litehtml::render_item_flex::get_last_baseline()
{
    if(css().get_flex_direction() == flex_direction_row || css().get_flex_direction() == flex_direction_row_reverse)
    {
        if(!m_lines.empty())
        {
            const auto& first_line = m_lines.front();
            if(first_line.last_baseline.type() != baseline::baseline_type_none)
            {
                return first_line.cross_start + first_line.last_baseline.get_offset_from_top(first_line.cross_size) +
                       content_offset_top();
            }
            if(first_line.first_baseline.type() != baseline::baseline_type_none)
            {
                return first_line.cross_start + first_line.first_baseline.get_offset_from_top(first_line.cross_size) +
                       content_offset_top();
            }
        }
    }
    if(!m_lines.empty())
    {
        if(!m_lines.front().items.empty())
        {
            return m_lines.front().items.front()->el->get_last_baseline() + content_offset_top();
        }
    }
    return height();
}
