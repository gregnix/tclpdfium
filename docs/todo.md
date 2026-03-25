# pdfiumtcl — Todo und Roadmap

Stand: 2026-03-25

---

## Erledigt

### Version 0.1
- [x] `pdfium::open` / `pdfium::close`
- [x] `pdfium::pagecount`
- [x] `pdfium::pagesize`
- [x] `pdfium::render` (-dpi, -width)
- [x] `pdfium::gettext`
- [x] Stubs korrekt (kein direkter libtcl/libtk Link)
- [x] `rpath=$ORIGIN` — portabel, libpdfium.so im gleichen Verzeichnis
- [x] Viewer 0.1: Basis-Viewer, Zoom, Passwort-Dialog
- [x] Makefile mit make check

### Version 0.2
- [x] `pdfium::meta` — Metadaten lesen (Titel, Autor, Datum...)
- [x] `pdfium::rotation` — Seitenrotation
- [x] `pdfium::search` — Volltextsuche mit Trefferpositionen
- [x] `pdfium::links` — Web-Links extrahieren
- [x] `pdfium::bookmarks` — Lesezeichen / Outline mit Hierarchie
- [x] `pdfium::formfields` — AcroForm-Felder lesen (Name, Typ, Wert)
- [x] Viewer 0.2: QL-Dialog mit Bandbreiten-Auswahl (29/38/54/62/102mm)
- [x] Viewer 0.2: alle CUPS-Drucker wählbar, Auto-Cut

### Version 0.3
- [x] Viewer 0.3: Infopanel mit drei Tabs (Lesezeichen, Metadaten, Formular)
- [x] Viewer 0.3: Lesezeichen-Navigation (Klick springt zur Seite)
- [x] Viewer 0.3: Metadaten-Anzeige
- [x] Viewer 0.3: Formularfelder-Anzeige (alle Seiten)
- [x] Viewer 0.3: Panel ein/ausblendbar
- [x] Brother QL-820NWB Druckkonfiguration (IPP-Everywhere)
- [x] IPP-Everywhere: MediaType=Roll, CutMedia=EndOfPage
- [x] ppdtool.tcl: PPD-Verwaltung (list/detail/add/delete)
- [x] install-brother-ql820nwb.sh
- [x] brother-ql-tcltk.md (701 Zeilen, 16 Abschnitte)
- [x] feature-matrix.md

---

## Offen

### pdfiumtcl C-Binding

#### Priorität hoch
- [ ] `pdfium::annot_list` — Alle Annotationen einer Seite lesen
  - Typ, Rect, Inhalt, Autor, Datum
  - PDFium: `FPDFPage_GetAnnotCount`, `FPDFAnnot_GetStringValue`
- [ ] `pdfium::layers` — Layer / Optional Content Groups lesen
  - PDFium: `FPDF_GetLayerContext`, `FPDFDoc_GetLayerCount`
- [ ] `pdfium::formfields_set` — Feldwerte programmatisch setzen
  - PDFium: `FPDFAnnot_SetStringValue` für einfache Felder
  - Voraussetzung für interaktives Ausfüllen im Viewer

#### Priorität mittel
- [ ] `pdfium::pagetext_rects` — Textblöcke mit Koordinaten
  - PDFium: `FPDFText_CountRects`, `FPDFText_GetRect`
  - Nützlich für Highlighting und Auswahl
- [ ] `pdfium::thumbnail` — Kleines Vorschaubild
  - Kann bereits mit `pdfium::render -width 120` simuliert werden
  - Echte PDFium-Thumbnails: `FPDFPage_GetDecodedThumbnailData`
- [ ] `pdfium::pagecount_all` — Seitenanzahl ohne vollständiges Öffnen
- [ ] `pdfium::doc_permissions` — Berechtigungen lesen (Drucken, Kopieren...)

#### Priorität niedrig
- [ ] `pdfium::annot_add` — Annotationen hinzufügen
  - PDFium: `FPDFPage_CreateAnnot`, `FPDFAnnot_AppendAttachmentPoints`
- [ ] `pdfium::signature_count` — Anzahl digitaler Signaturen
- [ ] `pdfium::javascript` — JavaScript in PDF lesen

### Viewer

#### Priorität hoch
- [ ] Volltextsuche im Viewer
  - `pdfium::search` ist fertig, nur UI fehlt
  - Suchfeld in Toolbar, Treffer gelb markieren auf Canvas
  - Weiter/Zurück durch Treffer
- [ ] Links klickbar machen
  - `pdfium::links` gibt URLs, PDFium gibt auch Rect-Koordinaten
  - Canvas-Binding: Klick auf Link → `exec xdg-open $url`
- [ ] Seitenrotation anzeigen
  - `pdfium::rotation` ist fertig
  - Canvas-Transform entsprechend der Rotation

#### Priorität mittel
- [ ] Formulare interaktiv ausfüllen
  - Text-Felder: Entry-Widget über Canvas legen
  - Checkboxen: Toggle auf Canvas-Klick
  - Setzt `pdfium::formfields_set` voraus
- [ ] Annotationen anzeigen
  - Kommentare als Tooltips oder Seitenleiste
  - Setzt `pdfium::annot_list` voraus
- [ ] Layer-Panel (4. Tab im Infopanel)
  - Checkboxen zum Ein-/Ausblenden
  - Setzt `pdfium::layers` voraus
- [ ] Thumbnailleiste links
  - Kleine Vorschaubilder aller Seiten
  - Klick springt zur Seite
- [ ] Suchfeld in Toolbar
  - Volltextsuche über alle Seiten

#### Priorität niedrig
- [ ] Letzte Dateien (MRU-Liste)
- [ ] Vollbild-Modus
- [ ] Doppelseiten-Ansicht
- [ ] Druckvorschau mit Skalierung

### Dokumentation
- [ ] pdfiumtcl-api-referenz.md — vollständige API-Dokumentation
  - Alle 12 Befehle mit Parametern, Rückgabewerten, Beispielen
- [ ] pdfiumtcl-installation.md — Kompilierung, Abhängigkeiten, Stubs
- [ ] pdfiumtcl-viewer.md — Viewer-Dokumentation
- [ ] README.md aktualisieren (0.3, neue Befehle)

### GitHub
- [ ] Repository anlegen: gregnix/pdfiumtcl
- [ ] README.md mit Kurzbeschreibung und Beispielen
- [ ] Erstes Release: 0.3
- [ ] Topics: tcl, tk, pdf, pdfium, binding
- [ ] Hinweis: erstes Tcl-PDFium-Binding (bblanchon listet kein Tcl)

---

## Architektur-Notizen

### pdfiumtcl.c aktuell (0.3)

```
Befehle (12):
  open, close, pagecount, pagesize, rotation
  render, gettext, search
  meta, links, bookmarks, formfields

Headers:
  fpdfview.h, fpdf_text.h, fpdf_doc.h
  fpdf_annot.h, fpdf_edit.h

Build:
  gcc -shared -fPIC -DUSE_TCL_STUBS -DUSE_TK_STUBS
  libtclstub8.6.a + libtkstub8.6.a (statisch)
  rpath=$ORIGIN → portabel
```

### Nächste C-Funktion: Annotationen

```c
#include <fpdf_annot.h>

/* pdfium::annot_list doc pagenum */
/* -> {{type rect content author date} ...} */

int n = FPDFPage_GetAnnotCount(page);
for (int i = 0; i < n; i++) {
    FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
    FPDF_ANNOTATION_SUBTYPE sub = FPDFAnnot_GetSubtype(annot);
    /* sub: FPDF_ANNOT_TEXT, FPDF_ANNOT_HIGHLIGHT, FPDF_ANNOT_WIDGET ... */
    FS_RECTF rect;
    FPDFAnnot_GetRect(annot, &rect);
    /* Inhalt: FPDFAnnot_GetStringValue(annot, "Contents", buf, len) */
    FPDFPage_CloseAnnot(annot);
}
```

### Interaktive Formulare — FormHandle nötig

Für `FPDFAnnot_GetFormFieldType` und Feldwerte setzen braucht PDFium
einen `FPDF_FORMHANDLE`. Dieser erfordert FPDF_FORMFILLINFO-Struct
mit Callback-Funktionen. Das ist aufwendiger als die bisherigen
read-only Befehle — eigene Aufgabe für Version 0.4.

---

## Versionsplan

| Version | Fokus | Status |
|---------|-------|--------|
| 0.1 | Render, Text, Pagesize | ✓ fertig |
| 0.2 | Meta, Search, Links, Bookmarks, Forms-read, QL-Dialog | ✓ fertig |
| 0.3 | Viewer Infopanel, QL IPP-Everywhere | ✓ fertig |
| 0.4 | Annotationen lesen, Suche im Viewer, Links klickbar | geplant |
| 0.5 | Formulare ausfüllen, Layer, Thumbnailleiste | geplant |
| 1.0 | GitHub Release, vollständige Dokumentation | geplant |
