# litehtml 辅助模块

[English](../../en/support/README.en.md)

[support 目录](../../../support)中的代码可以帮助应用集成 litehtml。

## GTKMM4 控件（gtkmm4）

这是用 GTKMM4 编写的 GTK4 控件。以下示例演示如何将控件添加到窗口：

```c++
// Declaration of the widget
html_widget m_html;

// Append widget to the vertical box.
m_vbox.append(m_html);
m_html.set_expand(true);

// Connect to signals
m_html.signal_set_address().connect( sigc::mem_fun(*this, &browser_window::set_address) );
m_html.signal_update_state().connect( sigc::mem_fun(*this, &browser_window::update_buttons) );
signal_close_request().connect(sigc::mem_fun(m_html, &html_widget::on_close), false);

.....

// Set url to the text
void browser_window::set_address(const std::string& text)
{
    m_address_bar.set_text(text);
}

// Update the state of buttons depending of the current state of the widget
void browser_window::update_buttons(uint32_t)
{
	uint32_t state = m_html.get_state();

	if((m_prev_state & page_state_has_back) != (state & page_state_has_back))
	{
		m_back_button.set_sensitive(state & page_state_has_back);
	}
	if((m_prev_state & page_state_has_forward) != (state & page_state_has_forward))
	{
		m_forward_button.set_sensitive(state & page_state_has_forward);
	}
	if((m_prev_state & page_state_downloading) != (state & page_state_downloading))
	{
		if (state & page_state_downloading)
		{
			m_stop_reload_button.set_image_from_icon_name("process-stop-symbolic");
		} else
		{
			m_stop_reload_button.set_image_from_icon_name("view-refresh-symbolic");
		}
	}
	m_prev_state = state;
}

```

调用 `m_html.open_url("http://domain.tld/index")` 即可打开 URL。控件需要 `webpage` 目录中的所有文件。

## 网页实现（webpage）

这组类提供网页处理功能，使用 `container_cairo_pango` 作为容器，通过 libcurl 下载文件，并支持多线程下载。用法可参考 GTKMM4 控件。

### litebrowser::html_host_interface

这个接口用于与控件或浏览器窗口交互。应用需要实现类的所有方法，并将指针传给 `litebrowser::web_page` 构造函数。

#### 方法

```c++
virtual void open_url(const std::string& url);
```

打开 URL，例如用户点击链接时调用。

```c++
virtual void update_cursor();
```

鼠标光标变化时调用。用 `web_page::get_cursor()` 获取光标名称。

```c++
virtual void scroll_to(int x, int y);
```

将页面滚动到 `[x, y]`，例如跳转到 URL 片段 `#fragment` 时使用。

```c++
virtual void get_client_rect(litehtml::position& client) const;
```

通过 `client` 返回控件的客户区矩形，通常使用控件的宽度和高度。

```c++
virtual void redraw_boxes(const litehtml::position::vector& boxes);
```

重绘 `boxes` 中的区域，用来响应移动或点击鼠标引起的变化，例如 `:hover`。

```c++
virtual int get_render_width();
```

返回布局使用的宽度，通常是控件的宽度。

```c++
virtual double get_dpi()
```

返回屏幕 DPI。这个值用于计算字号及 `em` 等单位。应用至少应提供默认值 96。

```c++
virtual int get_screen_width();
virtual int get_screen_height();
```

返回屏幕宽度和高度，供媒体查询使用。

```c++
virtual cairo_surface_t* load_image(const std::string& path);
```

从 `path` 加载图像。

### litebrowser::browser_notify_interface

这个接口类似于 `litebrowser::html_host_interface`，但所有方法都应分派到 GUI 线程。

#### 方法

```c++
virtual void redraw();
```

请求重绘控件。

```c++
virtual void render();
```

请求重新布局控件。

```c++
virtual void update_state();
```

通知网页状态已变化。

```c++
virtual void on_set_caption(const std::string& caption_text);
```

请求更改窗口标题。

```c++
virtual void on_page_loaded(uint64_t web_page_id);
```

通知页面已加载并完成布局。

### litebrowser::web_page

网页辅助类，实现 `container_cairo_pango` 所需的全部方法，并继承 `std::enable_shared_from_this`。

#### 方法

```c++
web_page(html_host_interface* html_host, std::shared_ptr<browser_notify_interface> notify, int pool_size);
```

`web_page` 的构造函数：

- `html_host`：`html_host_interface` 的实现。
- `notify`：指向 `browser_notify_interface` 实现的共享指针。
- `pool_size`：加载网页时使用的线程数。

```c++
uint64_t id() const;
```

返回页面标识符。

```c++
const std::string& get_html_source() const;
```

返回页面的源代码。

```c++
void open(const std::string& url, const std::string& hash);
```

打开页面。`url` 是页面 URL，`hash` 是页面的 `#fragment`。

```c++
void on_mouse_over(int x, int y, int client_x, int client_y);
```

鼠标移动时调用。`x`、`y` 是鼠标指针在文档中的水平和垂直坐标；`client_x`、`client_y` 是在控件中的水平和垂直坐标。

```c++
void on_lbutton_down(int x, int y, int client_x, int client_y);
```

鼠标左键按下时调用。参数坐标与 `on_mouse_over` 相同。

```c++
void on_lbutton_up(int x, int y, int client_x, int client_y);
```

鼠标左键释放时调用。参数坐标与 `on_mouse_over` 相同。

```c++
const std::string& get_cursor() const;
```

返回鼠标光标名称，参见 [CSS `cursor` 属性](https://developer.mozilla.org/en-US/docs/Web/CSS/cursor)。

```c++
int render(int max_width);
```

按指定宽度布局页面。

```c++
const std::string& url() const;
```

返回页面 URL。

```c++
int width() const;
int height() const;
```

返回页面的宽度和高度。

```c++
bool media_changed();
```

将媒体特性的变化应用于网页。需要重新布局时返回 `true`。

```c++
void stop_loading()
```

停止下载页面。

```c++
bool is_downloading()
```

有内容正在下载时返回 `true`。

### litebrowser::draw_buffer

这个辅助类将绘图操作写入 Cairo 表面。应用先绘制到缓冲区，再将缓冲区绘制到控件或窗口上。

缩放规则：

- 所有方法都接收未经缩放的坐标和尺寸，因此传参时不要自行缩放。
- `cairo_surface_t` 的尺寸始终应用缩放。例如，缩放因子为 2.0、缓冲区宽度为 600 时，表面宽度为 1200。
- 缩放因子不为 1 时，缓冲区的 `left` 和 `top` 会舍入到整数坐标。
- 调用 `web_page::on_mouse_over` 等方法时，应使用 `draw_buffer::get_left()` 和 `draw_buffer::get_top()` 获取偏移。

#### 方法

```c++
int get_width() const;
int get_height() const;
```

返回缓冲区的宽度和高度。

```c++
int get_left() const;
int get_top() const;
```

返回缓冲区 `(0, 0)` 对应的坐标。缓冲区相当于视口，其 `left` 和 `top` 表示视口偏移。

```c++
cairo_surface_t* get_cairo_surface() const { return m_draw_buffer; }
```

返回缓冲区的 Cairo 表面。

```c++
double get_scale_factor() const { return m_scale_factor; }
```

返回缓冲区缩放因子。

```c++
void set_scale_factor(std::shared_ptr<litebrowser::web_page> page, double scale)
```

设置缓冲区缩放因子，默认值为 1。

```c++
void on_size_allocate(std::shared_ptr<litebrowser::web_page> page, int width, int height);
```

控件大小变化时调用。此方法重新分配缓冲区，重新布局并重绘页面。

```c++
void on_scroll(std::shared_ptr<litebrowser::web_page> page, int left, int top);
```

滚动到指定位置。

```c++
void redraw_area(std::shared_ptr<litebrowser::web_page> page, int x, int y, int width, int height);
```

重绘指定区域。

```c++
void redraw(std::shared_ptr<litebrowser::web_page> page)
```

重绘全部内容。
