# pdfiumtcl API Reference

Version: 0.6.0

---

## Commands

### pdfium::open

```tcl
pdfium::open filename ?password?
```

Opens a PDF file and returns a document handle.
If the file is encrypted, supply the password.

```tcl
set doc [pdfium::open report.pdf]
set doc [pdfium::open secret.pdf "mypassword"]
```

---

### pdfium::close

```tcl
pdfium::close doc-handle
```

Closes a document and frees resources. Always call after use.

```tcl
pdfium::close $doc
```

---

### pdfium::pagecount

```tcl
pdfium::pagecount doc-handle
```

Returns the number of pages as integer.

```tcl
set n [pdfium::pagecount $doc]
puts "Pages: $n"
```

---

### pdfium::pagesize

```tcl
pdfium::pagesize doc-handle pagenum
```

Returns `{width_mm height_mm}` for the given page (0-based index).

```tcl
lassign [pdfium::pagesize $doc 0] w h
puts "Page size: ${w} x ${h} mm"
```

---

### pdfium::rotation

```tcl
pdfium::rotation doc-handle pagenum
```

Returns the page rotation in degrees: `0`, `90`, `180`, or `270`.

```tcl
set rot [pdfium::rotation $doc 0]
```

---

### pdfium::render

```tcl
pdfium::render doc-handle pagenum ?options?
```

Renders a page as a Tk photo image. Returns the image name.

> **This command needs Tk** (as does `pdfium::addimagebitmap`). Tk is initialized
> lazily on the first `render` call, so under a plain `tclsh` this pulls in Tk
> (and its event loop) — end such scripts with `exit`, or use `wish`.

Options:

| Option | Description |
|--------|-------------|
| `-dpi n` | Render at n DPI (default: 72) |
| `-width px` | Render at exactly px pixels wide (height proportional) |
| `-imagename name` | Use this Tk photo image name |

Use `-width` for label printing (exact pixel width from hardware table).
Use `-dpi` for screen display.

```tcl
# Screen display at 150 DPI
pdfium::render $doc 0 -dpi 150 -imagename pdfpage

# Label printing: exactly 590px wide (54mm QL band)
pdfium::render $doc 0 -width 590 -imagename qlpage
qlpage write /tmp/label.png -format png
```

---

### pdfium::gettext

```tcl
pdfium::gettext doc-handle pagenum
```

Extracts the text content of a page as a UTF-8 string.

```tcl
set text [pdfium::gettext $doc 0]
puts $text
```

---

### pdfium::search

```tcl
pdfium::search doc-handle pagenum searchtext ?-case 0|1?
```

Searches for text on a page. Returns a list of `{startpos count}` pairs
(character positions within the page text).

`-case 1` enables case-sensitive search (default: case-insensitive).

```tcl
set hits [pdfium::search $doc 0 "invoice"]
foreach hit $hits {
    lassign $hit pos count
    puts "Found at position $pos, length $count"
}
```

---

### pdfium::meta

```tcl
pdfium::meta doc-handle key
```

Returns a metadata value. Returns an empty string if not set.

Available keys: `Title` `Author` `Subject` `Keywords`
`Creator` `Producer` `CreationDate` `ModDate`

```tcl
puts [pdfium::meta $doc Title]
puts [pdfium::meta $doc Author]
puts [pdfium::meta $doc CreationDate]
```

---

### pdfium::links

```tcl
pdfium::links doc-handle pagenum
```

Returns a list of web URLs found on the page.

```tcl
set urls [pdfium::links $doc 0]
foreach url $urls {
    puts $url
}
```

---

### pdfium::bookmarks

```tcl
pdfium::bookmarks doc-handle
```

Returns the complete bookmark outline as a list of
`{title pagenum level}` entries. Pages are 0-based.
Level 0 = top-level, level 1 = child, and so on.

```tcl
foreach bm [pdfium::bookmarks $doc] {
    lassign $bm title pagenum level
    set indent [string repeat "  " $level]
    puts "${indent}${title}  (page [expr {$pagenum + 1}])"
}
```

---

### pdfium::formfields

```tcl
pdfium::formfields doc-handle pagenum
```

Returns a list of AcroForm fields on the page.
Each entry is `{type name value}`.

Field types: `text` `button` `choice` `signature` `widget`

```tcl
foreach field [pdfium::formfields $doc 0] {
    lassign $field type name value
    puts "$type  $name  = $value"
}
```

---

### pdfium::annot_list

```tcl
pdfium::annot_list doc-handle pagenum
```

Returns a list of all annotations on the page.
Each entry is `{type rect content author date}`.

- `type` — `text` `link` `freetext` `line` `square` `circle` `polygon`
  `polyline` `highlight` `underline` `squiggly` `strikeout` `stamp`
  `caret` `ink` `popup` `fileattachment` `sound` `movie` `widget`
  `screen` `printermark` `trapnet` `watermark` `threed` `richmedia`
  `xfawidget` `unknown`
- `rect` — `{left bottom right top}` in page coordinates (points)
- `content`, `author`, `date` — strings (may be empty)

```tcl
foreach a [pdfium::annot_list $doc 0] {
    lassign $a type rect content author date
    puts "$type  $rect  $content"
}
```

---

## Writing / Editing (0.4)

Since 0.4 pdfiumtcl can also create and modify PDFs.

> **Units:** `pagesize` returns millimetres, but all writing commands
> below expect **points** (`pt = mm * 72 / 25.4`). The page origin is the
> bottom-left corner.
>
> Encrypted saving and vector/text drawing are intentionally **not**
> provided — use [pdf4tcl](https://sourceforge.net/projects/pdf4tcl/) for
> those. PDFium offers no suitable public write API.

A document/page/object handle is a wide integer, exactly like the
`doc-handle` returned by `pdfium::open`. Pages created with `newpage` must
be closed with `closepage`; documents with `close`.

### pdfium::newdoc

```tcl
pdfium::newdoc
```

Creates an empty document and returns its `doc-handle`.

### pdfium::newpage

```tcl
pdfium::newpage doc-handle index width height
```

Inserts a new blank page at `index` (0-based), `width`/`height` in points.
Returns a `page-handle`.

```tcl
set doc  [pdfium::newdoc]
set page [pdfium::newpage $doc 0 595 842]   ;# A4 in points
```

### pdfium::closepage

```tcl
pdfium::closepage page-handle
```

Releases a page handle obtained from `newpage`.

### pdfium::generatecontent

```tcl
pdfium::generatecontent page-handle
```

Regenerates the page content stream. Call this after adding/changing page
objects (e.g. images) and before `save`. Returns `0|1`.

### pdfium::importpages

```tcl
pdfium::importpages dest-handle src-handle ?pagerange? ?index?
```

Copies pages from `src` into `dest` at `index` (default 0).
`pagerange` is 1-based, e.g. `"1,3,5-7"`; omit it or pass `""` for all
pages. Returns `0|1`. Replaces qpdf for extract/split/merge.

```tcl
# Extract pages 2-4 of in.pdf into a new document
set src [pdfium::open in.pdf]
set out [pdfium::newdoc]
pdfium::importpages $out $src "2-4" 0
pdfium::save $out part.pdf
pdfium::close $out
pdfium::close $src
```

### pdfium::setcropbox / pdfium::setmediabox

```tcl
pdfium::setcropbox  doc-handle pageindex left bottom right top
pdfium::setmediabox doc-handle pageindex left bottom right top
```

Sets the crop/media box of a page (coordinates in points). Returns `1`.
A vector crop preserves the page content; only the visible box changes.

### pdfium::deletepage

```tcl
pdfium::deletepage doc-handle index
```

Removes the page at `index` (0-based). Returns `1`.

### pdfium::setrotation

```tcl
pdfium::setrotation doc-handle index degrees
```

Sets the page rotation. `degrees` must be `0`, `90`, `180` or `270`
(negatives are normalised). Returns `0|1`.

### pdfium::addimagejpeg

```tcl
pdfium::addimagejpeg page-handle doc-handle jpegfile x y w h
```

Embeds a JPEG file as an image object on the page, scaled to `w x h`
points and positioned at `(x,y)` in points. Returns `0|1`.

### pdfium::addimagebitmap

```tcl
pdfium::addimagebitmap page-handle doc-handle photo x y w h
```

Embeds a **Tk photo image** losslessly (no JPEG artifacts), scaled to
`w x h` points at `(x,y)`. Pixels are read via the Tk stub API, so this
works unchanged on Linux, Windows and macOS. Returns `0|1`.

```tcl
set photo [pdfium::render $src 0 -dpi 150]
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

### pdfium::save

```tcl
pdfium::save doc-handle filename ?flags?
```

Writes the document with `FPDF_SaveAsCopy`. `flags` defaults to
`FPDF_NO_INCREMENTAL` (1 — a clean full rewrite). Returns `0|1`.

### pdfium::savewithversion

```tcl
pdfium::savewithversion doc-handle filename version ?flags?
```

Like `save`, but forces the PDF version with `FPDF_SaveWithVersion`.
`version` is an integer such as `14`..`17` (PDF 1.4 .. 1.7). Returns `0|1`.

---

## Errors

`pdfium::open` raises a catchable Tcl error on failure. The error message is
`cannot open PDF '<file>' (PDFium error N)` with `N` from `FPDF_GetLastError`:

| N | Constant | Meaning |
|---|----------|---------|
| 1 | `FPDF_ERR_UNKNOWN`  | Unknown error |
| 2 | `FPDF_ERR_FILE`     | File not found or cannot be opened |
| 3 | `FPDF_ERR_FORMAT`   | Not in PDF format or corrupted |
| 4 | `FPDF_ERR_PASSWORD` | Password required or incorrect |
| 5 | `FPDF_ERR_SECURITY` | Unsupported security scheme |
| 6 | `FPDF_ERR_PAGE`     | Page not found or content error |

Notes:

- PDFium reads **PDF only**. A PostScript/EPS file (starts with `%!PS`, e.g.
  `pcal` output) yields error 3 even with a `.pdf` name — convert it first
  (`ps2pdf in.ps out.pdf`).
- Structurally broken PDFs (bad xref, junk before `%PDF`) also yield error 3;
  rewriting them with `qpdf in.pdf out.pdf` often makes them loadable.
- Encrypted files yield error 4 — re-open with the password argument.

```tcl
if {[catch {pdfium::open $f} doc]} {
    if {[string match {*error 4*} $doc]} {
        set doc [pdfium::open $f $password]
    } else {
        error "open failed: $doc"
    }
}
```

---

## Complete Example

```tcl
package require pdfiumtcl

set doc [pdfium::open document.pdf]

puts "Title:  [pdfium::meta $doc Title]"
puts "Author: [pdfium::meta $doc Author]"
puts "Pages:  [pdfium::pagecount $doc]"

# Page sizes
for {set p 0} {$p < [pdfium::pagecount $doc]} {incr p} {
    lassign [pdfium::pagesize $doc $p] w h
    puts "  Page [expr {$p+1}]: ${w} x ${h} mm"
}

# Bookmarks
foreach bm [pdfium::bookmarks $doc] {
    lassign $bm title pagenum level
    puts "[string repeat {  } $level]${title}  (p.[expr {$pagenum+1}])"
}

# Render page 1 to PNG
pdfium::render $doc 0 -dpi 150 -imagename pg
pg write page1.png -format png

pdfium::close $doc
```

---

## Pixel Widths for Brother QL Label Printers

Hardware-fixed pixel widths for `-width` option (300 DPI):

| Band (mm) | Pixels |
|-----------|--------|
| 12 | 106 |
| 29 | 306 |
| 38 | 413 |
| 50 | 554 |
| **54** | **590** |
| 62 | 696 |
| 102 | 1164 |

Do not calculate `mm / 25.4 * 300` — use the table values.

---

## Printing (Windows only)

PDFium can render into a Windows device context, which makes direct
printing possible without any external program. These commands exist
only on Windows; on other platforms `::pdfium::canprint` returns 0 and
the remaining print commands are not created.

### `::pdfium::canprint`

Returns 1 on Windows builds with print support, 0 elsewhere. Available
on all platforms — use it to branch instead of catching errors.

### `::pdfium::printers`

Returns a list of installed printer names.

### `::pdfium::defaultprinter`

Returns the system default printer name, or raises an error if none is
configured.

### `::pdfium::papers ?printer?`

Returns the forms the driver offers, one sublist per form:

```tcl
{name code width_mm height_mm}
```

Relevant for label printers: Brother QL drivers expose their tapes as
named forms. Use the reported code or name with `-paper`; a free-form
`-paperw`/`-paperh` is often ignored by these drivers.

### `::pdfium::print doc ?options?`

Prints the document, returns the number of pages printed.

| Option | Values | Default |
|--------|--------|---------|
| `-printer` | name | system default |
| `-from` / `-to` | page index, 0-based | whole document |
| `-copies` | integer | 1 |
| `-mode` | 0=EMF … 5=PostScript3 passthrough | 0 |
| `-rotate` | 0, 90, 180, 270 | 0 |
| `-fit` | 1 = scale to printable area, 0 = 1:1 | 1 |
| `-paper` | form name, code, or a4/a5/a3/letter/legal | driver default |
| `-paperw` / `-paperh` | mm, both required | — |
| `-orientation` | portrait, landscape | driver default |
| `-duplex` | off, long, short | driver default |
| `-source` | tray name or DMBIN_* code | driver default |
| `-quality` | draft, low, medium, high, or DPI | driver default |
| `-color` | auto, mono | driver default |
| `-mediatype` | DMMEDIA_* or driver code | driver default |
| `-nup` | pages per sheet: 1, 2, 4, 6, 9, 16 | 1 |
| `-nuporder` | rows, cols | rows |
| `-margin` | mm, all four sides, from the paper edge | 0 |
| `-marginl` `-marginr` `-margint` `-marginb` | mm, override `-margin` | — |
| `-scale` | percent; overrides `-fit` | — |
| `-docname` | text shown in the print queue | "Tcl PDFium Job" |

Notes:

* `-duplex long`/`short` raises an error if the printer does not report
  duplex support, rather than silently printing single-sided.
* `-mode` is a global PDFium setting, not a per-call parameter. If the
  same process also renders to screen, reset it to 0 after printing.
* Copies go through the driver (`dmCopies`, collated) when it reports
  support, otherwise the document is sent repeatedly.
* `-scale` and `-nup` are computed here, not handed to the driver.
  DEVMODE has `dmScale` and `dmNup`, but driver support is unreliable:
  many drivers report the field and then silently reset it, which is
  invisible from the outside. `-color` and `-mediatype` do go through
  DEVMODE -- they describe hardware, not geometry.
* Margins are measured from the paper edge and then clipped to the
  printable area. With `-margin 0` the hardware margin always remains;
  that is not an error.
* With `-nup` greater than 1 each cell gets a 2 mm gutter. `-fit 0`
  aligns to the cell corner rather than centring, so the position stays
  predictable for labels.

### `::pdfium::printercaps ?printer? ?-paper form?`

Returns a dict describing the sheet geometry of the selected form:
`paper_w_mm`, `paper_h_mm`, `print_w_mm`, `print_h_mm`,
`margin_l_mm`, `margin_r_mm`, `margin_t_mm`, `margin_b_mm`,
`borderless`, `dpi_x`, `dpi_y`, `planes`, `bitspixel`, `numcolors`.

This is the way to answer the borderless question. Borderless is not an
option that can be set, it is a property of the driver: on a borderless
form the printable area reaches the sheet or overfills it, so the
margins are zero or negative. Iterate over `::pdfium::papers` and
measure each form to find out whether the driver offers one.

Negative margins mean bleed -- the driver prints past the sheet edge so
that no white stripe remains if the paper runs askew. Two to three
millimetres of image content are lost at the border.

```tcl
if {[::pdfium::canprint]} {
    set doc [::pdfium::open invoice.pdf]
    ::pdfium::print $doc -printer "HP LaserJet" \
        -paper a4 -duplex long -copies 2
    ::pdfium::close $doc
} else {
    exec lp -d $printer invoice.pdf
}
```
