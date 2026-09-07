# pdfiumtcl API Reference

Version: 0.6.3

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
|

**`-clip {left bottom right top}`** renders only that part of the page,
in **points, page coordinates, origin bottom left** -- the same numbers
`search -rects 1` returns and `pageobjects` reports. A rectangle reaching
past the sheet is cut to it; one entirely outside is an error.

Without this every zoom has to build the whole page. On an A0 drawing
that is the difference between usable and not.

**`-forms 1`** draws form fields as well. Without it they are **missing
from the image**: PDFium does not draw widget annotations with the page
contents but through the form layer (`FPDF_FFLDraw`). Measured on
`tests/fixtures/form.pdf` -- `formfields` reported `Muster GmbH` while the
image showed only the label.

For drawing, an empty `FPDF_FORMFILLINFO` with version 1 is enough: the
callbacks in it are for input, and there is none here. *Filling* a form is
a different matter and needs the whole environment.

`-forms` and `-clip` cannot be combined: `FPDF_FFLDraw` takes no matrix.
Saying so is better than silently drawing the wrong thing.

**`-printing 1`** renders the way a printer would (`FPDF_PRINTING`).
This makes `/Usage /Print /PrintState /OFF` **measurable** instead of
believed: render once with and once without, and compare.

```tcl
pdfium::render $doc 3 -dpi 100 -imagename schirm
pdfium::render $doc 3 -dpi 100 -printing 1 -imagename druck
# measured on pdf4tcl's demo-layers.pdf, page 4:
#   screen 58656 dark pixels, printing 52782 -- the difference is
#   exactly the layer marked -print 0, and nothing else.
```

--------|-------------|
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

### pdfium::pageobjects

```tcl
pdfium::pageobjects doc-handle pagenum ?-marks 0|1? ?-fonts 0|1?
```

What the page is **made of**. Returns one entry per object:

```
{index type {left bottom right top}}
```

`type` is `text`, `path`, `image`, `shading`, `form` or `unknown`; the
rectangle is in points, page coordinates, origin bottom left. An object
without bounds gets an empty list -- a list of zeros would claim it sits
in the corner.

`gettext` says **what** is on the page and `structure` says how it is
tagged. What it is **drawn from** was not available: whether a box is a
path or an image, whether a scan lies behind the text, where a form
XObject sits.

```tcl
set arten [dict create]
foreach e [pdfium::pageobjects $doc 0] {
    dict incr arten [lindex $e 1]
}
puts $arten        ;# e.g. "path 8 text 28"
```

**`-fonts 1`** adds `{name size embedded flags}` for a text object, and
an empty list for anything else -- empty but present, so the caller does
not have to branch on the object type.

Rebuilding a foreign form, this is the answer that matters: which face,
which size. For `readorder` and `overlaps` it is a better basis than the
rectangle alone -- a six-point line and a heading look the same as
rectangles.

**`embedded` is the portability question.** A face that is not embedded
looks different on another machine, and that only shows up *there*:

```
sample.pdf       'Helvetica-Bold' 14.0 pt  embedded=0
demo-tagged.pdf  'BaseFreeSans'    8.0 pt  embedded=1
```

**`-marks 1`** adds a further element: the **names** of the marked-content
marks the object sits in -- `Artifact`, `OC`, `P` and so on.

Only the names. The parameters of an `OC` mark come back as type 0, so
PDFium says *that* an object is in a layer but not which one. The name
alone answers the question this exists for: is this text an **artifact**
-- a running header or page number a screen reader should skip -- or is
it simply untagged? Both carry no MCID, and without the name they cannot
be told apart.

```tcl
foreach e [pdfium::pageobjects $doc 0 -marks 1] {
    lassign $e idx typ box marks
    if {"Artifact" in $marks} { ... }
}
```

---

### pdfium::search

```tcl
pdfium::search doc-handle pagenum searchtext ?-case 0|1? ?-rects 0|1?
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

**`-rects 1` adds the rectangles.** Each hit becomes
`{startpos count {rect ...}}`, and every rect is
`{left bottom right top}` in **points, page coordinates, origin bottom
left** -- the numbers a stamp or a strike-through line is drawn with.

```tcl
foreach hit [pdfium::search $doc 0 "storniert" -rects 1] {
    lassign $hit pos count rects
    foreach r $rects {
        lassign $r left bottom right top
        # ein Strich durch das Wort, auf halber Hoehe
        set y [expr {($bottom + $top) / 2.0}]
        puts "line $left $y $right $y"
    }
}
```

**Why several rectangles per hit:** a match can run across a line break
or sit in more than one text run. PDFium then returns one rectangle per
contiguous piece. Returning only the first would draw the line-break case
silently wrong.

Without this the character position said *that* something is there, not
*where* -- crossing a word out was not possible.

The rectangles ignore `/Rotate`: they are page coordinates as the content
stream uses them. On a rotated page the drawing has to be rotated with
it.

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

### pdfium::structure

```tcl
pdfium::structure doc-handle pagenum
```

Returns the tagged-PDF structure tree of the page, as PDFium sees it.
Empty list if the page has no structure tree — which is the normal case:
most PDFs are not tagged.

Each element is a dict:

| key | |
|---|---|
| `type` | the `/S` role: `P`, `H1`, `Table`, `TH`, `Figure`, ... |
| `title` | `/T`, only present when set |
| `alt` | `/Alt`, the alternative text |
| `actual` | `/ActualText` |
| `lang` | `/Lang` |
| `id` | `/ID` |
| `mcids` | **list** of all marked-content ids of the element |
| `attrs` | dict of attributes (`/Scope`, `/O`, `/ListNumbering`, ...), only present when the element has any |
| `children` | list of child elements, same shape |

`mcids` is a list on purpose. A paragraph that runs across a page break has
two marked-content ids, and PDFium's single-value
`FPDF_StructElement_GetMarkedContentID` returns only one — the rest would
disappear without a word.

```tcl
proc dumpStructure {nodes {indent ""}} {
    foreach node $nodes {
        set line "$indent[dict get $node type]"
        if {[dict exists $node alt]} {
            append line " alt='[dict get $node alt]'"
        }
        if {[llength [dict get $node mcids]]} {
            append line "  mcids: [dict get $node mcids]"
        }
        if {[dict exists $node attrs]} {
            append line "  attrs: [dict get $node attrs]"
        }
        puts $line
        dumpStructure [dict get $node children] "$indent    "
    }
}

dumpStructure [pdfium::structure $doc 0]
```

On a tagged document this prints something like:

```
Document
    H1  mcids: 0
    P  mcids: 1
    P  mcids: 2
        Link alt='pdf4tcl auf GitHub'
            Span  mcids: 3
    H2  mcids: 4
    L  attrs: ListNumbering Decimal O List
        LI
            Lbl  mcids: 5
            LBody  mcids: 6
    ...
    Table
        TR
            TH  mcids: 12  attrs: O Table Scope Column
```

(Copied from an actual run over a pdf4tcl demo, abridged in the middle.)

`examples/structure-dump.tcl` does exactly this and takes a file name.

**Why this exists.** Anyone who writes a structure tree and reads it back
with their own tool confirms themselves by construction. PDFium is the
engine inside Chrome and Edge, so this is a second, independent reading —
and what does not show up here does not reach a reader there either.

> **A measured limit.** PDFium does not appear to follow `/MCR` entries
> that carry `/Stm`, so structure elements whose content lives in a form
> XObject do not show up. A document that veraPDF accepts as PDF/UA can
> therefore come out of this command with elements missing. That is worth
> knowing before you read a short tree as "the document is badly tagged".

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

### Creating annotations

```tcl
pdfium::addannot doc-handle pagenum type {left bottom right top} \
        ?-color {r g b}? ?-opacity 0..1? ?-contents text? ?-author name?
pdfium::delannot doc-handle pagenum index
```

`type` is `highlight`, `underline`, `strikeout`, `squiggly`, `square` or
`text` (the note flag). `addannot` returns the index; `delannot` returns
how many are left.

**The third way.** Marking a word can be done three ways:

| | keeps the original | removable | machine-readable |
|---|---|---|---|
| stamp (tclpdfwriter) | yes | no | no |
| flatten | no | no | no |
| **annotate** | yes | **yes** | **yes** |

For "highlight this word" the third is usually the right one -- a viewer
can click it, hide it, export it. With `search -rects 1` you already have
the coordinates:

```tcl
foreach t [pdfium::search $doc 0 "storniert" -rects 1] {
    lassign $t pos cnt rects
    foreach r $rects {
        pdfium::addannot $doc 0 highlight $r -color {1 1 0} -opacity 0.5
    }
}
pdfium::save $doc out.pdf
```

**No appearance stream from us**, and that is a promise rather than an
omission. Building a correct `/AP` for a highlight means writing
transparency groups and blend modes by hand. Without one, viewers draw
the mark from `/QuadPoints` and `/C` themselves -- and a test measures
that PDFium does, so the promise is not a guess. This is the same gap
`pdf4tcl::fillForms` has, named here up front instead of discovered
later.

Text markup types get `/QuadPoints` automatically: the rectangle alone
is not enough for them (ISO 32000-1 12.5.6.10).

**Call `save` afterwards**, or the work is gone when the document
closes.

---

### pdfium::formfields

```tcl
pdfium::formfields doc-handle pagenum
```

One entry per widget:
`{type name value flags {left bottom right top} options description}`.

`description` is the field's `/TU` -- the text a viewer shows as a
tooltip. A field list can then read "Empfänger, Name und Anschrift"
instead of `f_kunde_2`; on a foreign consignment note that is the
difference between usable and guesswork. It is **empty** when the file
carries none, not the technical name repeated: a duplicate looks like an
explanation and is not one.

`options` lists the permitted values of a combo box or list box, one
`{index label selected}` per entry. For every other kind of field it is
**empty but present** -- a return value whose length depends on the
field type forces every caller into a case distinction.

Until 0.6.3 `formfields` reported `combobox` and kept quiet about which
values were allowed; anyone filling one had to guess. An answer that is
half is worse than one that is missing, because it is taken for
complete.
`type` is `text`, `checkbox`, `radiobutton`, `combobox`, `listbox`,
`pushbutton`, `signature` or `unknown`.

**Read through PDFium's form interface, not out of the annotation
dictionary.** `/T`, `/V` and `/FT` may sit on the parent field and be
inherited by the widget (ISO 32000-1 12.7.3.1). Reading them off the
widget catches the common case -- field and widget in one object -- and
nothing else. Measured on a form with one field and two widgets:

```
before:  {widget {} {}} {widget {} {}}
after:   text kunde {Spedition Muster} 0 {50.0 702.0 250.0 722.0}
         text kunde {Spedition Muster} 0 {50.0 642.0 250.0 662.0}
```

Type, name and value empty, and without any message. Nested names came
back without their parent -- `city` instead of `person.city`.

**The type names changed in 0.6.3.** PDFium tells the kinds apart where
the annotation dictionary only had `/FT`:

```
before:  text  button  choice  signature  widget
now:     text  pushbutton  checkbox  radiobutton  combobox  listbox
         signature  unknown
```

Code that tested for `button` or `choice` will no longer match. The
entry also grew from three elements to five.

**The rectangle is part of the answer**: anyone who wants to click or
highlight a field needs it, and fetching it separately would be a second
route to the same numbers.

For writing there is `pdfium::formfill` below.

---

### pdfium::formfill

```tcl
pdfium::formfill doc-handle pagenum dict
```

Fills text and combo-box fields on a page. Returns how many were filled;
raises an error naming any field it could not find, because "3 of 5
filled" leaves the caller searching.

**Not through `FPDFAnnot_SetStringValue`.** That would write `/V` and
leave the appearance stream -- exactly the gap `pdf4tcl::fillForms` had
until 0.9.4.64: the screen showed the new value and the paper the old
one. This goes the way a viewer goes: focus the field, select all,
replace. **PDFium rebuilds the appearance itself** -- it is the form
engine in Chrome and does nothing else when someone types there.

The proof is measurable: flatten the result **without** `-forms` and the
value is in the page text. Had only `/V` been set, flatten would have
burned in the old, empty stream and the value would be gone.

```tcl
set d [pdfium::open leer.pdf]
pdfium::formfill $d 0 {kunde "Spedition Muster"}
pdfium::save $d voll.pdf
```

**A choice field is chosen from, not written to.** A combo box without
the edit flag cannot be written at all -- `FORM_ReplaceSelection` does
nothing there, and until 0.6.3 `formfill` reported a success anyway
while the value stayed empty. It now navigates with the arrow keys, the
way a viewer does, and matches on the **label**, because that is what
the caller knows. An unknown value is reported with the permitted ones:

```
formfill: could not fill on page 0: {f_artikel (no such option;
allowed: {Artikel A} {Artikel B} {Artikel C} Sonderbestellung)}
```

`editkey` gained `up` and `down` for the same reason.

A field with several widgets -- the carbon set -- is filled by one call:
PDFium joins field and widgets itself.

**A check box or radio button takes a boolean**, not a text -- it has a
state, not a value. It is switched by a **click into the middle of the
field**, the same way a viewer does it, so PDFium keeps `/V`, `/AS` and
the appearance together. Setting `/V` alone would tick the box in the
file and leave it empty on paper. Measured: 0 dark pixels before, 58
after, and they survive flattening.

The current state is read first rather than clicking blindly: a click on
an already ticked box would clear it, and "set to yes" would have done
the opposite.

A **radio button cannot be unset** -- one of a group is always
selected -- so `0` on one is reported rather than quietly failing.

**A list box is chosen from as well** (0.6.3). The branch for it was in
the code all along; the type guard in front of it never let it be
reached -- dead code left behind when choice fields went in, and it
would have worked.

Anything else (push button, signature) is reported as not fillable
rather than skipped: the caller named it and deserves an answer.

A measured difference worth knowing: after moving to the top, a **combo
box** has nothing selected -- the first `down` selects the first entry --
while a **list box** already sits on the first. A fixed rule got this
wrong twice, so the code asks `FPDFAnnot_IsOptionSelected` instead of
counting.

Call `pdfium::save` afterwards, or the work is gone when the document
closes.

---

### Typing into a page

```tcl
set s [pdfium::editbegin $doc $page]
pdfium::editclick $s 100 497      ;# page coordinates, points
pdfium::editchar  $s "Vreden"
pdfium::editkey   $s tab          ;# tab|back|del|left|right|home|end
pdfium::editrender $s -dpi 100 -imagename ::img
pdfium::editend   $s
```

**Draw through `editrender`, not through `render -forms 1`.** The latter
builds its own form environment, which knows nothing of the session --
including whatever has been typed and not yet committed. Measured: three
characters typed during a session left the image at 1406 dark pixels;
only after `editend` were there 1503. You were typing blind, and someone
who cannot see what they write cannot correct it.

**A session, because typing is state**: focus, caret, a half-typed
field. Every other command builds the form environment per call and
tears it down again -- right for drawing, wrong here. The session keeps
environment **and page** open, so the lifetime is in the caller's hands:
whoever calls `editbegin` can see that they owe an `editend`.

**Give up the focus before saving.** PDFium writes a field's content
into the document on focus loss, so `pdfium::save` during an open
session writes the document *without* whatever was typed last. Measured:
the field showed `Muster GmbHVreden` on screen, the saved file contained
`Muster GmbH`. The value was in the session, not in the document -- and
it was visible on screen, which is what makes it treacherous. Call
`editend` (or start a fresh session) before saving.

`editend` gives up the focus first. PDFium commits a field's content on
focus loss, so without it the field you were just working on would be
the one that got lost.

**Typing inserts, it does not replace** -- as on a keyboard. The click
sets the caret and what follows goes in there. To replace, use
`formfill`, which selects all first. Two ways, two names.

`editclick` sends a **mouse move before the press**, the way a viewer
does. Without it PDFium does not know which widget the pointer is over:
in a group of three radio buttons only the first click took effect, and
the caret always landed at the start of a text field instead of where
the click was. In *separate* sessions each option worked -- that was the
hint that it was session state and not the file.

`editclick` returns 1 if a field was hit and 0 otherwise, and only
clicks when there is one: a click into empty space would silently drop
the focus, and the next keystroke would vanish -- which looks like a
broken keyboard.

**`tab` moves to the next field**, which is how a form is actually
filled in: click, type, tab. Without it every field would have to be hit
separately -- on a consignment note with twenty boxes that is the
difference between usable and not.

**One page per session.** Typing across pages would mean holding several
pages open and juggling the focus between them; that would be a second
thing under the same name.

A measured detail worth knowing: **the backspace goes through
`FORM_OnChar`, not `FORM_OnKeyDown`.** Typing "abc" and sending
`FWL_VKEY_Back` as a key left "abc" standing; sending `0x08` as a
character makes it "ab". PDFium treats the backspace as a character, the
arrow keys as keys.

---

### pdfium::signatures

```tcl
pdfium::signatures doc-handle
```

One dict per signature: `index`, `subfilter`, `reason`, `time`,
`docmdp`, `ranges`, `covered`, `size`.

**This is not a verification.** PDFium hands out the parts; it computes
nothing. Whether the signature is valid, whether the certificate is any
good, whether it has been revoked -- none of that is here. Reading
"signatures returned something" as "the document is intact" is wrong in
the dangerous direction.

**What it is still good for:** compare `covered` against the file size.
If the signature covers less than the file contains, something was
appended after signing -- an incremental update. That is not a
verification either, but it is a hint you get without any crypto.

```tcl
foreach s [pdfium::signatures $doc] {
    if {[dict get $s covered] < [file size $f]} {
        puts "changed after signing"
    }
}
```

A trap worth knowing if you extend this: `subfilter` and `time` come
back as **7-bit ASCII**, `reason` as **UTF-16LE** -- three encodings in
one interface. And the length PDFium reports includes the terminating
NUL, so without trimming every ASCII value carries a trailing zero byte
that only shows up when someone compares strings.

---

### pdfium::images

```tcl
pdfium::images doc-handle pagenum
```

One dict per image on the page: `index`, `box`, `width`, `height`,
`dpix`, `dpiy`, `bpp`, `mcid`.

A scan can look fine on screen and come out flat in print -- you notice
when the sheet is already through the machine. `dpix` says it
beforehand:

```
600x400   152.4 x 153.9 dpi
 60x40     15.2 x  15.4 dpi
```

Both images are the **same size on the paper** in that example.

**The figure comes from PDFium, not from arithmetic here.** It sets
pixels against the rectangle on the sheet and takes the object's
transformation matrix into account. An image placed rotated or skewed
has different horizontal and vertical values, and "width divided by
points" would be wrong there -- and wrong without looking wrong.

**No verdict.** What counts as too little depends on the print path: 150
dpi is ample for an office printer and short for a print shop. The tool
names the figure; the decision stays with the reader.

`mcid` is `-1` when the image carries no marked-content ID. Not `0` --
that is a valid mark, and confusing the two would file an untagged image
under the first structure element.

---

### Embedded files (attachments)

```tcl
pdfium::attachments   doc-handle          ;# list of {index name size}
pdfium::attachment    doc-handle index    ;# the contents, as a byte array
pdfium::addattachment doc-handle name data
pdfium::delattachment doc-handle index
```

A ZUGFeRD invoice carries its XML form as an attachment, and a
consignment note can carry supporting documents. `tclpdfreader` reads
them through qpdf -- an external program that may be missing on a
machine. These work natively, and they also **write**.

```tcl
set d [pdfium::open in.pdf]
pdfium::addattachment $d "beleg.xml" [encoding convertto utf-8 $xml]
pdfium::save $d out.pdf
```

**By index, not by name.** Names need not be unique (ISO 32000-1
7.11.4 does not require it). Deleting by name could hit the wrong one,
and nothing would say so. `attachments` reports the index with each
entry.

**The contents are a byte array**, not a string: an attachment is
arbitrary binary material, and a string would send it through the
system encoding -- a PNG or a ZIP would come back changed.

`attachments` reports the size **without fetching the contents**, so
listing twenty attachments does not pull twenty megabytes into memory.

**`delattachment` removes the entry, not the data.** That is PDFium's
behaviour and it is stated in `fpdf_attachment.h`: the bytes stay in the
file until it is rewritten. Do not treat it as a way to get rid of
something confidential.

---

### pdfium::catalog

```tcl
pdfium::catalog doc-handle
```

What the catalogue says about the **whole document**:

| Key | Meaning |
|---|---|
| `tagged` | 1 if a structure tree is present (`/MarkInfo /Marked`) |
| `language` | the `/Lang` entry, `""` if absent |

**Why `tagged` belongs here and not on the page:** asked indirectly via
an empty structure tree, "not tagged" cannot be told apart from "this
one page has nothing in it".

**Why `language` matters:** without `/Lang` a screen reader does not
know which language to read in, and pronounces German text in English.
PDF/UA requires the entry; it is often missing, and the document does
not look any different for it.

**Limit:** `tagged` only says a tree is *there*, not that it is any
good. A document where every paragraph is a `P` is tagged and still
tells a reader nothing.

---

### pdfium::flatten

```tcl
pdfium::flatten doc-handle pagenum ?-mode display|print?
```

Burns annotations and form fields **into the page contents**.

Afterwards they are drawing: no longer clickable, no longer removable --
but also no longer dependent on whether a viewer renders them. That is
what "if you need the annotations burned in" means: a comment or a filled
field that looks the same everywhere.

This is the opposite of overlaying: there the original stays untouched,
here it is changed. Wanting both means overlay first, then flatten.

Returns `flattened` or `nothing` (nothing to burn in -- not an error;
otherwise every batch would stop at the first empty page). A real failure
raises an error, and PDFium gives **no reason** for it; that is stated in
`fpdf_flatten.h` and cannot be improved here.

**`-forms 1` first builds the form layer.** Use it whenever the file was
filled by something that sets `/V` and `/NeedAppearances` without
rebuilding the appearance stream -- `pdf4tcl::fillForms` does exactly
that. Without it `flatten` burns in the **empty** stream and the value is
gone afterwards, which is worse than before:

```
flatten            -> "Auftrag | Kunde:"
flatten -forms 1   -> "Auftrag | Kunde: | Spedition Muster"
```

The form environment generates the missing streams, and only then does
`flatten` have something to take over.

`-mode print` uses `FLAT_PRINT` instead of `FLAT_NORMALDISPLAY`. The
difference matters for annotations meant only for the screen or only for
paper -- the same distinction `/Usage` makes for layers.

**The page is changed in memory only.** Call `pdfium::save` to keep it,
or the work is gone when the document closes.

```tcl
set d [pdfium::open formular.pdf]
pdfium::flatten $d 0        ;# -> flattened
pdfium::save $d flach.pdf
pdfium::close $d
```

---

### pdfium::charboxes

```tcl
pdfium::charboxes doc-handle pagenum ?-range {start count}?
```

One rectangle **per character**: `{char {left bottom right top}}`, points,
page coordinates, origin bottom left.

`gettext` gives the text, `search -rects` the rectangles of whole hits,
`pageobjects` those of an object. The finest step was missing: where does
*this one character* sit. That is what highlighting inside a word,
following a line break, or re-setting text needs.

Without `-range` the whole page, which can be thousands of entries.
`-range {start count}` takes exactly the two numbers `search` returns
without `-rects`, so the two commands fit together:

```tcl
lassign [lindex [pdfium::search $doc 0 "Etikett"] 0] pos cnt
set teil [pdfium::charboxes $doc 0 -range [list $pos $cnt]]
join [lmap e $teil {lindex $e 0}] ""     ;# -> Etikett
```

A range past the end of the page yields an empty list, not an error: a
hit at the page end must not fail because someone counted one too far.

**A trap in the C layer, in case anyone extends this:** `FPDFText_GetCharBox`
hands out *left, right, bottom, top* -- a different order from
`FPDFText_GetRect`, which uses *left, top, right, bottom*. Treating them
alike swaps edges. Everything leaves this package as
`{left bottom right top}`.

---

### pdfium::mctext

```
pdfium::mctext doc-handle pagenum
```

The text of a page grouped by marked-content ID, as a flat dict:

```
mcid1 text1 mcid2 text2 ...
```

The entries come in the order the objects sit in the **content stream**,
which is the point: `pdfium::structure` gives the order of the
**structure tree**, and comparing the two is the only way to see whether
a document reads the way it is tagged.

Text objects without a marked-content ID land under the key `-1`. In a
tagged document that key should not appear -- it is content a screen
reader cannot reach.

```tcl
set d [pdfium::open tagged.pdf]

# MCIDs in the order of the structure tree
proc walkMc {el varName} {
    upvar 1 $varName out
    foreach m [dict get $el mcids] { lappend out $m }
    foreach k [dict get $el children] { walkMc $k out }
}
set tree {}
foreach el [pdfium::structure $d 0] { walkMc $el tree }

# MCIDs in the order of the content stream
set stream {}
foreach {id txt} [pdfium::mctext $d 0] {
    if {$id >= 0} { lappend stream $id }
}

# An element may own an MCID without carrying text of its own -- a Table
# does. Compare only those that do.
set treeWithText {}
foreach m $tree { if {$m in $stream} { lappend treeWithText $m } }

puts [expr {$treeWithText eq $stream ? "reading order ok" : "MISMATCH"}]
pdfium::close $d
```

Measured output on a tagged table:

```
  MCID 0: Bestellpositionen
  MCID 2: Artikel
  MCID 3: Menge
  MCID 4: Einzelpreis
  MCID 5: Betrag
  MCID 7: Schraube M6
```

No validator checks the reading order. veraPDF answers whether a
document is conformant, and a conformant file can still be tagged in one
order and drawn in another -- which is exactly what a screen reader
stumbles over.
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


**`-boxes 1`** adds the rectangle of each text object and returns a
**different shape**: a list of `{mcid text {left bottom right top}}`
triples.

Without the option the flat alternating list stays, which reads like a
dict. Appending a third element there would silently turn it into
something else -- `dict get` on an odd list is an error, and one that
surfaces at the caller.

The rectangle is that of the **text object**, not of a character; for
characters there is `charboxes`.

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
