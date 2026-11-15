# `document::createFromString` 的输入编码

[English](../../en/guides/document_createFromString.en.md)

```cpp
static document::ptr  document::createFromString(
	const estring&       str,
	document_container*  container,
	const string&        master_styles = litehtml::master_css,
	const string&        user_styles = "");
```

### 术语

**BOM 编码**由字节顺序标记（BOM）指示，可以是 UTF-8、UTF-16LE 或 UTF-16BE。UTF-32 不是有效的 HTML 编码，因此不在此列。参见 `bom_sniff`。

**meta 编码**由有效的 `<meta>` 字符集标签指示。这个标签必须位于 `<head>` 内，格式如下：

- `<meta charset="utf-8">`
- `<meta http-equiv="Content-Type" content="text/html; charset=utf-8">`

其中的编码名称必须是[有效的编码标签](https://encoding.spec.whatwg.org/#names-and-labels)，参见 `get_encoding`。

**HTTP 编码**指 [HTTP `Content-Type` 响应头](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Type)指定的编码。

**用户覆盖编码**指应用允许用户为特定网页或站点手动选择的编码。

### 调用时不指定编码

```cpp
createFromString(string, container);
```

`string` 可以是 `std::string` 或 `char*`：

- 有 BOM 时采用 BOM 编码。
- 否则，有有效的 `<meta>` 标签时采用 meta 编码。
- 两者都没有时采用 UTF-8。

### 指定确定的编码

```cpp
createFromString({string, encoding::big5}, container)
```

- 有 BOM 时采用 BOM 编码。
- 否则采用 Big5。

**注意：**这种用法会忽略 `<meta>` 标签中的编码。

### 指定暂定编码（很少需要）

```cpp
createFromString({string, encoding::big5, confidence::tentative}, container)
```

- 有 BOM 时采用 BOM 编码。
- 否则，有有效的 `<meta>` 标签时采用 meta 编码。
- 两者都没有时采用 Big5。

### 编码优先级

用户覆盖编码和 HTTP 编码都应以 `certain` 传入。如果两者同时存在，用户覆盖编码优先。

如果两者都未指定，应用可以根据上次访问该页面时使用的编码、字符频率分析、URL 域名、用户当前区域设置等信息猜测编码。猜测的编码应以 `tentative` 传入。猜测值的优先顺序见[编码探测算法](https://html.spec.whatwg.org/multipage/parsing.html#encoding-sniffing-algorithm)和 `litehtml::encoding_sniffing_algorithm`。

litehtml 只实现了算法中的以下三步：

- 第 1 步：如果存在 BOM，使用 BOM 指示的编码。
- 第 5 步：当没有 BOM，且用户未指定编码或只给出暂定编码时，预扫描输入以识别编码。
- 第 9 步：当没有 BOM、用户未指定编码且预扫描失败时，返回实现定义的默认编码 UTF-8。

如果应用显示来自网络的 HTML，建议读取 HTTP 编码，因为有些网页只在 HTTP 响应头中声明编码，也有些网页的 meta 编码和 HTTP 编码不一致。后一种情况下，HTTP 编码优先。
