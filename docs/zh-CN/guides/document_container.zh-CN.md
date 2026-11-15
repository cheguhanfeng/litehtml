# litehtml::document_container

[English](../../en/guides/document_container.en.md)

`litehtml::document_container` 负责绘制 HTML 元素，并执行依赖平台的操作。这是应用需要实现的容器接口。

```litehtml::document_container``` class draws the HTML elements and perform some actions depended of the platform. This is the only class you should implement in your application.

```cpp
class document_container
{
public:
    virtual litehtml::uint_ptr  create_font(const font_description& descr, const document* doc, litehtml::font_metrics* fm) = 0;
    virtual void                delete_font(litehtml::uint_ptr hFont) = 0;
    virtual pixel_t             text_width(const char* text, litehtml::uint_ptr hFont) = 0;
    virtual void                draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) = 0;
    virtual pixel_t             pt_to_px(float pt) const = 0;
    virtual pixel_t             get_default_font_size() const = 0;
    virtual const char*         get_default_font_name() const = 0;
    virtual void                draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) = 0;
    virtual void                load_image(const char* src, const char* baseurl, bool redraw_on_ready) = 0;
    virtual void                get_image_size(const char* src, const char* baseurl, litehtml::size& sz) = 0;
    virtual void                draw_image(litehtml::uint_ptr hdc, const background_layer& layer, const std::string& url, const std::string& base_url) = 0;
    virtual void                draw_solid_fill(litehtml::uint_ptr hdc, const background_layer& layer, const web_color& color) = 0;
    virtual void                draw_linear_gradient(litehtml::uint_ptr hdc, const background_layer& layer, const background_layer::linear_gradient& gradient) = 0;
    virtual void                draw_radial_gradient(litehtml::uint_ptr hdc, const background_layer& layer, const background_layer::radial_gradient& gradient) = 0;
    virtual void                draw_conic_gradient(litehtml::uint_ptr hdc, const background_layer& layer, const background_layer::conic_gradient& gradient) = 0;
    virtual void                draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) = 0;

    virtual void                set_caption(const char* caption) = 0;
    virtual void                set_base_url(const char* base_url) = 0;
    virtual void                link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) = 0;
    virtual void                on_anchor_click(const char* url, const litehtml::element::ptr& el) = 0;
    virtual bool                on_element_click(const litehtml::element::ptr& /*el*/) { return false; };
    virtual void                on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) = 0;
    virtual void                set_cursor(const char* cursor) = 0;
    virtual void                transform_text(std::string& text, litehtml::text_transform tt) = 0;
    virtual void                import_css(std::string& text, const std::string& url, std::string& baseurl) = 0;
    virtual void                set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) = 0;
    virtual void                del_clip() = 0;
    virtual void                get_viewport(litehtml::position& viewport) const = 0;
    virtual litehtml::element::ptr create_element( const char* tag_name,
                                                   const std::string& attributes,
                                                   const std::shared_ptr<litehtml::document>& doc) = 0;

    virtual void                get_media_features(litehtml::media_features& media) const = 0;
    virtual void                get_language(std::string& language, std::string& culture) const = 0;
    virtual std::string    resolve_color(const std::string& /*color*/) const { return std::string(); }
    virtual void                split_text(const char* text, const std::function<void(const char*)>& on_word, const std::function<void(const char*)>& on_space);

protected:
    virtual ~document_container() = default;
};

```

成员函数按用途分类如下：

1. **字体管理**：[create_font](#create_font)、[delete_font](#delete_font)、[text_width](#text_width)、[draw_text](#draw_text)、[pt_to_px](#pt_to_px)、[get_default_font_size](#get_default_font_size)、[get_default_font_name](#get_default_font_name)
2. **绘制**：[draw_list_marker](#draw_list_marker)、[draw_image](#draw_image)、[draw_solid_fill](#draw_solid_fill)、[draw_linear_gradient](#draw_linear_gradient)、[draw_radial_gradient](#draw_radial_gradient)、[draw_conic_gradient](#draw_conic_gradient)、[draw_borders](#draw_borders)
3. **图像处理**：[load_image](#load_image)、[get_image_size](#get_image_size)
4. **HTML 元素交互**：[link](#link)、[on_anchor_click](#on_anchor_click)、[on_element_click](#on_element_click)、[on_mouse_event](#on_mouse_event)
5. **CSS 和样式**：[import_css](#import_css)、[transform_text](#transform_text)、[set_cursor](#set_cursor)
6. **裁剪**：[set_clip](#set_clip)、[del_clip](#del_clip)
7. **视口和媒体**：[get_viewport](#get_viewport)、[get_media_features](#get_media_features)
8. **自定义元素**：[create_element](#create_element)
9. **本地化**：[get_language](#get_language)
10. **其他**：[set_caption](#set_caption)、[set_base_url](#set_base_url)、[resolve_color](#resolve_color)、[split_text](#split_text)

### create_font

```cpp
virtual litehtml::uint_ptr create_font(const font_description& descr, const document* doc, litehtml::font_metrics* fm);
```

litehtml 调用此函数创建字体。返回的 `uint_ptr` 必须能标识创建的字体。`descr` 描述字体名称、大小、字重和装饰等属性。`font_description` 定义如下：

```font_description``` definition:
```cpp
struct font_description
{
    std::string             family;
    int                     size;
    font_style              style;
    int                     weight;
    int                     decoration_line;
    css_length              decoration_thickness;
    text_decoration_style   decoration_style;
    web_color               decoration_color;
    std::string             emphasis_style;
    web_color               emphasis_color;
    int                     emphasis_position;
};
```

- `family`：按 [CSS `font-family` 规范](https://developer.mozilla.org/en-US/docs/Web/CSS/font-family)指定的字体族名称。应用应选择合适的字体。
- `size`：字号，单位为像素。
- `style`：`font_style_normal` 为正常，`font_style_italic` 为斜体，`font_style_oblique` 为倾斜体。
- `weight`：字重，取值 100 到 900。400 对应正常字重，700 对应粗体。参见 [CSS `font-weight`](https://developer.mozilla.org/en-US/docs/Web/CSS/font-weight)。
- `decoration_line`：一个或多个标志。`font_decoration_none=0x00`、`font_decoration_underline=0x01`、`font_decoration_linethrough=0x02`、`font_decoration_overline=0x04` 分别表示无装饰、下划线、删除线和上划线。
- `decoration_thickness`：装饰线粗细，单位为像素。预定义值见枚举 `text_decoration_thickness`。
- `decoration_style`：装饰线样式。
- `decoration_color`：装饰线颜色。
- `emphasis_style`、`emphasis_color`、`emphasis_position`：着重标记的样式、颜色和位置。

输出参数 `litehtml::font_metrics* fm` 需要填入字体度量值：

```cpp
struct font_metrics
{
    int     font_size;
    int     height;
    int     ascent;
    int     descent;
    int     x_height;
    int     ch_width;
    bool    draw_spaces;
    int     sub_shift;
    int     super_shift;
};
```

- `font_size`：字号，单位为像素，与 `create_font` 的输入字号相同。
- `height`：用该字体排相邻两行文本时，基线之间的建议垂直距离。它比 `ascent+descent` 多出的部分称为行间距或外部行距。空间有限时，大多数字体也可以仅用 `ascent+descent` 作为基线间距。
- `ascent`：基线到文本行顶部的距离。
- `descent`：基线到文本行底部的距离。
- `x_height`：字符 `x` 的高度。
- `ch_width`：字符 `0` 的宽度。
- `draw_spaces`：为真时调用绘图函数绘制空格，否则只计入空格宽度。
- `sub_shift`、`super_shift`：下标和上标的基线偏移量。

### delete_font

```cpp
virtual void delete_font(uint_ptr hFont);
```

销毁 [create_font](#create_font) 创建的字体。

### text_width

```cpp
virtual pixel_t text_width(const char* text, uint_ptr hFont);
```

返回文本宽度。

### draw_text

```cpp
virtual void draw_text(uint_ptr hdc, const char* text, uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos);
```

绘制文本字符串。

### pt_to_px

```cpp
virtual pixel_t pt_to_px(float pt);
```

将**点**换算成**像素**。

### get_default_font_size

```cpp
virtual pixel_t get_default_font_size();
```

返回默认字号，单位为像素。

### get_default_font_name

```cpp
virtual const char* get_default_font_name();
```

返回默认字体名称。

### draw_list_marker

```cpp
virtual void draw_list_marker(uint_ptr hdc, const litehtml::list_marker& marker);
```

绘制列表标记。通过 `marker` 获取标记的类型和位置。

### load_image

```cpp
virtual void load_image(const char* src, const char* baseurl, bool redraw_on_ready);
```

可以在此函数中预加载图像。litehtml 不缓存图像；如需缓存，由应用自行实现。

### get_image_size

```cpp
virtual void get_image_size(const char* src, const char* baseurl, litehtml::size& sz);
```

在 `sz` 中填入图像的宽度和高度。

## 背景绘制函数

- [draw_image](#draw_image)：绘制图像背景，例如 `background: no-repeat url("image.png");`。此函数也用于绘制 `<img>` 元素。
- [draw_solid_fill](#draw_solid_fill)：绘制纯色背景，例如 `background-color: #FF0000;`。
- [draw_linear_gradient](#draw_linear_gradient)：绘制线性渐变，例如 `background: linear-gradient(to right, #FF0000, #00FF00);`。
- [draw_radial_gradient](#draw_radial_gradient)：绘制径向渐变，例如 `background: radial-gradient(circle, #FF0000, #00FF00);`。
- [draw_conic_gradient](#draw_conic_gradient)：绘制锥形渐变，例如 `background: conic-gradient(#FF0000, #00FF00);`。

所有背景绘制函数都接收 `background_layer` 参数，用来指定背景的位置和大小：

```cpp
class background_layer
{
public:
    position border_box;
    border_radiuses border_radius;
    position clip_box;
    position origin_box;
    background_attachment attachment;
    background_repeat repeat;
    bool is_root;
}
```

- `border_box`：绘制边界，内容必须绘制在这个矩形内。
- `border_radius`：边框盒的圆角半径。
- `clip_box`：裁剪矩形，作用与 `border_box` 类似。容器必须设置这层额外裁剪。
- `origin_box`：背景定位的参考矩形。
- `attachment`：背景附着方式。`background_attachment_fixed` 表示固定，`background_attachment_scroll` 表示随内容滚动。
- `repeat`：背景重复方式。`background_repeat_no_repeat` 为不重复，`background_repeat_repeat` 为重复，`background_repeat_repeat_x` 为水平重复，`background_repeat_repeat_y` 为垂直重复。
- `is_root`：根元素时为真。容器可据此把背景应用到顶层窗口。

渐变函数接收基于 `background_layer::gradient_base` 的参数：

```cpp
class gradient_base
{
public:
    vector<color_point>  color_points;
    color_space_t        color_space       = color_space_none;
    hue_interpolation_t  hue_interpolation = hue_interpolation_none;
};
```

- `color_points`：渐变色标，每个色标包含颜色和位置。
- `color_space`：渐变所用的色彩空间，参见枚举 `color_space_t`。
- `hue_interpolation`：色相插值方式，参见枚举 `hue_interpolation_t`。

### draw_image

```cpp
virtual void draw_image(litehtml::uint_ptr hdc, const background_layer& layer, const std::string& url, const std::string& base_url);
```

绘制图像。`layer` 描述图像的位置和大小；`url` 和 `base_url` 指定图像来源。

### draw_solid_fill

```cpp
virtual void draw_solid_fill(uint_ptr hdc, const background_layer& layer, const web_color& color);
```

绘制纯色背景。`layer` 指定矩形位置和大小，`color` 指定填充颜色。

### draw_linear_gradient

```cpp
virtual void draw_linear_gradient(uint_ptr hdc, const background_layer& layer, const background_layer::linear_gradient& gradient);
```

在 `layer` 指定的矩形内绘制 `gradient` 指定的线性渐变。`background_layer::linear_gradient` 定义如下：

```cpp
class linear_gradient : public gradient_base
{
public:
    pointF start;
    pointF end;
};
```

- `start`：渐变起点。
- `end`：渐变终点。

### draw_radial_gradient

```cpp
virtual void draw_radial_gradient(uint_ptr hdc, const background_layer& layer, const background_layer::radial_gradient& gradient);
```

在 `layer` 指定的矩形内绘制 `gradient` 指定的径向渐变。`background_layer::radial_gradient` 定义如下：

```cpp
class radial_gradient : public gradient_base
{
public:
    pointF position;
    pointF radius;
};
```

- `position`：渐变中心。
- `radius`：渐变半径。

### draw_conic_gradient

```cpp
virtual void draw_conic_gradient(uint_ptr hdc, const background_layer& layer, const background_layer::conic_gradient& gradient);
```

在 `layer` 指定的矩形内绘制 `gradient` 指定的锥形渐变。`background_layer::conic_gradient` 定义如下：

```cpp
class conic_gradient : public gradient_base
{
public:
    pointF position;
    float  angle;
    float  radius;
};
```

- `position`：渐变中心。
- `angle`：渐变角度，单位为度。正上方为 0 度，顺时针递增。
- `radius`：从中心到背景盒最远角的距离。

### draw_borders

```cpp
virtual void draw_borders(uint_ptr hdc, const css_borders& borders, const litehtml::position& draw_pos, bool root);
```

绘制元素边框。绘制根元素（通常是 `<html>`）的边框时，`root` 为 `true`。

### set_caption

```cpp
virtual void set_caption(const char* caption);
```

litehtml 把 `<title>` 标签中的文本传给此函数。应用可以用 `caption` 设置窗口标题。

### set_base_url

```cpp
virtual void set_base_url(const char* base_url);
```

litehtml 根据 `<base>` 标签调用此函数设置基准 URL。应保存这个字符串，供接收 `baseurl` 的回调使用。

### link

```cpp
virtual void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el);
```

处理 `<link>` 标签。litehtml 会自动处理样式表引用，并调用 `import_css` 加载 CSS 文件。

### on_anchor_click

```cpp
virtual void on_anchor_click(const char* url, litehtml::element::ptr el);
```

点击链接元素时，litehtml 调用此函数。应用可在此打开新页面或执行其他操作。

### on_element_click

```cpp
virtual bool on_element_click(const litehtml::element::ptr& el)
```

点击元素时，litehtml 调用此函数。

### on_mouse_event

```cpp
virtual void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event);
```

发生鼠标事件时，litehtml 调用此函数。`el` 是收到事件的元素，`event` 是事件类型：

- `mouse_event_enter`：鼠标进入元素。
- `mouse_event_leave`：鼠标离开元素。

### set_cursor

```cpp
virtual void set_cursor(const char* cursor);
```

实现此函数以处理 CSS `cursor` 属性。

### transform_text

```cpp
virtual void transform_text(std::string& text, litehtml::text_transform tt);
```

根据 `tt` 转换 `text`：

- `text_transform_capitalize`：首字符大写。
- `text_transform_uppercase`：全部字符转为大写。
- `text_transform_lowercase`：全部字符转为小写。

### import_css

```cpp
virtual void import_css(std::string& text, const std::string& url, std::string& baseurl);
```

litehtml 调用此函数加载样式表。应用应按 `url` 和 `baseurl` 取得 CSS 文件，把内容写入 `text`。

### set_clip

```cpp
virtual void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius);
```

设置绘制裁剪矩形。`bdr_radius` 指定圆角半径。litehtml 可能设置多层裁剪矩形；应用需要保存它们，并在绘制时应用裁剪。

### del_clip

```cpp
virtual void del_clip();
```

移除最后设置的一层裁剪区域。

### get_viewport

```cpp
virtual void get_viewport(litehtml::position& viewport) const;
```

在 `viewport` 中填入视口位置和大小。通常对应绘制 HTML 的窗口客户区。

### create_element

```cpp
virtual litehtml::element::ptr create_element( const char* tag_name, const std::string& attributes, const std::shared_ptr<litehtml::document>& doc);
```

用此函数处理自定义标签。创建自定义 `litehtml::element` 并返回它。`tag_name` 是 HTML 标签名，如 `a`、`p`、`table`。

### get_media_features

```cpp
virtual void get_media_features(litehtml::media_features& media)
```

在 `media` 中填入当前渲染环境的媒体特性。litehtml 根据这些值处理样式表。媒体特性变化时调用 `document::media_changed`，例如窗口大小改变后。`media_features` 定义如下：

```cpp
struct media_features
{
    media_type    type;
    int            width;
    int            height;
    int            device_width;
    int            device_height;
    int            color;
    int            color_index;
    int            monochrome;
    int            resolution;
};
```

- `type`：媒体类型，参见枚举 `media_type`。
- `width`：以像素为单位。连续媒体使用包含可见滚动条在内的视口宽度；分页媒体使用页面盒宽度。
- `height`：输出设备目标显示区的高度，单位为像素。连续媒体使用包含可见滚动条在内的视口高度；分页媒体使用页面盒高度。
- `device_width`、`device_height`：输出设备渲染面的宽度和高度。连续媒体使用屏幕尺寸；分页媒体使用纸张尺寸。
- `color`：输出设备每个颜色分量的位数；非彩色设备填 0。
- `color_index`：设备颜色查找表的条目数；没有查找表时填 0。
- `monochrome`：单色帧缓冲区每像素的位数；非单色设备填 0。
- `resolution`：输出设备分辨率，单位为 DPI。

### get_language

```cpp
virtual void get_language(std::string& language, std::string& culture) const;
```

在 `language` 和 `culture` 中填入用户的语言和区域文化设置，用于处理 HTML 标签的 `lang` 属性。

### resolve_color

```cpp
virtual std::string resolve_color(const std::string& color) const;
```

将颜色名称解析为颜色值，例如把 `red` 解析为 `#FF0000`。不需要额外解析时，返回空字符串即可。标准颜色由 litehtml 本身解析；应用可用此函数解析自定义颜色。

### split_text

```cpp
virtual void split_text(const char* text, const std::function<void(const char*)>& on_word, const std::function<void(const char*)>& on_space);
```

将 `text` 分割成单词和空白。分别对每个单词和空白调用 `on_word` 与 `on_space`，并传入指向该片段的指针。默认实现使用以下条件进行切分：

```cpp
if (c <= ' ' && (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'))
```
