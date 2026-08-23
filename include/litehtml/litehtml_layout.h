#ifndef LITEHTML_LAYOUT_H
#define LITEHTML_LAYOUT_H

// litehtml 纯 C 布局服务接口
//
// 本接口将 litehtml 的 C++ 布局能力以纯 C 形式暴露，用于跨 DLL 边界集成：
//   - 插件（宿主）实现 litehtml_layout_callbacks 回调结构，传入布局服务
//   - 布局服务在布局过程中调用这些回调，产出绘制数据
//   - 绘制指令（FCommand）等高层契约由插件侧定义，不在此接口内
//
// 线程约定：布局服务实例非线程安全，应由宿主单线程（逻辑线程）串行使用。

#include "litehtml_export.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 轻量纯 C 结构（跨 DLL 边界安全，仅标量）                              */
/* ------------------------------------------------------------------ */

typedef struct litehtml_color
{
    uint8_t r, g, b, a; // 0-255
} litehtml_color;

typedef struct litehtml_rect
{
    float x;
    float y;
    float width;
    float height;
} litehtml_rect;

typedef struct litehtml_size
{
    float width;
    float height;
} litehtml_size;

typedef struct litehtml_font_metrics
{
    float font_size;
    float height;
    float ascent;
    float descent;
    float x_height;
    float ch_width;
    float sub_shift;
    float super_shift;
    int   draw_spaces;
} litehtml_font_metrics;

typedef struct litehtml_font_description
{
    const char* family; // UTF-8
    float       size;
    int         style; // 见 litehtml_font_style_*
    int         weight;
    int         decoration_line;  // bitset
    float       decoration_thickness;
    int         decoration_style;
    litehtml_color decoration_color;
    const char* emphasis_style;   // UTF-8，可为 NULL
    litehtml_color emphasis_color;
    int         emphasis_position;
} litehtml_font_description;

/* 字体样式枚举（与 litehtml 对齐） */
enum litehtml_font_style
{
    litehtml_font_style_normal = 0,
    litehtml_font_style_italic = 1,
    litehtml_font_style_oblique = 2,
};

/* 列表标记类型（子集，随 litehtml） */
enum litehtml_list_style_type
{
    litehtml_list_style_none = 0,
    litehtml_list_style_circle = 1,
    litehtml_list_style_disc = 2,
    litehtml_list_style_square = 3,
    litehtml_list_style_decimal = 4,
};

typedef struct litehtml_list_marker
{
    const char*     image;      // 可为 NULL
    const char*     baseurl;    // 可为 NULL
    int             marker_type; // litehtml_list_style_type
    litehtml_color  color;
    litehtml_rect   pos;
    int             index;
    uint64_t        font;       // 字体句柄
} litehtml_list_marker;

typedef struct litehtml_border
{
    float          width;
    litehtml_color color;
    int            style;
} litehtml_border;

typedef struct litehtml_borders
{
    litehtml_border left;
    litehtml_border top;
    litehtml_border right;
    litehtml_border bottom;
    float           radius_top_left_x;
    float           radius_top_left_y;
    float           radius_top_right_x;
    float           radius_top_right_y;
    float           radius_bottom_right_x;
    float           radius_bottom_right_y;
    float           radius_bottom_left_x;
    float           radius_bottom_left_y;
} litehtml_borders;

typedef struct litehtml_media_features
{
    float width;
    float height;
    float device_width;
    float device_height;
    int   color;
    int   monochrome;
    float resolution;
} litehtml_media_features;

/* 背景绘制层。圆角是 CSS 像素，按左上、右上、右下、左下顺序存储。 */
typedef struct litehtml_background_layer
{
    litehtml_rect border_box;
    float          radius_top_left_x;
    float          radius_top_left_y;
    float          radius_top_right_x;
    float          radius_top_right_y;
    float          radius_bottom_right_x;
    float          radius_bottom_right_y;
    float          radius_bottom_left_x;
    float          radius_bottom_left_y;
} litehtml_background_layer;

/* 线性渐变：stop.offset 为 0-1，start/end 是相对 background layer 的坐标。 */
typedef struct litehtml_gradient_stop
{
    float          offset;
    litehtml_color color;
} litehtml_gradient_stop;

typedef struct litehtml_linear_gradient
{
    float start_x, start_y;
    float end_x, end_y;
    const litehtml_gradient_stop* stops;
    int stop_count;
} litehtml_linear_gradient;

typedef struct litehtml_radial_gradient
{
    float center_x, center_y;
    float radius_x, radius_y;
    const litehtml_gradient_stop* stops;
    int stop_count;
} litehtml_radial_gradient;

/* ------------------------------------------------------------------ */
/* 宿主回调表（等价 document_container 的纯 C 化）                       */
/* ------------------------------------------------------------------ */

typedef struct litehtml_layout_callbacks
{
    /* user 指针：回调的上下文（如插件对象） */
    void* user;

    /* 字体 */
    void*  (*create_font)(const litehtml_font_description* descr, litehtml_font_metrics* fm, void* user);
    void   (*delete_font)(void* hFont, void* user);
    float  (*text_width)(const char* text, void* hFont, void* user);

    /* 单位转换 */
    float  (*pt_to_px)(float pt, void* user);
    float  (*get_default_font_size)(void* user);
    const char* (*get_default_font_name)(void* user);

    /* 绘制 */
    void (*draw_text)(const char* text, void* hFont, litehtml_color color,
                      const litehtml_rect* pos, void* user);
    void (*draw_solid_fill)(const litehtml_background_layer* layer, litehtml_color color, void* user);
    void (*draw_borders)(const litehtml_borders* borders, const litehtml_rect* draw_pos, int root, void* user);
    void (*draw_list_marker)(const litehtml_list_marker* marker, void* user);

    /* 渐变 */
    void (*draw_linear_gradient)(const litehtml_background_layer* layer, const litehtml_linear_gradient* gradient, void* user);
    void (*draw_radial_gradient)(const litehtml_background_layer* layer, const litehtml_radial_gradient* gradient, void* user);
    void (*draw_conic_gradient)(const litehtml_background_layer* layer, void* user);

    /* 图片 */
    void (*load_image)(const char* src, const char* baseurl, int redraw_on_ready, void* user);
    void (*get_image_size)(const char* src, const char* baseurl, litehtml_size* sz, void* user);
    void (*draw_image)(const litehtml_background_layer* layer, const char* url, const char* base_url, void* user);

    /* 视口 / 媒体 */
    void (*get_viewport)(litehtml_rect* viewport, void* user);
    void (*get_media_features)(litehtml_media_features* media, void* user);

    /* 文档 / 事件 */
    void (*set_caption)(const char* caption, void* user);
    void (*set_base_url)(const char* base_url, void* user);
    void (*on_anchor_click)(const char* url, void* user);
    void (*on_mouse_event)(int event, void* user);
    void (*set_cursor)(const char* cursor, void* user);
    void (*transform_text)(char* text, int text_transform, void* user);
    void (*import_css)(char* text, const char* url, char* baseurl, void* user);
    /* Extended anchor notification. Kept at the end for source compatibility
       with hosts that only implement on_anchor_click. target is never NULL. */
    void (*on_anchor_click_ex)(const char* url, const char* target, void* user);

    /* Paint-time clipping. Calls are balanced and may be nested. The clip uses
       the same border-box/radius representation as a background layer. */
    void (*push_clip)(const litehtml_background_layer* clip, void* user);
    void (*pop_clip)(void* user);

    /* Backdrop filter. Emitted before the element's own background layers so
       the host can blur content already painted behind its border box. */
    void (*draw_backdrop_blur)(const litehtml_background_layer* layer, float radius, void* user);
} litehtml_layout_callbacks;

/* ------------------------------------------------------------------ */
/* 布局服务（纯 C 入口）                                                */
/* ------------------------------------------------------------------ */

/* 不透明句柄：布局服务实例 */
typedef struct litehtml_layout_service litehtml_layout_service;
typedef struct litehtml_layout_element litehtml_layout_element;

#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
/* Test/benchmark-only telemetry. Release plugin builds intentionally omit
   these exports so the production C ABI remains unchanged. */
typedef struct litehtml_style_invalidation_stats
{
    uint64_t computed_refresh_count;
    uint64_t subtree_match_count;
    uint64_t full_match_count;
    uint64_t computed_refresh_elements;
    uint64_t subtree_match_elements;
    uint64_t full_match_elements;
    uint64_t computed_refresh_ns;
    uint64_t subtree_match_ns;
    uint64_t full_match_ns;
    uint64_t render_tree_fallback_count;
    uint64_t containment_hit_count;
    uint64_t containment_fallback_count;
    uint64_t geometry_cache_hit_count;
    uint64_t geometry_cache_miss_count;
    uint64_t layout_visited_elements;
} litehtml_style_invalidation_stats;

LITEHTML_API void litehtml_layout_get_style_invalidation_stats(
    const litehtml_layout_service* service, litehtml_style_invalidation_stats* out_stats);
LITEHTML_API void litehtml_layout_reset_style_invalidation_stats(litehtml_layout_service* service);

typedef struct litehtml_selector_index_stats
{
    uint64_t build_ns;
    uint64_t query_count;
    uint64_t total_rules_considered;
    uint64_t candidate_rules;
    uint64_t index_bytes;
    int enabled;
} litehtml_selector_index_stats;

LITEHTML_API void litehtml_layout_set_selector_index_enabled(int enabled);
LITEHTML_API int litehtml_layout_get_selector_index_stats(
    const litehtml_layout_service* service, litehtml_selector_index_stats* out_stats);

typedef struct litehtml_selector_cache_stats
{
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t bypass_count;
    uint64_t stale_count;
    uint64_t evict_count;
    uint64_t validation_ns;
    uint64_t peak_entries_per_element;
} litehtml_selector_cache_stats;

LITEHTML_API void litehtml_layout_set_selector_cache_mode(int mode);
LITEHTML_API int litehtml_layout_get_selector_cache_stats(
    const litehtml_layout_service* service, litehtml_selector_cache_stats* out_stats);
LITEHTML_API int litehtml_layout_benchmark_refresh_selector_matches(litehtml_layout_service* service);
#endif

/* 布局模式（对应 litehtml render_type，顺序必须与 litehtml 一致） */
enum litehtml_render_type
{
    litehtml_render_all = 0,          // litehtml::render_all
    litehtml_render_no_fixed = 1,     // litehtml::render_no_fixed
    litehtml_render_fixed_only = 2,   // litehtml::render_fixed_only
};

/* 创建布局服务。cb 为宿主回调（可静态存储，须在服务生命周期内有效）。
   返回 NULL 表示创建失败。 */
LITEHTML_API litehtml_layout_service* litehtml_layout_create(const litehtml_layout_callbacks* cb);

/* 销毁布局服务。 */
LITEHTML_API void litehtml_layout_destroy(litehtml_layout_service* service);

/* 加载 HTML 字符串并设置视口。成功返回非 0。 */
LITEHTML_API int litehtml_layout_load_html(litehtml_layout_service* service,
                                           const char* html,
                                           const char* base_url,
                                           float viewport_width,
                                           float viewport_height);

/* 触发布局。max_width 为布局最大宽度（<=0 时用视口宽）。 */
LITEHTML_API int litehtml_layout_render(litehtml_layout_service* service,
                                        float max_width,
                                        int render_type);
/* Re-layout the changed subtree when safe. Normal-flow and structural edits
   transparently use the document render fallback to preserve CSS correctness. */
LITEHTML_API int litehtml_layout_render_dirty(litehtml_layout_service* service,
                                              litehtml_layout_element* changed_root,
                                              float max_width,
                                              int render_type);

/* 绘制。触发宿主回调产出绘制数据。 */
LITEHTML_API void litehtml_layout_draw(litehtml_layout_service* service);

/* 查询文档内容尺寸（布局后有效）。 */
LITEHTML_API void litehtml_layout_get_content_size(litehtml_layout_service* service,
                                                   litehtml_size* size);

/* 前向声明（scroll API 位于 DOM service 类型定义之前）。 */
/* ------------------------------------------------------------------ */
/* 页面滚动（宿主维护滚动偏移，draw 时应用负偏移实现视口滚动）           */
/* ------------------------------------------------------------------ */

/* 设置页面滚动偏移（像素，自动 clamp 到 [0, content - viewport]）。 */
LITEHTML_API void litehtml_layout_set_scroll(litehtml_layout_service* service, float x, float y);
/* 按增量滚动（滚轮等），自动 clamp。 */
LITEHTML_API void litehtml_layout_scroll_by(litehtml_layout_service* service, float dx, float dy);
/* 滚动到指定元素（元素顶部对齐视口顶部）。 */
LITEHTML_API void litehtml_layout_scroll_to(litehtml_layout_service* service, litehtml_layout_element* element);
/* 查询当前滚动偏移。 */
LITEHTML_API float litehtml_layout_get_scroll_x(litehtml_layout_service* service);
LITEHTML_API float litehtml_layout_get_scroll_y(litehtml_layout_service* service);

/* ------------------------------------------------------------------ */
/* Minimal DOM service. Element handles are owned by the caller and must
   be released before destroying the layout service. All strings are UTF-8. */
typedef struct litehtml_layout_element litehtml_layout_element;

LITEHTML_API int litehtml_layout_get_script_count(litehtml_layout_service* service);
LITEHTML_API const char* litehtml_layout_get_script(litehtml_layout_service* service, int index);
LITEHTML_API const char* litehtml_layout_get_script_src(litehtml_layout_service* service, int index);

LITEHTML_API litehtml_layout_element* litehtml_layout_get_element_by_id(litehtml_layout_service* service, const char* id);
LITEHTML_API litehtml_layout_element* litehtml_layout_query_selector(litehtml_layout_service* service, const char* selector);
LITEHTML_API litehtml_layout_element* litehtml_layout_create_element(litehtml_layout_service* service, const char* tag);
LITEHTML_API void litehtml_layout_element_destroy(litehtml_layout_element* element);
LITEHTML_API const char* litehtml_layout_element_get_attribute(const litehtml_layout_element* element, const char* name);
LITEHTML_API int litehtml_layout_element_set_attribute(litehtml_layout_element* element, const char* name, const char* value);
/* Removes an attribute (needed for boolean form attributes such as checked). */
LITEHTML_API int litehtml_layout_element_remove_attribute(litehtml_layout_element* element, const char* name);
LITEHTML_API const char* litehtml_layout_element_get_text(const litehtml_layout_element* element);
/* Stable only for the lifetime of the containing document generation. */
LITEHTML_API uint64_t litehtml_layout_element_get_node_id(const litehtml_layout_element* element);
LITEHTML_API const char* litehtml_layout_element_get_tag_name(const litehtml_layout_element* element);
/* Caller owns the returned handle. Returns NULL for the document root. */
LITEHTML_API litehtml_layout_element* litehtml_layout_element_get_parent(const litehtml_layout_element* element);
/* Border-box placement after litehtml_layout_render(). Returns 0 for an invalid
   element or before a document has been rendered. */
LITEHTML_API int litehtml_layout_element_get_placement(const litehtml_layout_element* element, litehtml_rect* out_rect);
LITEHTML_API int litehtml_layout_element_set_inner_html(litehtml_layout_element* element, const char* html);
LITEHTML_API int litehtml_layout_element_append_child(litehtml_layout_element* parent, litehtml_layout_element* child);
/* Element-child collection helpers. Returned handles are caller-owned. */
LITEHTML_API int litehtml_layout_element_get_child_count(const litehtml_layout_element* parent);
LITEHTML_API litehtml_layout_element* litehtml_layout_element_get_child(const litehtml_layout_element* parent, int index);
LITEHTML_API int litehtml_layout_element_remove_child(litehtml_layout_element* parent, litehtml_layout_element* child);
LITEHTML_API int litehtml_layout_element_replace_child(litehtml_layout_element* parent, litehtml_layout_element* replacement, litehtml_layout_element* child);
/* Returns caller-owned handles for elements whose tag name matches tag (ASCII case-insensitive).
   The count is stable for the current document generation only. */
LITEHTML_API int litehtml_layout_get_elements_by_tag_count(litehtml_layout_service* service, const char* tag);
LITEHTML_API litehtml_layout_element* litehtml_layout_get_element_by_tag(litehtml_layout_service* service, const char* tag, int index);

/* Logic-thread-only interaction entry points. Coordinates are document CSS pixels.
   They update litehtml's :hover/:active state and return non-zero when a repaint is needed. */
LITEHTML_API int litehtml_layout_on_mouse_move(litehtml_layout_service* service, float x, float y);
LITEHTML_API int litehtml_layout_on_mouse_down(litehtml_layout_service* service, float x, float y);
LITEHTML_API int litehtml_layout_on_mouse_up(litehtml_layout_service* service, float x, float y);
/* Same release transition but conditionally suppresses element click/default actions. */
LITEHTML_API int litehtml_layout_on_mouse_up_ex(litehtml_layout_service* service, float x, float y, int activate_default);
LITEHTML_API int litehtml_layout_on_mouse_cancel(litehtml_layout_service* service);
/* The returned caller-owned handle is valid only while its service is alive. */
LITEHTML_API litehtml_layout_element* litehtml_layout_hit_test(litehtml_layout_service* service, float x, float y);

#ifdef __cplusplus
}
#endif

#endif // LITEHTML_LAYOUT_H
