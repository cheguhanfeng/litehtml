// litehtml 纯 C 布局服务实现
//
// 提供 litehtml_layout_service 纯 C 入口，内部封装 litehtml::document 生命周期，
// 并通过 layout_container（继承 litehtml::document_container）把宿主回调桥接到纯 C 回调结构。
//
// 线程约定：本实现非线程安全，应由宿主单线程串行使用。

#include "litehtml_layout.h"

#include "document.h"
#include "html_tag.h"
#include "document_container.h"
#include "encodings.h"
#include "font_description.h"
#include "html.h"
#include "el_script.h"
#include "render_item.h"
#include "types.h"

#include <cstring>
#include <unordered_map>
#include <string>
#include <vector>

namespace
{
    // ---------- C++ -> 纯 C 类型转换 ----------

    inline litehtml_color ToCColor(const litehtml::web_color& c)
    {
        litehtml_color out;
        out.r = c.red;
        out.g = c.green;
        out.b = c.blue;
        out.a = c.alpha;
        return out;
    }

    inline litehtml_rect ToCRect(const litehtml::position& p)
    {
        litehtml_rect out;
        out.x = static_cast<float>(p.x);
        out.y = static_cast<float>(p.y);
        out.width = static_cast<float>(p.width);
        out.height = static_cast<float>(p.height);
        return out;
    }

    inline litehtml_rect ToCRect(const litehtml::size& s)
    {
        litehtml_rect out;
        out.x = 0.f;
        out.y = 0.f;
        out.width = static_cast<float>(s.width);
        out.height = static_cast<float>(s.height);
        return out;
    }

    inline litehtml_background_layer ToCLayer(const litehtml::background_layer& layer)
    {
        litehtml_background_layer out{};
        out.border_box = ToCRect(layer.border_box);
        out.radius_top_left_x = static_cast<float>(layer.border_radius.top_left_x);
        out.radius_top_left_y = static_cast<float>(layer.border_radius.top_left_y);
        out.radius_top_right_x = static_cast<float>(layer.border_radius.top_right_x);
        out.radius_top_right_y = static_cast<float>(layer.border_radius.top_right_y);
        out.radius_bottom_right_x = static_cast<float>(layer.border_radius.bottom_right_x);
        out.radius_bottom_right_y = static_cast<float>(layer.border_radius.bottom_right_y);
        out.radius_bottom_left_x = static_cast<float>(layer.border_radius.bottom_left_x);
        out.radius_bottom_left_y = static_cast<float>(layer.border_radius.bottom_left_y);
        return out;
    }

    inline litehtml_linear_gradient ToCLinearGradient(
        const litehtml::background_layer::linear_gradient& gradient,
        std::vector<litehtml_gradient_stop>& out_stops)
    {
        out_stops.clear();
        out_stops.reserve(gradient.color_points.size());
        for(const auto& point : gradient.color_points)
        {
            litehtml_gradient_stop stop;
            stop.offset = point.offset;
            stop.color = ToCColor(point.color);
            out_stops.push_back(stop);
        }

        litehtml_linear_gradient out;
        out.start_x = static_cast<float>(gradient.start.x);
        out.start_y = static_cast<float>(gradient.start.y);
        out.end_x = static_cast<float>(gradient.end.x);
        out.end_y = static_cast<float>(gradient.end.y);
        out.stops = out_stops.empty() ? nullptr : out_stops.data();
        out.stop_count = static_cast<int>(out_stops.size());
        return out;
    }

    inline litehtml_background_layer ToCClip(const litehtml::position& pos,
                                             const litehtml::border_radiuses& radius)
    {
        litehtml_background_layer out{};
        out.border_box = ToCRect(pos);
        out.radius_top_left_x = static_cast<float>(radius.top_left_x);
        out.radius_top_left_y = static_cast<float>(radius.top_left_y);
        out.radius_top_right_x = static_cast<float>(radius.top_right_x);
        out.radius_top_right_y = static_cast<float>(radius.top_right_y);
        out.radius_bottom_right_x = static_cast<float>(radius.bottom_right_x);
        out.radius_bottom_right_y = static_cast<float>(radius.bottom_right_y);
        out.radius_bottom_left_x = static_cast<float>(radius.bottom_left_x);
        out.radius_bottom_left_y = static_cast<float>(radius.bottom_left_y);
        return out;
    }

    inline litehtml_radial_gradient ToCRadialGradient(
        const litehtml::background_layer::radial_gradient& gradient,
        std::vector<litehtml_gradient_stop>& out_stops)
    {
        out_stops.clear();
        out_stops.reserve(gradient.color_points.size());
        for(const auto& point : gradient.color_points)
        {
            out_stops.push_back({point.offset, ToCColor(point.color)});
        }
        return {static_cast<float>(gradient.position.x), static_cast<float>(gradient.position.y),
                static_cast<float>(gradient.radius.x), static_cast<float>(gradient.radius.y),
                out_stops.empty() ? nullptr : out_stops.data(), static_cast<int>(out_stops.size())};
    }

    inline litehtml_size ToCSize(const litehtml::size& s)
    {
        litehtml_size out;
        out.width = static_cast<float>(s.width);
        out.height = static_cast<float>(s.height);
        return out;
    }

    inline litehtml_font_description ToCFont(const litehtml::font_description& f)
    {
        litehtml_font_description out;
        out.family = f.family.c_str();
        out.size = static_cast<float>(f.size);
        out.style = static_cast<int>(f.style);
        out.weight = f.weight;
        out.decoration_line = f.decoration_line;
        out.decoration_thickness = f.decoration_thickness.val();
        out.decoration_style = static_cast<int>(f.decoration_style);
        out.decoration_color = ToCColor(f.decoration_color);
        out.emphasis_style = f.emphasis_style.empty() ? nullptr : f.emphasis_style.c_str();
        out.emphasis_color = ToCColor(f.emphasis_color);
        out.emphasis_position = f.emphasis_position;
        return out;
    }

    inline litehtml_border ToCBorder(const litehtml::border& b)
    {
        litehtml_border out;
        out.width = static_cast<float>(b.width);
        out.color = ToCColor(b.color);
        out.style = static_cast<int>(b.style);
        return out;
    }

    inline litehtml_borders ToCBorders(const litehtml::borders& b)
    {
        litehtml_borders out;
        out.left = ToCBorder(b.left);
        out.top = ToCBorder(b.top);
        out.right = ToCBorder(b.right);
        out.bottom = ToCBorder(b.bottom);
        out.radius_top_left_x = static_cast<float>(b.radius.top_left_x);
        out.radius_top_left_y = static_cast<float>(b.radius.top_left_y);
        out.radius_top_right_x = static_cast<float>(b.radius.top_right_x);
        out.radius_top_right_y = static_cast<float>(b.radius.top_right_y);
        out.radius_bottom_right_x = static_cast<float>(b.radius.bottom_right_x);
        out.radius_bottom_right_y = static_cast<float>(b.radius.bottom_right_y);
        out.radius_bottom_left_x = static_cast<float>(b.radius.bottom_left_x);
        out.radius_bottom_left_y = static_cast<float>(b.radius.bottom_left_y);
        return out;
    }

    inline litehtml_list_marker ToCListMarker(const litehtml::list_marker& m)
    {
        litehtml_list_marker out;
        out.image = m.image.empty() ? nullptr : m.image.c_str();
        out.baseurl = m.baseurl;
        out.marker_type = static_cast<int>(m.marker_type);
        out.color = ToCColor(m.color);
        out.pos = ToCRect(m.pos);
        out.index = m.index;
        out.font = static_cast<uint64_t>(m.font);
        return out;
    }

    inline litehtml_media_features ToCMedia(const litehtml::media_features& m)
    {
        litehtml_media_features out;
        out.width = m.width;
        out.height = m.height;
        out.device_width = m.device_width;
        out.device_height = m.device_height;
        out.color = m.color;
        out.monochrome = m.monochrome;
        out.resolution = m.resolution;
        return out;
    }
} // namespace

namespace litehtml
{
    // ---------- 宿主回调适配器（内部 document_container 实现） ----------

    class layout_container : public document_container
    {
      public:
        explicit layout_container(const litehtml_layout_callbacks* cb) :
            m_cb(cb)
        {
        }

        ~layout_container() override = default;

        void* user() const
        {
            return m_cb ? m_cb->user : nullptr;
        }

        // ---- 字体 ----
        litehtml::uint_ptr create_font(const font_description& descr, const document* doc,
                                       font_metrics* fm) override
        {
            if(!m_cb || !m_cb->create_font)
            {
                return 0;
            }
            litehtml_font_description cf = ToCFont(descr);
            litehtml_font_metrics cfm{};
            void* h = m_cb->create_font(&cf, &cfm, user());
            if(fm)
            {
                fm->font_size = cfm.font_size;
                fm->height = cfm.height;
                fm->ascent = cfm.ascent;
                fm->descent = cfm.descent;
                fm->x_height = cfm.x_height;
                fm->ch_width = cfm.ch_width;
                fm->sub_shift = cfm.sub_shift;
                fm->super_shift = cfm.super_shift;
                fm->draw_spaces = cfm.draw_spaces != 0;
            }
            return reinterpret_cast<litehtml::uint_ptr>(h);
        }

        void delete_font(litehtml::uint_ptr hFont) override
        {
            if(m_cb && m_cb->delete_font)
            {
                m_cb->delete_font(reinterpret_cast<void*>(hFont), user());
            }
        }

        litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override
        {
            if(m_cb && m_cb->text_width)
            {
                return m_cb->text_width(text, reinterpret_cast<void*>(hFont), user());
            }
            return 0;
        }

        // ---- 单位转换 ----
        litehtml::pixel_t pt_to_px(float pt) const override
        {
            if(m_cb && m_cb->pt_to_px)
            {
                return m_cb->pt_to_px(pt, user());
            }
            return pt * 1.333f;
        }

        litehtml::pixel_t get_default_font_size() const override
        {
            if(m_cb && m_cb->get_default_font_size)
            {
                return m_cb->get_default_font_size(user());
            }
            return 16.f;
        }

        const char* get_default_font_name() const override
        {
            if(m_cb && m_cb->get_default_font_name)
            {
                return m_cb->get_default_font_name(user());
            }
            return "Roboto";
        }

        // ---- 绘制 ----
        void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
                       litehtml::web_color color, const litehtml::position& pos) override
        {
            if(m_cb && m_cb->draw_text)
            {
                litehtml_color cc = ToCColor(color);
                litehtml_rect cr = ToCRect(pos);
                m_cb->draw_text(text, reinterpret_cast<void*>(hFont), cc, &cr, user());
            }
        }

        void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                             const litehtml::web_color& color) override
        {
            if(m_cb && m_cb->draw_solid_fill)
            {
                litehtml_background_layer cl = ToCLayer(layer);
                litehtml_color cc = ToCColor(color);
                m_cb->draw_solid_fill(&cl, cc, user());
            }
        }

        void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders,
                          const litehtml::position& draw_pos, bool root) override
        {
            if(m_cb && m_cb->draw_borders)
            {
                litehtml_borders cb = ToCBorders(borders);
                litehtml_rect cr = ToCRect(draw_pos);
                m_cb->draw_borders(&cb, &cr, root ? 1 : 0, user());
            }
        }

        void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override
        {
            if(m_cb && m_cb->draw_list_marker)
            {
                litehtml_list_marker cm = ToCListMarker(marker);
                m_cb->draw_list_marker(&cm, user());
            }
        }

        // ---- 渐变 ----
        void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                  const litehtml::background_layer::linear_gradient& gradient) override
        {
            if(m_cb && m_cb->draw_linear_gradient)
            {
                litehtml_background_layer cl = ToCLayer(layer);
                std::vector<litehtml_gradient_stop> stops;
                litehtml_linear_gradient cg = ToCLinearGradient(gradient, stops);
                m_cb->draw_linear_gradient(&cl, &cg, user());
            }
        }

        void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                  const litehtml::background_layer::radial_gradient& gradient) override
        {
            if(m_cb && m_cb->draw_radial_gradient)
            {
                litehtml_background_layer cl = ToCLayer(layer);
                std::vector<litehtml_gradient_stop> stops;
                litehtml_radial_gradient cg = ToCRadialGradient(gradient, stops);
                m_cb->draw_radial_gradient(&cl, &cg, user());
            }
        }

        void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                 const litehtml::background_layer::conic_gradient& gradient) override
        {
            if(m_cb && m_cb->draw_conic_gradient)
            {
                litehtml_background_layer cl = ToCLayer(layer);
                m_cb->draw_conic_gradient(&cl, user());
            }
        }

        // ---- 图片 ----
        void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override
        {
            if(m_cb && m_cb->load_image)
            {
                m_cb->load_image(src, baseurl, redraw_on_ready ? 1 : 0, user());
            }
        }

        void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override
        {
            if(m_cb && m_cb->get_image_size)
            {
                litehtml_size cs{};
                m_cb->get_image_size(src, baseurl, &cs, user());
                sz.width = cs.width;
                sz.height = cs.height;
            }
            else
            {
                sz.width = 0;
                sz.height = 0;
            }
        }

        void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                        const std::string& url, const std::string& base_url) override
        {
            if(m_cb && m_cb->draw_image)
            {
                litehtml_background_layer cl = ToCLayer(layer);
                m_cb->draw_image(&cl, url.c_str(), base_url.c_str(), user());
            }
        }

        // ---- 视口 / 媒体 ----
        void get_viewport(litehtml::position& viewport) const override
        {
            if(m_cb && m_cb->get_viewport)
            {
                litehtml_rect cv{};
                m_cb->get_viewport(&cv, user());
                viewport.x = cv.x;
                viewport.y = cv.y;
                viewport.width = cv.width;
                viewport.height = cv.height;
            }
        }

        void get_media_features(litehtml::media_features& media) const override
        {
            if(m_cb && m_cb->get_media_features)
            {
                litehtml_media_features cm{};
                m_cb->get_media_features(&cm, user());
                media.width = cm.width;
                media.height = cm.height;
                media.device_width = cm.device_width;
                media.device_height = cm.device_height;
                media.color = cm.color;
                media.monochrome = cm.monochrome;
                media.resolution = cm.resolution;
            }
        }

        void get_language(std::string& language, std::string& culture) const override
        {
            language = "en";
            culture = "en-US";
        }

        // ---- 文档 / 事件 ----
        void set_caption(const char* caption) override
        {
            if(m_cb && m_cb->set_caption)
            {
                m_cb->set_caption(caption, user());
            }
        }

        void set_base_url(const char* base_url) override
        {
            if(m_cb && m_cb->set_base_url)
            {
                m_cb->set_base_url(base_url, user());
            }
        }

        void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override
        {
        }

        void on_anchor_click(const char* url, const litehtml::element::ptr& el) override
        {
            if(m_cb && m_cb->on_anchor_click)
            {
                m_cb->on_anchor_click(url, user());
            }
            if(m_cb && m_cb->on_anchor_click_ex)
            {
                const char* target = el ? el->get_attr("target", "") : "";
                m_cb->on_anchor_click_ex(url, target ? target : "", user());
            }
        }

        void draw_backdrop_filter(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                  float blur_radius) override
        {
            (void)hdc;
            if(m_cb && m_cb->draw_backdrop_blur && blur_radius > 0)
            {
                litehtml_background_layer cl = ToCLayer(layer);
                m_cb->draw_backdrop_blur(&cl, blur_radius, user());
            }
        }

        void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override
        {
            if(m_cb && m_cb->on_mouse_event)
            {
                m_cb->on_mouse_event(static_cast<int>(event), user());
            }
        }

        void set_cursor(const char* cursor) override
        {
            if(m_cb && m_cb->set_cursor)
            {
                m_cb->set_cursor(cursor, user());
            }
        }

        void transform_text(std::string& text, litehtml::text_transform tt) override
        {
            if(m_cb && m_cb->transform_text)
            {
                // 分配可增长的缓冲区，避免回调写入越界
                std::string buffer(text.size() * 2 + 16, '\0');
                std::memcpy(buffer.data(), text.data(), text.size());
                m_cb->transform_text(buffer.data(), static_cast<int>(tt), user());
                text = buffer.c_str();
            }
        }

        void import_css(std::string& text, const std::string& url, std::string& baseurl) override
        {
            if(m_cb && m_cb->import_css)
            {
                // External stylesheets are commonly much larger than the empty
                // placeholder passed by the parser. Give the host a documented
                // bounded response buffer instead of the previous 16-byte slot.
                constexpr size_t import_css_capacity = 1024 * 1024;
                std::string tbuf(import_css_capacity, '\0');
                std::memcpy(tbuf.data(), text.data(), text.size());
                std::string bbuf(baseurl.size() * 2 + 16, '\0');
                std::memcpy(bbuf.data(), baseurl.data(), baseurl.size());
                m_cb->import_css(tbuf.data(), url.c_str(), bbuf.data(), user());
                text = tbuf.c_str();
                baseurl = bbuf.c_str();
            }
        }

        void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override
        {
            if(m_cb && m_cb->push_clip)
            {
                const litehtml_background_layer clip = ToCClip(pos, bdr_radius);
                m_cb->push_clip(&clip, user());
            }
        }

        void del_clip() override
        {
            if(m_cb && m_cb->pop_clip)
            {
                m_cb->pop_clip(user());
            }
        }

        litehtml::element::ptr create_element(const char* tag_name, const litehtml::string_map& attributes,
                                              const std::shared_ptr<litehtml::document>& doc) override
        {
            return nullptr; // 使用 litehtml 内置元素
        }

      private:
        const litehtml_layout_callbacks* m_cb;
    };
} // namespace litehtml

// ------------------------------------------------------------------
// 纯 C 布局服务入口
// ------------------------------------------------------------------

struct litehtml_layout_service
{
    litehtml::layout_container  container;
    litehtml::document::ptr     doc;
    float                       viewport_w = 0.f;
    float                       viewport_h = 0.f;
    float                       scroll_x = 0.f;
    float                       scroll_y = 0.f;
    uint64_t                    next_node_id = 1;
    std::unordered_map<const litehtml::element*, uint64_t> node_ids;
};

struct litehtml_layout_element
{
    litehtml_layout_service* service = nullptr;
    litehtml::element::ptr element;
    mutable std::string text_cache;
};

namespace
{
    litehtml_layout_element* MakeElementHandle(litehtml_layout_service* service, const litehtml::element::ptr& element)
    {
        if(!service || !element) return nullptr;
        auto* handle = new litehtml_layout_element;
        handle->service = service;
        handle->element = element;
        return handle;
    }

    bool IsCurrentElement(const litehtml_layout_element* handle)
    {
        return handle && handle->service && handle->service->doc && handle->element &&
               handle->element->get_document() == handle->service->doc;
    }

    uint64_t GetNodeId(litehtml_layout_service* service, const litehtml::element::ptr& element)
    {
        if(!service || !element) return 0;
        const auto [it, inserted] = service->node_ids.emplace(element.get(), service->next_node_id);
        if(inserted) ++service->next_node_id;
        return it->second;
    }

    void CollectScripts(const litehtml::element::ptr& element, std::vector<const litehtml::el_script*>& scripts)
    {
        if(auto* script = dynamic_cast<litehtml::el_script*>(element.get())) scripts.push_back(script);
        for(const auto& child : element->children()) CollectScripts(child, scripts);
    }
}

LITEHTML_API litehtml_layout_service* litehtml_layout_create(const litehtml_layout_callbacks* cb)
{
    if(!cb)
    {
        return nullptr;
    }
    auto* service = new litehtml_layout_service{litehtml::layout_container(cb)};
    return service;
}

LITEHTML_API void litehtml_layout_destroy(litehtml_layout_service* service)
{
    delete service;
}

#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
LITEHTML_API void litehtml_layout_get_style_invalidation_stats(
    const litehtml_layout_service* service, litehtml_style_invalidation_stats* out_stats)
{
    if(!out_stats) return;
    *out_stats = {};
    if(!service || !service->doc) return;
    const auto& stats = service->doc->style_stats();
    out_stats->computed_refresh_count = stats.computed_refresh_count;
    out_stats->subtree_match_count = stats.subtree_match_count;
    out_stats->full_match_count = stats.full_match_count;
    out_stats->computed_refresh_elements = stats.computed_refresh_elements;
    out_stats->subtree_match_elements = stats.subtree_match_elements;
    out_stats->full_match_elements = stats.full_match_elements;
    out_stats->computed_refresh_ns = stats.computed_refresh_ns;
    out_stats->subtree_match_ns = stats.subtree_match_ns;
    out_stats->full_match_ns = stats.full_match_ns;
    out_stats->render_tree_fallback_count = stats.render_tree_fallback_count;
}

LITEHTML_API void litehtml_layout_reset_style_invalidation_stats(litehtml_layout_service* service)
{
    if(service && service->doc) service->doc->reset_style_stats();
}

LITEHTML_API void litehtml_layout_set_selector_index_enabled(int enabled)
{
    litehtml::css::set_selector_index_enabled(enabled != 0);
}

LITEHTML_API int litehtml_layout_get_selector_index_stats(
    const litehtml_layout_service* service, litehtml_selector_index_stats* out_stats)
{
    if(!out_stats) return 0;
    *out_stats = {};
    if(!service || !service->doc) return 0;
    const auto stats = service->doc->selector_index_stats();
    out_stats->build_ns = stats.build_ns;
    out_stats->query_count = stats.query_count;
    out_stats->total_rules_considered = stats.total_rules_considered;
    out_stats->candidate_rules = stats.candidate_rules;
    out_stats->index_bytes = stats.bytes;
    out_stats->enabled = stats.enabled ? 1 : 0;
    return 1;
}
#endif

LITEHTML_API int litehtml_layout_load_html(litehtml_layout_service* service,
                                           const char* html,
                                           const char* base_url,
                                           float viewport_width,
                                           float viewport_height)
{
    if(!service || !html)
    {
        return 0;
    }
    service->viewport_w = viewport_width;
    service->viewport_h = viewport_height;
    service->scroll_x = 0.f;
    service->scroll_y = 0.f;
    service->node_ids.clear();
    service->next_node_id = 1;

    std::string base = base_url ? base_url : "";
    // Browsers paint the document canvas to the viewport even when body content is
    // shorter.  Without this rule litehtml correctly sizes body to its content, but
    // the host widget's background becomes visible below a short JS page.
    static constexpr char viewport_canvas_styles[] = "html, body { min-height: 100vh; }";
    service->doc = litehtml::document::createFromString(
        litehtml::estring(html),
        &service->container,
        litehtml::master_css,
        viewport_canvas_styles);
    return service->doc ? 1 : 0;
}

LITEHTML_API int litehtml_layout_render(litehtml_layout_service* service,
                                        float max_width,
                                        int render_type)
{
    if(!service || !service->doc)
    {
        return 0;
    }
    litehtml::pixel_t mw = max_width > 0.f ? static_cast<litehtml::pixel_t>(max_width)
                                           : static_cast<litehtml::pixel_t>(service->viewport_w);
    litehtml::render_type rt = static_cast<litehtml::render_type>(render_type);
    service->doc->render(mw, rt);
    return 1;
}

LITEHTML_API int litehtml_layout_render_dirty(litehtml_layout_service* service,
                                              litehtml_layout_element* changed_root,
                                              float max_width,
                                              int render_type)
{
    if(!service || !service->doc || !IsCurrentElement(changed_root) || changed_root->service != service)
    {
        return 0;
    }
    const litehtml::pixel_t mw = max_width > 0.f ? static_cast<litehtml::pixel_t>(max_width)
                                                   : static_cast<litehtml::pixel_t>(service->viewport_w);
    service->doc->render_dirty(changed_root->element, mw, static_cast<litehtml::render_type>(render_type));
    return 1;
}

LITEHTML_API void litehtml_layout_draw(litehtml_layout_service* service)
{
    if(!service || !service->doc)
    {
        return;
    }
    // 页面滚动：draw 的 x/y 偏移加到每个元素位置，传入负滚动偏移实现视口滚动。
    service->doc->draw(0, -static_cast<litehtml::pixel_t>(service->scroll_x),
                          -static_cast<litehtml::pixel_t>(service->scroll_y), nullptr);
}

namespace
{
    void ClampScroll(litehtml_layout_service* service)
    {
        if(!service || !service->doc) return;
        const float content_w = static_cast<float>(service->doc->width());
        const float content_h = static_cast<float>(service->doc->height());
        const float max_x = content_w - service->viewport_w;
        const float max_y = content_h - service->viewport_h;
        service->scroll_x = max_x > 0.f ? std::max(0.f, std::min(service->scroll_x, max_x)) : 0.f;
        service->scroll_y = max_y > 0.f ? std::max(0.f, std::min(service->scroll_y, max_y)) : 0.f;
    }
}

LITEHTML_API void litehtml_layout_set_scroll(litehtml_layout_service* service, float x, float y)
{
    if(!service) return;
    service->scroll_x = x;
    service->scroll_y = y;
    ClampScroll(service);
}

LITEHTML_API void litehtml_layout_scroll_by(litehtml_layout_service* service, float dx, float dy)
{
    if(!service) return;
    service->scroll_x += dx;
    service->scroll_y += dy;
    ClampScroll(service);
}

LITEHTML_API void litehtml_layout_scroll_to(litehtml_layout_service* service, litehtml_layout_element* element)
{
    if(!service || !IsCurrentElement(element) || element->service != service) return;
    const auto placement = element->element->get_placement();
    // 目标元素顶部对齐视口顶部（保留少量边距）。
    service->scroll_y = static_cast<float>(placement.y) - 8.f;
    ClampScroll(service);
}

LITEHTML_API float litehtml_layout_get_scroll_x(litehtml_layout_service* service)
{
    return service ? service->scroll_x : 0.f;
}

LITEHTML_API float litehtml_layout_get_scroll_y(litehtml_layout_service* service)
{
    return service ? service->scroll_y : 0.f;
}

LITEHTML_API void litehtml_layout_get_content_size(litehtml_layout_service* service,
                                                   litehtml_size* size)
{
    if(!service || !service->doc || !size)
    {
        return;
    }
    litehtml::size s(service->doc->width(), service->doc->height());
    *size = ToCSize(s);
}

LITEHTML_API int litehtml_layout_get_script_count(litehtml_layout_service* service)
{
    if(!service || !service->doc) return 0;
    std::vector<const litehtml::el_script*> scripts;
    CollectScripts(service->doc->root(), scripts);
    return static_cast<int>(scripts.size());
}

LITEHTML_API const char* litehtml_layout_get_script(litehtml_layout_service* service, int index)
{
    if(!service || !service->doc || index < 0) return nullptr;
    std::vector<const litehtml::el_script*> scripts;
    CollectScripts(service->doc->root(), scripts);
    return index < static_cast<int>(scripts.size()) ? scripts[index]->text().c_str() : nullptr;
}

LITEHTML_API const char* litehtml_layout_get_script_src(litehtml_layout_service* service, int index)
{
    if(!service || !service->doc || index < 0) return nullptr;
    std::vector<const litehtml::el_script*> scripts;
    CollectScripts(service->doc->root(), scripts);
    return index < static_cast<int>(scripts.size()) ? scripts[index]->src() : nullptr;
}

LITEHTML_API litehtml_layout_element* litehtml_layout_get_element_by_id(litehtml_layout_service* service, const char* id)
{
    if(!service || !service->doc || !id) return nullptr;
    return MakeElementHandle(service, service->doc->root()->select_one("#" + std::string(id)));
}

LITEHTML_API litehtml_layout_element* litehtml_layout_query_selector(litehtml_layout_service* service, const char* selector)
{
    if(!service || !service->doc || !selector) return nullptr;
    return MakeElementHandle(service, service->doc->root()->select_one(selector));
}

LITEHTML_API litehtml_layout_element* litehtml_layout_create_element(litehtml_layout_service* service, const char* tag)
{
    if(!service || !service->doc || !tag) return nullptr;
    return MakeElementHandle(service, service->doc->create_element(tag, {}));
}

LITEHTML_API void litehtml_layout_element_destroy(litehtml_layout_element* element) { delete element; }

LITEHTML_API const char* litehtml_layout_element_get_attribute(const litehtml_layout_element* element, const char* name)
{
    return element && element->element && name ? element->element->get_attr(name) : nullptr;
}

LITEHTML_API int litehtml_layout_element_set_attribute(litehtml_layout_element* element, const char* name, const char* value)
{
    if(!IsCurrentElement(element) || !name || !value) return 0;
    element->element->set_attr(name, value);
    return 1;
}

LITEHTML_API int litehtml_layout_element_remove_attribute(litehtml_layout_element* element, const char* name)
{
    if(!IsCurrentElement(element) || !name) return 0;
    // litehtml models attributes as strings.  Erasing the source attribute is
    // important for HTML boolean attributes: checked="false" is still checked.
    auto tag = std::dynamic_pointer_cast<litehtml::html_tag>(element->element);
    if(!tag) return 0;
    if(!tag->remove_attr(name)) return 0;
    return 1;
}

LITEHTML_API const char* litehtml_layout_element_get_text(const litehtml_layout_element* element)
{
    if(!element || !element->element) return nullptr;
    element->text_cache.clear();
    element->element->get_text(element->text_cache);
    return element->text_cache.c_str();
}

LITEHTML_API uint64_t litehtml_layout_element_get_node_id(const litehtml_layout_element* element)
{
    return element ? GetNodeId(element->service, element->element) : 0;
}

LITEHTML_API const char* litehtml_layout_element_get_tag_name(const litehtml_layout_element* element)
{
    return element && element->element ? element->element->get_tagName() : nullptr;
}

LITEHTML_API litehtml_layout_element* litehtml_layout_element_get_parent(const litehtml_layout_element* element)
{
    return element && element->service && element->element ? MakeElementHandle(element->service, element->element->parent())
                                                           : nullptr;
}

LITEHTML_API int litehtml_layout_element_get_placement(const litehtml_layout_element* element, litehtml_rect* out_rect)
{
    if(!element || !element->element || !out_rect) return 0;
    const auto placement = element->element->get_placement();
    out_rect->x = placement.x;
    out_rect->y = placement.y;
    out_rect->width = placement.width;
    out_rect->height = placement.height;
    return 1;
}

LITEHTML_API int litehtml_layout_element_set_inner_html(litehtml_layout_element* element, const char* html)
{
    if(!IsCurrentElement(element) || !html) return 0;
    return element->service->doc->set_inner_html(element->element, html) ? 1 : 0;
}

LITEHTML_API int litehtml_layout_element_append_child(litehtml_layout_element* parent, litehtml_layout_element* child)
{
    if(!IsCurrentElement(parent) || !IsCurrentElement(child) || parent->service != child->service) return 0;
    return parent->service->doc->append_child(parent->element, child->element) ? 1 : 0;
}

LITEHTML_API int litehtml_layout_element_get_child_count(const litehtml_layout_element* parent)
{
    if(!parent || !parent->element) return 0;
    int count = 0;
    for(const auto& child : parent->element->children()) if(child && child->get_tagName()) ++count;
    return count;
}

LITEHTML_API litehtml_layout_element* litehtml_layout_element_get_child(const litehtml_layout_element* parent, int index)
{
    if(!parent || !parent->service || !parent->element || index < 0) return nullptr;
    for(const auto& child : parent->element->children())
    {
        if(!child || !child->get_tagName()) continue;
        if(index-- == 0) return MakeElementHandle(parent->service, child);
    }
    return nullptr;
}

LITEHTML_API int litehtml_layout_element_remove_child(litehtml_layout_element* parent, litehtml_layout_element* child)
{
    if(!IsCurrentElement(parent) || !IsCurrentElement(child) || parent->service != child->service) return 0;
    return parent->service->doc->remove_child(parent->element, child->element) ? 1 : 0;
}

LITEHTML_API int litehtml_layout_element_replace_child(litehtml_layout_element* parent, litehtml_layout_element* replacement, litehtml_layout_element* child)
{
    if(!IsCurrentElement(parent) || !IsCurrentElement(replacement) || !IsCurrentElement(child) ||
       parent->service != replacement->service || parent->service != child->service) return 0;
    return parent->service->doc->replace_child(parent->element, replacement->element, child->element) ? 1 : 0;
}

namespace
{
    void CollectElementsByTag(const litehtml::element::ptr& element, const std::string& tag,
        std::vector<litehtml::element::ptr>& result)
    {
        if(!element) return;
        const char* tag_name = element->get_tagName();
        if(tag_name && litehtml::lowcase(tag_name) == tag) result.push_back(element);
        for(const auto& child : element->children()) CollectElementsByTag(child, tag, result);
    }
}

LITEHTML_API int litehtml_layout_get_elements_by_tag_count(litehtml_layout_service* service, const char* tag)
{
    if(!service || !service->doc || !tag) return 0;
    std::vector<litehtml::element::ptr> elements;
    CollectElementsByTag(service->doc->root(), litehtml::lowcase(tag), elements);
    return static_cast<int>(elements.size());
}

LITEHTML_API litehtml_layout_element* litehtml_layout_get_element_by_tag(litehtml_layout_service* service, const char* tag, int index)
{
    if(!service || !service->doc || !tag || index < 0) return nullptr;
    std::vector<litehtml::element::ptr> elements;
    CollectElementsByTag(service->doc->root(), litehtml::lowcase(tag), elements);
    return index < static_cast<int>(elements.size()) ? MakeElementHandle(service, elements[index]) : nullptr;
}

namespace
{
    bool IgnoreRedrawBox(const litehtml::position&) { return false; }

    // Input arrives in viewport coordinates, while litehtml's render tree is
    // positioned in document coordinates. draw() applies the inverse offset
    // (-scroll_x, -scroll_y), so interactions must apply the matching positive
    // offset before hit testing or updating :hover/:active state.
    litehtml::position DocumentPoint(const litehtml_layout_service* service, float x, float y)
    {
        return litehtml::position(
            litehtml::pixel_t(x + service->scroll_x),
            litehtml::pixel_t(y + service->scroll_y),
            0,
            0);
    }
}

LITEHTML_API int litehtml_layout_on_mouse_move(litehtml_layout_service* service, float x, float y)
{
    if(!service || !service->doc) return 0;
    const auto point = DocumentPoint(service, x, y);
    return service->doc->on_mouse_over(point.x, point.y, point.x, point.y, IgnoreRedrawBox)
               ? 1
               : 0;
}

LITEHTML_API int litehtml_layout_on_mouse_down(litehtml_layout_service* service, float x, float y)
{
    if(!service || !service->doc) return 0;
    const auto point = DocumentPoint(service, x, y);
    return service->doc->on_lbutton_down(point.x, point.y, point.x, point.y, IgnoreRedrawBox)
               ? 1
               : 0;
}

LITEHTML_API int litehtml_layout_on_mouse_up(litehtml_layout_service* service, float x, float y)
{
	return litehtml_layout_on_mouse_up_ex(service, x, y, 1);
}

LITEHTML_API int litehtml_layout_on_mouse_up_ex(litehtml_layout_service* service, float x, float y, int activate_default)
{
    if(!service || !service->doc) return 0;
    const auto point = DocumentPoint(service, x, y);
    return service->doc->on_lbutton_up(point.x, point.y, point.x, point.y, IgnoreRedrawBox, activate_default != 0)
               ? 1
               : 0;
}

LITEHTML_API int litehtml_layout_on_mouse_cancel(litehtml_layout_service* service)
{
    if(!service || !service->doc) return 0;
    return service->doc->on_button_cancel(IgnoreRedrawBox) ? 1 : 0;
}

LITEHTML_API litehtml_layout_element* litehtml_layout_hit_test(litehtml_layout_service* service, float x, float y)
{
    if(!service || !service->doc || !service->doc->root_render()) return nullptr;
    const auto point = DocumentPoint(service, x, y);
    return MakeElementHandle(service, service->doc->root_render()->get_element_by_point(
                                          point.x, point.y, point.x, point.y, nullptr));
}
