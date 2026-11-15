# Content box percentages and inline control geometry / 内容盒百分比与内联控件尺寸

## English

A `height:100%` child of a `box-sizing:border-box` block could extend below the parent's content box. The CEF fixture used a 54 px parent with 9 px padding and 4 px borders. `render_item_block` resolved descendant percentages against 54 px, while the parent itself removed 26 px when assigning its final height. The block now gives children the content height on both layout passes. The child measures 84×28 px in the regression fixture.

An unstyled canvas, progress element, or meter had inline layout, so supplying a size did not yield a drawable box. This also discarded Canvas image commands in the host even after the image loaded. Their default display is now inline block. Progress and meter get intrinsic fallback dimensions from the default stylesheet. The stylesheet owns the default horizontal rule border and basic native input geometry.

Release CTest exercises the percentage child, inline canvas, progress, and meter through the C ABI. The Demo CEF audit verifies the packaged host and provides the before/after screenshots.

## 简体中文

父块指定 `box-sizing:border-box` 后，`height:100%` 子元素可能超出父块内容盒。CEF 样例的父块高 54 px，上下各有 9 px 内边距和 4 px 边框。`render_item_block` 给子元素传的是 54 px，父块给自身计算尺寸时却减去了 26 px。两次子布局现在都使用内容盒高度。回归样例中的子元素为 84×28 px。

未设置 `display` 的 Canvas、进度条和仪表走内联布局，即使提供尺寸，也得不到有效绘制盒。宿主因此丢掉了已经成功加载的 Canvas 图片命令。现在三类元素默认采用 `inline-block`，进度条和仪表的缺省尺寸由默认样式表提供。水平线的默认边框和基本输入控件的尺寸也由样式表负责。

Release CTest 通过 C ABI 验证百分比高度、内联 Canvas、进度条和仪表。Demo 的 CEF 报告验证了打包宿主，并保留修复前后截图。
