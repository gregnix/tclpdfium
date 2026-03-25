# PDFium Headers

PDFium-Headers von bblanchon/pdfium-binaries:

```bash
wget https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-linux-x64.tgz
tar -xzf pdfium-linux-x64.tgz -C vendor/pdfium
```

Oder symbolischer Link auf bestehende Installation:
```bash
ln -s /opt/pdfium/include vendor/pdfium/include
ln -s /opt/pdfium/lib/libpdfium.so vendor/pdfium/lib/libpdfium.so
```
