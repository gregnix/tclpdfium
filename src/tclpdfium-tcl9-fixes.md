# tclpdfium -- drei Tcl-8/9-Fixes (auf aktuelle 0.5-Quelle angewandt)

Betrifft nur `src/pdfiumtcl.c`. **Version unveraendert 0.5** (dein aktueller
Stand -- kein Bump, `pkgIndex.tcl`/`PkgProvide` bleiben 0.5). Build unveraendert.
Selbst gegen deine libpdfium kompiliert, auf **Tcl 8.6 und 9.0** verifiziert.

## Fix 1 -- Tk lazy laden (nur render)
`Tk_InitStubs` aus `Pdfiumtcl_Init` entfernt; neuer Helfer `EnsureTk()`, lazy als
erste Zeile in `PdfiumRenderCmd`. -> `package require pdfiumtcl` zieht kein Tk
mehr (kein Fenster, kein tclsh-Event-Loop-Hang); nur `render` laedt Tk.

## Fix 2 -- Text-Ausgabe unter Tcl 9
`gettext`, `meta`, Links-URL, Formfield-Name/-Value nutzten
`Tcl_NewUnicodeObj((Tcl_UniChar*)utf16,n)` -- in Tcl 9 (32-bit Tcl_UniChar)
Mojibake. Jetzt via deinen vorhandenen Helfer `_AnnotUtf16ToObj()`
(`Tcl_ExternalToUtfDString`, utf-16le) + Vorwaerts-Deklaration.

## Fix 3 -- Such-Term unter Tcl 9
`search` wandelte den Begriff per `Tcl_GetUnicodeFromObj` (Tcl 9: 32-bit) und gab
ihn als UTF-16 an PDFium -> Fehltreffer. Jetzt via
`Tcl_UtfToExternalDString(...,utf-16le,...)` + UTF-16-Nullterminator.

## Verifiziert (headless, kein DISPLAY, kein exit; 8.6 + 9.0)
- `package require pdfiumtcl` -> `Tk? 0`, kein Hang, `ldd` ohne `libtk`.
- `meta Title` -> `Rechnung INV-2026-0001`
- `gettext` -> `RECHNUNG Factur-X / ZUGFeRD (EN 16931) Muster GmbH ...`
- `search "Rechnung"` -> `{0 8} {107 8} {163 8} {195 8} {429 8}` (identisch 8.6/9.0)

## Build
Quelle nach `src/pdfiumtcl.c`, dann:
```
make clean
make all86 && make install
make all90 && make install90
make check          # ldd darf KEIN libtk zeigen
```
Keine Versions-Aenderung noetig (bleibt 0.5).
