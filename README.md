# tclpdfium

Tcl/Tk binding for PDFium — Google's PDF rendering engine (BSD license).

The first PDFium binding for Tcl. Enables PDF rendering, text extraction,
metadata, search, bookmarks, and form fields directly from Tcl/Tk.

**Version:** 0.3  
**License:** BSD  
**Platform:** Linux x86_64, Windows (experimental via MinGW)  
**Tcl/Tk:** 8.5, 8.6, 9.0  

---

## Features

```tcl
pdfium::open      filename ?password?  -> doc-handle
pdfium::close     doc-handle
pdfium::pagecount doc-handle           -> integer
pdfium::pagesize  doc-handle pagenum   -> {width_mm height_mm}
pdfium::rotation  doc-handle pagenum   -> 0|90|180|270
pdfium::render    doc-handle pagenum   ?-dpi n? ?-width px?
pdfium::gettext   doc-handle pagenum   -> string
pdfium::search    doc-handle pagenum text ?-case 0|1?
pdfium::meta      doc-handle key       -> string
pdfium::links     doc-handle pagenum   -> {url ...}
pdfium::bookmarks doc-handle           -> {{title pagenum level} ...}
pdfium::formfields doc-handle pagenum  -> {{type name value} ...}
```

---

## Installation

### 1. Dependencies

```bash
sudo apt install tcl-dev tk-dev build-essential
```

### 2. Set up PDFium

```bash
bash scripts/setup.sh
```

Downloads `libpdfium.so` and headers from
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)
into `vendor/pdfium/`.

### 3. Build

```bash
make clean && make
make check
```

---

## Quick Start

```tcl
package require pdfiumtcl

set doc [pdfium::open document.pdf]
puts "Title:  [pdfium::meta $doc Title]"
puts "Pages:  [pdfium::pagecount $doc]"

# Bookmarks
foreach bm [pdfium::bookmarks $doc] {
    puts "[lindex $bm 2]-[lindex $bm 0]  (page [expr {[lindex $bm 1]+1}])"
}

# Render page 0 at 150 dpi -> Tk photo image
set img [pdfium::render $doc 0 -dpi 150]
label .l -image $img
pack .l

pdfium::close $doc
```

---

## Viewer

```bash
TCLLIBPATH=. wish app/viewer.tcl document.pdf
```

---

## Directory Structure

```
tclpdfium/
  Makefile
  pkgIndex.tcl
  src/              C source code (pdfiumtcl.c)
  app/              Tcl applications (viewer.tcl, ppdtool.tcl)
  scripts/          Shell scripts (setup.sh, createpdf.sh)
  examples/         Example scripts
  docs/             Documentation
    en/             API reference, getting started
  vendor/pdfium/    PDFium library (downloaded by setup.sh)
    include/        PDFium headers
    lib/            libpdfium.so / pdfium.dll (not in repo)
    licenses/       Third-party licenses
```

---

## Platform Notes

- **Linux x86_64:** fully supported, tested on Tcl 8.6 and 9.0
- **Windows:** experimental via MinGW/BAWT (see `docs/en/windows-build.md`)
- **macOS:** not yet tested
- **Stub-based:** runs without recompiling on any Tcl 8.5+

---

## See Also

- [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) — prebuilt PDFium
- [pdf4tcl](https://sourceforge.net/projects/pdf4tcl/) — create PDFs
- [tkmcairo](https://github.com/gregnix/tkmcairo) — Cairo 2D graphics for Tcl
- `docs/en/api-reference.md` — full API documentation

---

## License

tclpdfium: BSD  
PDFium: BSD (Apache CLA)  
Third-party licenses: see `vendor/pdfium/licenses/`
