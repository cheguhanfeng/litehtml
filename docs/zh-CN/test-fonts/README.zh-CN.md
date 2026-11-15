# 测试字体

[English](../../en/test-fonts/README.en.md)

[字体目录](../../../containers/test/fonts)中的字体不会自动加载，需要更新 `RasterFont::installed_fonts` 或 `OutlineFont::installed_fonts`。

## Terminus 字体

尺寸：6x12、8x14、8x16、10x18、10x20、11x22、12x24、14x28、16x32。

- [Terminus 项目主页](https://terminus-font.sourceforge.net)
- [Terminus 4.49.1 源码包](https://sourceforge.net/projects/terminus-font/files/terminus-font-4.49/terminus-font-4.49.1.tar.gz)

`test_container` 把原始 Terminus 粗体当作常规字形使用。`terminus-16px-bold.yaff` 则是在此基础上手工进一步加粗的版本。

使用 [monobit](https://github.com/robhagemans/monobit) 将 BDF 转为 YAFF：

```sh
pip install monobit
monobit-convert ter-u14b.bdf to ter-u14b.yaff
```

转换后手动删除了非 ASCII 字符。

## Ahem 字体

参见 [Ahem 字体说明](ahem-README.zh-CN.md)和 [W3C 的 CSS 测试字体页面](https://www.w3.org/Style/CSS/Test/Fonts)。
