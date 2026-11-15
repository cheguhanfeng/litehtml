# Test Fonts

[简体中文](../../zh-CN/test-fonts/README.zh-CN.md)

Fonts in the [test fonts directory](../../../containers/test/fonts) are not picked up automatically. Update `RasterFont::installed_fonts` or `OutlineFont::installed_fonts`.

## Terminus Font

Sizes: 6x12, 8x14, 8x16, 10x18, 10x20, 11x22, 12x24, 14x28, 16x32.

- [Terminus homepage](https://terminus-font.sourceforge.net)
- [Terminus 4.49.1 source archive](https://sourceforge.net/projects/terminus-font/files/terminus-font-4.49/terminus-font-4.49.1.tar.gz)

The `test_container` uses the original Terminus bold face as a regular face. `terminus-16px-bold.yaff` was further boldened by hand.

To convert BDF to YAFF with [monobit](https://github.com/robhagemans/monobit):

```sh
pip install monobit
monobit-convert ter-u14b.bdf to ter-u14b.yaff
```

Non-ASCII characters were then removed manually.

## Ahem Font

See the [Ahem font notes](ahem-README.en.md) and the [W3C CSS test fonts page](https://www.w3.org/Style/CSS/Test/Fonts).
