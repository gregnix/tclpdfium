# pdfiumtcl

Tcl/Tk-Binding für PDFium — Googles PDF-Engine (BSD-Lizenz).

Erstes Tcl-Binding für PDFium. Ermöglicht PDF-Rendering, Textextraktion,
Metadaten, Suche, Lesezeichen und Formularfelder direkt aus Tcl/Tk.

## Features (Version 0.3)

```tcl
pdfium::open     filename ?password?   -> doc-handle
pdfium::close    doc-handle
pdfium::pagecount doc-handle           -> integer
pdfium::pagesize  doc-handle pagenum   -> {width_mm height_mm}
pdfium::rotation  doc-handle pagenum   -> 0|90|180|270
pdfium::render    doc-handle pagenum   ?-dpi n? ?-width px?
pdfium::gettext   doc-handle pagenum   -> string
pdfium::search    doc-handle pagenum text ?-case 0|1?
pdfium::meta      doc-handle key       -> string
pdfium::links     doc-handle pagenum   -> {url ...}
pdfium::bookmarks doc-handle           -> {{titel pagenum level} ...}
pdfium::formfields doc-handle pagenum  -> {{type name value} ...}
```

## Verzeichnisstruktur

```
tclpdfium/
  Makefile
  pkgIndex.tcl
  src/            C-Quellcode
  app/            Tcl-Anwendungen (viewer.tcl, ppdtool.tcl)
  scripts/        Shell-Skripte (setup.sh, createpdf.sh)
  examples/       Beispiele
  docs/           Dokumentation
  vendor/pdfium/  PDFium-Bibliothek (siehe Setup)
  pdf/            Test-PDFs
  img/            PNG-Output
```

## Installation

### 1. Abhängigkeiten

```bash
sudo apt install tcl-dev tk-dev build-essential
```

### 2. PDFium einrichten

```bash
bash scripts/setup.sh
```

Lädt `libpdfium.so` und Headers von
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)
nach `vendor/pdfium/`.

### 3. Kompilieren

```bash
make clean && make
make check
```

## Viewer starten

```bash
TCLLIBPATH=. wish app/viewer.tcl mein.pdf
```

## Beispiel

```tcl
package require pdfiumtcl

set doc [pdfium::open dokument.pdf]
puts "Titel:  [pdfium::meta $doc Title]"
puts "Seiten: [pdfium::pagecount $doc]"

foreach bm [pdfium::bookmarks $doc] {
    puts "[lindex $bm 2]-[lindex $bm 0]  (Seite [expr {[lindex $bm 1]+1}])"
}

pdfium::close $doc
```

## Plattform

- Linux x86_64
- Tcl/Tk 8.5+
- Stub-basiert — läuft ohne Neukompilieren auf jedem Tcl 8.5+

## Lizenz

pdfiumtcl: BSD  
PDFium: BSD (Apache CLA)

## Siehe auch

- [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)
- [pdf4tcl](https://sourceforge.net/projects/pdf4tcl/) — PDF erzeugen
- `docs/feature-matrix.md` — Vergleich pdf4tcl / pdfiumtcl / PDF 1.7 / PDF/A
