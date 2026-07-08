# tclpdfium

Tcl/Tk binding for PDFium — Google's PDF rendering engine (BSD license).

The first PDFium binding for Tcl. Enables PDF rendering, text extraction,
metadata, search, bookmarks, form fields and annotations directly from
Tcl/Tk — and, since 0.4, creating and editing PDFs (import/merge/split,
delete/rotate pages, crop, embed images, save).

**Version:** 0.5.1  
**License:** BSD  
**Platform:** Linux x86_64, Windows (experimental via MinGW)  
**Tcl/Tk:** 8.5, 8.6, 9.0  

---

## Features

### Reading

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
pdfium::annot_list doc-handle pagenum  -> {{type rect content author date} ...}
```

> **Tk is only needed for `render`.** Since 0.5.1, Tk is loaded lazily: only
> `pdfium::render` (which returns a Tk photo image) pulls it in. Every other
> command is Tk-free, so `package require pdfiumtcl` works in a plain `tclsh`
> without opening a window or entering the Tk event loop.

### Writing / editing (0.4)

```tcl
pdfium::newdoc                                   -> doc-handle (empty)
pdfium::newpage     doc index width height       -> page-handle   (points)
pdfium::closepage   page
pdfium::generatecontent page                     -> 0|1
pdfium::importpages dest src ?range? ?index?     -> 0|1   range "1,3,5-7" or ""
pdfium::setcropbox  doc pageindex l b r t        -> 1     (points)
pdfium::setmediabox doc pageindex l b r t        -> 1     (points)
pdfium::deletepage  doc index                    -> 1
pdfium::setrotation doc index degrees            -> 0|1   0|90|180|270
pdfium::addimagejpeg  page doc jpegfile x y w h  -> 0|1   (points)
pdfium::addimagebitmap page doc photo x y w h    -> 0|1   (points, lossless)
pdfium::save           doc filename ?flags?      -> 0|1   default FPDF_NO_INCREMENTAL
pdfium::savewithversion doc filename version ?flags? -> 0|1   version 14..17
```

> **Units:** `pagesize` returns millimetres, but every writing command
> expects **points** (`pt = mm * 72 / 25.4`). Page origin is bottom-left.
>
> Encrypted saving and vector/text generation are intentionally left to
> [pdf4tcl](https://sourceforge.net/projects/pdf4tcl/) — PDFium has no
> suitable public write API for those.

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

### Writing example — extract a page region into a new PDF

```tcl
package require pdfiumtcl

# Render a page region to a Tk photo, then embed it losslessly.
set src  [pdfium::open in.pdf]
set photo [pdfium::render $src 0 -dpi 150]   ;# -> Tk photo image
pdfium::close $src

# A4-sized point box would be {595 842}; here we make a page the
# image's size in points (150 dpi -> pt factor 72/150).
set k   [expr {72.0 / 150}]
set wpt [expr {[image width  $photo] * $k}]
set hpt [expr {[image height $photo] * $k}]

set doc  [pdfium::newdoc]
set page [pdfium::newpage $doc 0 $wpt $hpt]
pdfium::addimagebitmap $page $doc $photo 0 0 $wpt $hpt
pdfium::generatecontent $page
pdfium::closepage $page
pdfium::save $doc out.pdf
pdfium::close $doc
```

---

## Error handling

`pdfium::open` raises a catchable Tcl error when a file cannot be loaded. The
message is `cannot open PDF '<file>' (PDFium error N)`, where `N` is the PDFium
error code:

| N | Meaning |
|---|---------|
| 1 | Unknown error |
| 2 | File not found / cannot be opened |
| 3 | Not a PDF or corrupted |
| 4 | Password required or incorrect |
| 5 | Unsupported security scheme |
| 6 | Page not found / content error |

PDFium reads **PDF only**. A PostScript/EPS file (`%!PS...`, e.g. `pcal` output)
fails with error 3 even when it has a `.pdf` name — convert it first (e.g.
`ps2pdf in.ps out.pdf`). Wrap calls in `catch` and branch on the code:

```tcl
if {[catch {pdfium::open $f} doc]} {
    if {[string match {*error 4*} $doc]} {
        set doc [pdfium::open $f $password]   ;# encrypted: retry with password
    } else {
        puts stderr $doc                      ;# 2 = missing, 3 = not a PDF, ...
    }
}
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
  app/              Tcl applications (viewer.tcl, viewer2.tcl)
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
- **Windows:** experimental via MinGW/BAWT (see `scripts/build-win.sh`)
- **macOS:** not yet tested
- **Stub-based:** runs without recompiling on any Tcl 8.5+

---

## Changes

### 0.5.1

- **Tk loaded lazily** — only `pdfium::render` initializes Tk now. Loading the
  package no longer opens the `.` window or traps a headless `tclsh` script in
  the event loop.
- **Text correct under Tcl 9** — `gettext`, `meta`, `search`, link URLs and form
  field text now convert UTF-16 ↔ UTF-8 portably (via `utf-16le` encoding)
  instead of relying on `Tcl_UniChar`, which changed width in Tcl 9. Fixes
  garbled output and broken search under Tcl 9.

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
