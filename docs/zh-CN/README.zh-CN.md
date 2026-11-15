# litehtml

[English](../en/README.en.md)

## 文档目录

- [快速入门](guides/using.zh-CN.md)
- [`document_container` 回调参考](guides/document_container.zh-CN.md)
- [`document::createFromString` 输入编码](guides/document_createFromString.zh-CN.md)
- [集成辅助模块](support/README.zh-CN.md)
- [测试字体](test-fonts/README.zh-CN.md)和 [Ahem 字体说明](test-fonts/ahem-README.zh-CN.md)
- 修复记录：[动态 DOM 与布局](bugfixes/2026-09-20-dynamic-layout.zh-CN.md)、[样式存储与背景分配](bugfixes/2026-09-20-style-storage.zh-CN.md)
- 性能实验：[根因排查，批次 05](experiments/2026-09-21-litehtml-root-cause-triage.zh-CN.md)

## 什么是 litehtml？

**litehtml** 是一个支持 CSS2/CSS3 的轻量级 HTML 渲染引擎。它不直接绘制文字、图片或其他图形，也不依赖特定的图像、绘图或字体库。你可以自由选择绘图库。**litehtml** 负责解析 HTML 和 CSS，并计算各个 HTML 元素的位置。宿主应用需要实现 [document_container](guides/document_container.zh-CN.md) 回调接口，才能正确绘制这些元素。

## litehtml 可以用在哪里？

**litehtml** 适合显示 HTML 格式的文字，也可以用来制作小型浏览器，但不建议把它当作功能齐全的浏览器引擎。对于简单的 HTML 工具提示或富文本，通常用不上 WebKit 那样完整的引擎；litehtml 更轻量，也更容易嵌入应用。

## HTML 解析器

**litehtml** 使用 [gumbo-parser](https://codeberg.org/gumbo-parser/gumbo-parser) 解析 HTML。Gumbo 用纯 C99 实现 HTML5 解析算法，没有外部依赖，可作为代码检查器、验证器、模板语言、重构和分析工具的基础组件。

## 兼容性

**litehtml** 可用于支持 C++ 和 STL 的平台。原上游说明建议 Windows 使用 MS Visual Studio 2013。**litehtml** 使用 UTF-8 字符串；有关载入 HTML 时指定或探测输入编码的方式，请参见[输入编码说明](guides/document_createFromString.zh-CN.md)。

## HTML 和 CSS 标准支持

**litehtml** 尚未完全兼容 HTML/CSS 标准，与现代浏览器相比仍有差距，但它支持大多数 HTML 标签和 CSS 属性。有关已支持的 CSS 属性，请参见[支持列表](https://docs.google.com/spreadsheet/ccc?key=0AvHXl5n24PuhdHdELUdhaUl4OGlncXhDcDJuM1JpMnc&usp=sharing)。现有功能足以满足大多数简单用途，也能处理一些布局较复杂的页面，例如用 [Bootstrap](http://getbootstrap.com/) 构建的页面通常可以正确排版。

## 测试 litehtml

可以[下载轻量浏览器 litebrowser](http://www.litehtml.com/download.html) 来试用 **litehtml** 渲染引擎。源代码位于 GitHub：

- [Windows](https://github.com/litehtml/litebrowser)
- [Linux](https://github.com/litehtml/litebrowser-linux)
- [Haiku](https://github.com/adamfowleruk/litebrowser-haiku)

## 许可证

**litehtml** 采用 [New BSD 许可证](https://opensource.org/licenses/BSD-3-Clause)。**gumbo-parser** 采用 [Apache 2.0 许可证](http://www.apache.org/licenses/LICENSE-2.0)。

## 链接

- [上游源代码](https://github.com/litehtml/litehtml)
- [官方网站](http://www.litehtml.com/)
