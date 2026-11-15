# 如何使用 litehtml

[English](../../en/guides/using.en.md)

1. 实现 [litehtml::document_container](document_container.zh-CN.md) 类。
2. 调用 `litehtml::document` 的方法解析 HTML、计算布局并进行绘制。

## 实现 `litehtml::document_container`

将 litehtml 集成到应用时，先实现抽象类 [litehtml::document_container](document_container.zh-CN.md)。它负责绘制 HTML 元素和执行依赖于平台的操作。

## 加载 HTML 页面

调用 `litehtml::document::createFromString` 从 HTML 字符串创建 `litehtml::document` 对象。输入通常采用 UTF-8。函数签名如下：

```cpp
static document::ptr  document::createFromString(
	const estring&       str,
	document_container*  container,
	const string&        master_styles = litehtml::master_css,
	const string&        user_styles = "");
```

关于显式指定输入编码和自动探测编码，请参见 [`document::createFromString` 的输入编码](document_createFromString.zh-CN.md)。

## 自行构建元素树

如果应用已有自己的 HTML 树，例如由其他解析器生成的树，或者完全不基于 HTML 的界面树，可以直接构建 litehtml 元素树，省去序列化成 HTML 字符串再用 `createFromString` 解析的步骤：

```cpp
auto doc = std::make_shared<litehtml::document>(container);
// must be done before any element is created
doc->set_document_mode(litehtml::no_quirks_mode);

auto html = doc->create_element("html", {});
auto body = doc->create_element("body", {});
html->appendChild(body);

auto div = doc->create_element("div", {{"class", "hello"}});
body->appendChild(div);
container->split_text("Hello world",
    [&](const char* word)  { div->appendChild(std::make_shared<litehtml::el_text>(word, doc)); },
    [&](const char* space) { div->appendChild(std::make_shared<litehtml::el_space>(space, doc)); });

doc->finalize_from_external_root(html);
```

`finalize_from_external_root` 接收与 `createFromString` 相同的 `master_styles` 和 `user_styles`，并执行相同的收尾工作：应用样式表、计算样式、建立渲染树。之后可像使用解析得到的文档一样调用 `render`、`draw` 等方法。

元素树完整后，只需执行一次初始化收尾。收尾会累积已解析的样式表、媒体列表、表格元素、渲染树和各元素用到的选择器。再次调用会造成状态重复，因此会被忽略。需要重建时，请新建一个文档。

`createFromString` 返回 `litehtml::document` 指针。调用 `litehtml::document::render(max_width)` 来计算 HTML 元素的布局：

```cpp
m_doc->render(max_width);
```

`max_width` 一般是浏览器窗口宽度。`render` 还返回 HTML 内容的合适宽度，可以把这个返回值用于再次布局，例如为工具提示确定宽度。

使用 `litehtml::document` 的 `height` 和 `width` 获取布局后的文档高度和宽度：

```cpp
m_doc->height()
m_doc->width()
```

然后调用 `draw` 绘制页面：

```cpp
m_doc->draw(hdc, m_left, m_top, &clip);
```

`draw` 接收四个参数：

- `uint_ptr hdc`：绘图所用的设备上下文或类似目标，例如 Windows 的 `HDC`。它会传给 [litehtml::document_container](document_container.zh-CN.md) 的所有绘图方法。
- `int x, int y`：绘制 HTML 的起点。
- `const position* clip`：裁剪区域。litehtml 会跳过与裁剪矩形不相交的元素。需要严格裁剪 HTML 内容时，宿主仍需自行设置裁剪。

## 按文档内容调整宽度

如果绘制 HTML 的窗口宽度不固定，可以利用 `document::render` 返回的合适宽度：

```cpp
pixel_t best_width = m_doc->render(max_width);
if(best_width < max_width)
{
	m_doc->render(best_width);
}
```

先用允许的最大宽度计算布局。如果返回的宽度更小，就用该宽度再次布局。然后通过 `document::width` 获取实际文档宽度。这适用于 HTML 工具提示和 HTML 控件。

## 滚动与裁剪

通过 `document::draw` 的 `x` 和 `y` 参数实现整个文档的滚动。例如，向右滚动 50 像素、向下滚动 100 像素：

```cpp
m_doc->draw(hdc, -50, -100, litehtml::position(50, 100, window_width, window_height));
```

`x` 和 `y` 使用相对于屏幕的坐标，`clip` 则使用相对于文档的坐标。litehtml 只检查元素的包围矩形是否与裁剪矩形相交，因此某些元素仍可能绘制到裁剪区域之外。实际裁剪由宿主实现。

## 滚动内部元素

如果元素设置了 `overflow: auto` 或 `overflow: scroll`，可以调用 `document::on_scroll` 滚动其内容：

```cpp
std::vector<scroll_values> on_scroll(pixel_t dx, pixel_t dy, pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y) const;
```

- `dx, dy`：请求的水平和垂直滚动量，单位为像素。
- `x, y`：鼠标指针的文档坐标。
- `client_x, client_y`：鼠标指针的窗口或视口坐标，用于处理固定定位元素。

发生滚动时，函数返回非空的 `scroll_values` 数组。该结构定义如下：

```cpp
struct scroll_values
{
    pixel_t	dx;
    pixel_t dy;
    position scroll_box;
};
```

- `dx, dy`：实际水平和垂直滚动量，可能与同名输入参数不同。
- `scroll_box`：已滚动的盒子的位置。

## 处理鼠标

调用 `litehtml::document` 的鼠标事件函数：

```cpp
bool litehtml::document::on_mouse_over( pixel_t x,
                                        pixel_t y,
                                        pixel_t client_x,
                                        pixel_t client_y,
                                        const std::function<void(const litehtml::position&)>& redraw_box );
bool litehtml::document::on_mouse_leave( pixel_t x,
                                         pixel_t y,
                                         pixel_t client_x,
                                         pixel_t client_y,
                                         const std::function<void(const litehtml::position&)>& redraw_box );
bool litehtml::document::on_lbutton_down( pixel_t x,
                                         pixel_t y,
                                         pixel_t client_x,
                                         pixel_t client_y,
                                         const std::function<void(const litehtml::position&)>& redraw_box );
bool litehtml::document::on_lbutton_up( pixel_t x,
                                        pixel_t y,
                                        pixel_t client_x,
                                        pixel_t client_y,
                                        const std::function<void(const litehtml::position&)>& redraw_box );
bool litehtml::document::on_mouse_leave( pixel_t x,
                                         pixel_t y,
                                         pixel_t client_x,
                                         pixel_t client_y,
                                         const std::function<void(const litehtml::position&)>& redraw_box );
```

这些函数都接收 `redraw_box` 回调。需要重绘时，函数会把待重绘矩形传给回调；如果有多个矩形，就会调用多次。应先记录这些矩形，等事件函数执行完再实际重绘。

函数返回的 `bool` 表示是否需要重绘。`x` 和 `y` 相对于 HTML 布局，`0,0` 是左上角。`client_x` 和 `client_y` 是鼠标在客户区（绘制区域）中的位置，用来处理固定定位元素。

## 处理链接点击

处理鼠标事件后，点击链接会触发 [document_container::on_anchor_click](document_container.zh-CN.md#on_anchor_click)。这个回调会收到链接 URL 和 `litehtml::element` 指针，可以用来发起页面跳转。

**注意：**不要在 [document_container::on_anchor_click](document_container.zh-CN.md#on_anchor_click) 中销毁当前文档，否则会崩溃。回调由 `document::on_lbutton_up` 触发，应等该函数结束后再销毁旧文档并显示新文档。

## 处理命名锚点

litehtml 不会自动跳转到命名锚点。可以在 [document_container::on_anchor_click](document_container.zh-CN.md#on_anchor_click) 中取得 URL，查找 `#` 并提取后面的锚点名称。下例中 `anchor_name` 存放锚点名称：

```cpp
if(anchor_name.empty())
{
    std::string selector = "#" + anchor_name;
    litehtml::element::ptr el = m_html->root()->select_one(selector);
    if (!el)
    {
        selector = "[name=" + anchor_name + "]";
        el = m_html->root()->select_one(selector);
    }
    if (el)
    {
        litehtml::position pos = el->get_placement();
        m_html_host->scroll_to(0, pos.top());
    }
}
```

`element::select_one` 返回与 CSS 选择器匹配的第一个元素。例如，`select_one("#name")` 会返回 `id="name"` 的元素。`el->get_placement()` 返回元素在文档坐标中的绝对位置，可以据此滚动文档。

## 图像

litehtml 不缓存图像。如果性能需要，可以自行实现图像缓存，也可以延迟加载。可在 [document_container::load_image](document_container.zh-CN.md#load_image) 中开始下载图像；图像未就绪时先返回占位尺寸并绘制占位图，载入完成后调用 `litehtml::document::render` 并重绘页面。

## CSS 媒体特性

litehtml 支持 CSS `@media` 规则，以及 `<link>` 和 `<style>` 标签的 `media` 属性。应用需要：

1. 实现 [document_container::get_media_features](document_container.zh-CN.md#get_media_features)，用宽度、高度等实际媒体特性填写 `media`。
2. 当媒体特性变化时调用 `document::media_changed`，例如窗口尺寸改变后。
