# litehtml

[简体中文](../zh-CN/README.zh-CN.md)

## Documentation

- [Getting started](guides/using.en.md)
- [`document_container` callback reference](guides/document_container.en.md)
- [`document::createFromString` input encoding](guides/document_createFromString.en.md)
- [Integration helpers](support/README.en.md)
- [Test fonts](test-fonts/README.en.md) and [Ahem font notes](test-fonts/ahem-README.en.md)
- Bug fixes: [dynamic DOM and layout](bugfixes/2026-09-20-dynamic-layout.en.md), [style storage and background allocations](bugfixes/2026-09-20-style-storage.en.md)
- Performance experiments: [root cause investigation, batch 05](experiments/2026-09-21-litehtml-root-cause-triage.en.md)

## What is litehtml?

**litehtml** is a lightweight HTML rendering engine with CSS2/CSS3 support. It parses HTML and CSS and computes the positions of HTML elements. It does not draw text, images, or graphics itself and has no dependency on a drawing, image, or font library. Your application implements the [document_container](guides/document_container.en.md) callback interface and can use any drawing library.

## Where litehtml can be used

Use **litehtml** to display HTML-formatted text or build a small browser. It is easier to embed than a full browser engine such as WebKit for uses like simple HTML tooltips, where complete browser behavior is unnecessary.

## HTML Parser

**litehtml** uses the [gumbo-parser](https://codeberg.org/gumbo-parser/gumbo-parser) to parse HTML. Gumbo is an implementation of the HTML5 parsing algorithm implemented as a pure C99 library with no outside dependencies. It's designed to serve as a building block for other tools and libraries such as linters, validators, templating languages, and refactoring and analysis tools.

## Compatibility

**litehtml** can run on platforms that support C++ and the STL. The original upstream documentation recommended MS Visual Studio 2013 for Windows. **litehtml** uses UTF-8 strings; see [input encoding](guides/document_createFromString.en.md) for how to specify or detect the encoding when loading HTML.

## Support for HTML and CSS standards

Unfortunately **litehtml** is not fully compatible with HTML/CSS standards. There is lots of work to do to make **litehtml** work as well as modern browsers. But **litehtml** supports most HTML tags and CSS properties. You can find the list of supported CSS properties in  [this table](https://docs.google.com/spreadsheet/ccc?key=0AvHXl5n24PuhdHdELUdhaUl4OGlncXhDcDJuM1JpMnc&usp=sharing). For most simple usecases the HTML/CSS features supported by **litehtml** are enough. Right now **litehtml** supports even some pages with very complex HTML/CSS designs. As an example the pages created with [bootstrap framework](http://getbootstrap.com/) are usually well formatted by **litehtml**.

## Testing litehtml

You can [download the simple browser](http://www.litehtml.com/download.html) (**litebrowser**) to test the **litehtml** rendering engine.

The litebrowser source codes are available on GitHub:
  * [For Windows](https://github.com/litehtml/litebrowser)
  * [For Linux](https://github.com/litehtml/litebrowser-linux)
  * [For Haiku](https://github.com/adamfowleruk/litebrowser-haiku)

## License

**litehtml** is distributed under [New BSD License](https://opensource.org/licenses/BSD-3-Clause).
The **gumbo-parser** is distributed under [Apache License, Version 2.0](http://www.apache.org/licenses/LICENSE-2.0)

## Links

  * [source code](https://github.com/litehtml/litehtml)
  * [website](http://www.litehtml.com/)
