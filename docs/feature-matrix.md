# pdfiumtcl — Feature-Matrix

Stand: 2026-03-25 — pdfiumtcl 0.3, pdf4tcl 0.9.4.16

Legende: ✓ implementiert · ~ teilweise / eingeschränkt · ✕ fehlt

## Rendering und Anzeige

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| Seiten rendern | ✕ | ✓ | ✓ | ✓ | ✓ |
| Zoom / DPI | ✕ | ✓ | ✓ | ✓ | ✓ |
| Seitenrotation lesen | ✕ | ✓ | ~ nicht angezeigt | ✓ | ✓ |
| Layer / Optional Content | ✕ | ✕ | ✕ | ✓ | ~ |

## Text und Suche

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| Text extrahieren | ✕ | ✓ | ✓ | ✓ | ✓ |
| Volltextsuche | ✕ | ✓ | ✕ geplant | ✓ | ✓ |
| Text schreiben / erzeugen | ✓ | ✕ | ✕ | ✓ | ✓ |

## Metadaten und Struktur

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| Metadaten lesen | ✕ | ✓ | ✓ | ✓ | ✓ |
| Metadaten schreiben | ✓ | ✕ | ✕ | ✓ | ✓ |
| Lesezeichen lesen | ✕ | ✓ | ✓ | ✓ | ✓ |
| Lesezeichen schreiben | ✓ | ✕ | ✕ | ✓ | ✓ |
| Links lesen | ✕ | ✓ | ~ nicht klickbar | ✓ | ✓ |

## Formulare (AcroForm)

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| Felder lesen | ✕ | ✓ | ✓ | ✓ | ~ |
| Felder erzeugen (8 Typen) | ✓ | ✕ | ✕ | ✓ | ~ |
| Felder interaktiv ausfüllen | ✕ | ✕ geplant | ✕ geplant | ✓ | ~ |

## Annotationen

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| Annotationen lesen | ✕ | ~ Widget-Typ | ✕ geplant | ✓ | ✓ |
| Annotationen erzeugen | ✕ | ✕ geplant | ✕ geplant | ✓ | ✓ |

## Grafik und Farben

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| Vektorgrafik erzeugen | ✓ | ✕ | ✕ | ✓ | ✓ |
| Bilder einbetten | ✓ | ✕ | ✕ | ✓ | ✓ |
| Gradienten | ✓ | ✕ | ✕ | ✓ | ✕ |

## Verschlüsselung und Sicherheit

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| Passwortschutz erzeugen (AES-256) | ✓ | ✕ | ✕ | ✓ | ✕ |
| Verschlüsselte PDF öffnen | ✕ | ✓ | ✓ | ✓ | ✕ |

## Standards und Konformität

| Feature | pdf4tcl | pdfiumtcl | Viewer | PDF 1.7 | PDF/A |
|---------|---------|-----------|--------|---------|-------|
| PDF/A-1b erzeugen | ✓ | ✕ | ✕ | ~ | ✓ |
| Eingebettete Dateien (ZUGFeRD) | ✓ | ✕ | ✕ | ✓ | ✓ PDF/A-3 |
| Drucken (CUPS / QL) | ✕ | ✕ | ✓ | ✓ | ✓ |
