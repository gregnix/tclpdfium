# pdfiumtcl API Reference

Version: 0.3

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
