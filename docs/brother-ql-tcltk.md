# Brother QL-Etikettendrucker unter Linux mit Tcl/Tk

Stand: 2026-03-25 — Erarbeitet mit pdfiumtcl 0.1

---

## Inhaltsverzeichnis

1. Grundlagen: Pixelbreiten und Auflösung
2. Druckwege im Überblick
3. brother_ql (Python) — Stärken und Schwächen
4. CUPS PPD/Filter-Weg — Einrichtung und Grenzen
5. IPP-Everywhere — der funktionierende Weg (empfohlen)
6. Brother QL-820NWB Treiber installieren
7. CUPS-Optionen des QL-820NWB
8. ppdtool — PPD-Verwaltung mit Tcl
9. pdfiumtcl — das Tcl-Rendering-Binding
10. labelutil-Modul
11. Druckpipeline: PDF → PDFium → PNG → Drucker
12. Tcl-Code: ql_width_px, do_print_ql
13. Etikett mit pdf4tcl erzeugen
14. createpdf.sh — CLI für Etikettendruck
15. Fehler und Fallstricke
16. Referenz: Alle QL-Bandbreiten

---

## 1. Grundlagen: Pixelbreiten und Auflösung

Der Brother QL-820NWB druckt auf Endlosbändern verschiedener Breite.
Die Pixelbreite ist vom Drucker fest vorgegeben und hängt **nicht**
von DPI ab — sie ist hardwareseitig fixiert.

### Korrekte Pixelbreiten (aus brother_ql / labelutil)

| Bandbreite (mm) | Pixelbreite | Anmerkung |
|-----------------|-------------|-----------|
| 12 mm | 106 px | Schmalband |
| 29 mm | 306 px | Schmales Endlosband |
| 38 mm | 413 px | |
| 50 mm | 554 px | |
| **54 mm** | **590 px** | Standard-Endlosband, häufigste Wahl |
| 62 mm | 696 px | Breites Endlosband |
| 102 mm | 1164 px | Breitestes Band |

**Wichtig:** Diese Werte gelten für 300 DPI. Bei 600 DPI verdoppeln
sich die Pixelwerte (54mm → 1182 px).

### Häufiger Fehler

Eine falsche Berechnung via `mm / 25.4 * DPI` liefert für 54mm bei
300 DPI den Wert 638 px — das ist falsch. Der korrekte Wert ist 590 px.
Der Unterschied entsteht weil die Druckbreite des QL-820 nicht exakt
300 DPI entspricht, sondern die Breite hardwareseitig auf 590 Pixel
festgelegt ist.

### Länge

Die Länge des Etiketts ist variabel (Endlosband). Sie ergibt sich
proportional aus der PDF-Seite: wenn die PDF-Seite 54 mm breit und
90 mm hoch ist, wird das Etikett 590 × 984 px groß gerendert.

---

## 2. Druckwege im Überblick

```
PDF-Datei
    │
    ├── Weg A: brother_ql (Python)
    │         PNG → brother_ql print -l 54 bild.png
    │         Vorteil: direkt, TCP/IP
    │         Nachteil: Python/pip, schwerfällige Installation
    │
    ├── Weg B: CUPS PPD/Filter
    │         lp -d QL-820NWB -o PageSize=54mm bild.png
    │         Vorteil: systemweit
    │         Nachteil: Filter fehlt oft, PPD-Chaos, funktioniert
    │                   nicht zuverlässig mit neuem CUPS
    │
    └── Weg C: CUPS IPP-Everywhere + pdfiumtcl  (empfohlen)
              PDF → pdfium::render -width 590 → PNG → lp
              Vorteil: kein Python, kein Filter, funktioniert,
                       variable Länge, Auto-Cut
```

---

## 3. brother_ql (Python) — Stärken und Schwächen

`brother_ql` ist ein Python-Paket für die direkte Ansteuerung von
Brother QL-Druckern über USB oder TCP/IP.

### Installation

```bash
pip install brother_ql
```

### Verwendung

```bash
export BROTHER_QL_PRINTER=tcp://192.168.158.241
export BROTHER_QL_MODEL=QL-820NWB
brother_ql print -l 54 bild.png
```

### Schwächen

- Python + pip erforderlich — auf manchen Systemen aufwendige Installation
- Virtualenv-Probleme bei neueren Python-Versionen
- Keine native Tcl-Integration

---

## 4. CUPS PPD/Filter-Weg — Einrichtung und Grenzen

### Was nicht funktioniert

Der klassische CUPS PPD/Filter-Weg mit `brother_lpdwrapper_ql820nwb`
hat mehrere Probleme:

**Problem 1: Filter fehlt**

```
E Brother_QL-820NWB_IPP: Datei "/usr/lib/cups/filter/brother_lpdwrapper_ql820nwb"
  nicht verfügbar: No such file or directory
```

Der Filter kommt vom offiziellen Brother-Treiber, ist aber oft nicht
installiert.

**Problem 2: PPD erwartet PostScript/PDF, nicht PNG**

```
*cupsFilter: "application/vnd.cups-postscript 0 brother_lpdwrapper_ql820nwb"
*cupsFilter: "application/vnd.cups-pdf 0 brother_lpdwrapper_ql820nwb"
```

PNG-Dateien werden vom Filter nicht akzeptiert.

**Problem 3: CUPS deprecation**

```
W Printer drivers are deprecated and will stop working in a future version of CUPS.
```

CUPS 3.x wird PPD/Filter komplett abschaffen.

### Fazit

Den PPD/Filter-Weg nicht verwenden. Stattdessen IPP-Everywhere (Abschnitt 5).

---

## 5. IPP-Everywhere — der funktionierende Weg

### Drucker einrichten

```bash
# IP des Druckers ermitteln
ping -c1 BRN94DDF8A624B8.local
# -> 192.168.158.243

# Drucker als IPP-Everywhere einrichten (kein PPD, kein Filter)
sudo lpadmin -p Brother_QL-820NWB_IPP \
    -v ipp://192.168.158.243/ipp/print \
    -m everywhere \
    -E
```

**Wichtig:** `-m everywhere` statt eines PPD-Treibers. CUPS holt die
Druckerfähigkeiten direkt vom Drucker über IPP.

### Verfügbare Optionen abfragen

```bash
lpoptions -p Brother_QL-820NWB_IPP -l
```

Ausgabe (QL-820NWB):
```
PageSize/Media Size: 12x12mm 17x54mm ... *29x90mm ... Custom.WIDTHxHEIGHT
MediaType/Media Type: *Labels Roll
CutMedia/CutMedia: *None Auto EndOfPage EndOfJob
cupsPrintQuality: Draft *Normal High
```

### Funktionierende Druckzeile (54mm Endlosband)

```bash
lp -d Brother_QL-820NWB_IPP \
   -o PageSize=Custom.54x56mm \
   -o MediaType=Roll \
   -o CutMedia=EndOfPage \
   etikett.png
```

### Wichtige Parameter

| Parameter | Wert | Bedeutung |
|-----------|------|-----------|
| `PageSize` | `Custom.54x56mm` | Breite x Höhe in mm |
| `MediaType` | `Roll` | Endlosband (nicht `Tape`, nicht `Labels`) |
| `CutMedia` | `EndOfPage` | Automatischer Schnitt nach Etikett |
| `CutMedia` | `None` | Kein Schnitt (Endlosdruck) |

### Warum `Roll` und nicht `Tape`?

Die PPD des ptouch-ql-Treibers kennt `Tape` (Endlosband) und `Labels`
(vorgestanzt). Beim IPP-Everywhere-Treiber heißen die Optionen anders:
`Roll` für Endlosband. `Tape` und `Labels` blinken beim QL-820NWB.

### Custom.WIDTHxHEIGHT

`Custom.WIDTHxHEIGHT` ist ein CUPS-Platzhalter für beliebige Maße.
Der Drucker akzeptiert jeden Wert — `Custom.54x56mm`, `Custom.54x90mm`,
`Custom.62x100mm` etc. Der Wert muss nicht in der PPD definiert sein.

---

## 6. Brother QL-820NWB Treiber installieren

Der offizielle Brother-Treiber ist für den PPD/Filter-Weg nötig.
Für IPP-Everywhere (Abschnitt 5) ist er nicht erforderlich.
Er enthält aber `brpapertoollpr_ql820nwb` — das Tool zum Hinzufügen
eigener Papierformate zur PPD.

### Treiber-Paket

Brother liefert das Paket `ql820nwbpdrv-3.1.5-0.i386` (trotz Name
enthält es 64-Bit-Binaries unter `lpd/x86_64/`).

### Inhalt des Pakets

```
cupswrapper/
  brother_lpdwrapper_ql820nwb   Perl-Script (CUPS-Filter)
  brother_ql820nwb_printer_en.ppd  PPD-Datei
  cupswrapperql820nwb           Wrapper-Skript

lpd/x86_64/                     64-Bit-Binaries
  brpapertoollpr_ql820nwb        Papierformat-Tool
  brpapertoolcups
  brprintconfpt1_ql820nwb
  rastertobrpt1                  Raster-Konverter

lpd/i686/                       32-Bit-Binaries (nicht verwenden)
  brpapertoollpr_ql820nwb
  ...

inf/
  paperinfql820nwb               Papierformat-Definitionen
  brql820nwbrc                   Drucker-Konfiguration
```

### Installation

```bash
# 1. ZIP entpacken
cd /tmp && mkdir -p brother_drv && cd brother_drv
unzip ql820nwbpdrv-3_1_5-0_i386_20260325_1115.zip

# 2. Defekten Symlink entfernen (falls vorhanden)
sudo rm -f /usr/lib/cups/filter/brother_lpdwrapper_ql820nwb

# 3. Filter und Treiber installieren
sudo bash << 'EOF'
SRC="/tmp/brother_drv/ql820nwbpdrv-3.1.5-0.i386"
BASE="$SRC/opt/brother/PTouch/ql820nwb"

cp "$BASE/cupswrapper/brother_lpdwrapper_ql820nwb" /usr/lib/cups/filter/
chmod 755 /usr/lib/cups/filter/brother_lpdwrapper_ql820nwb

mkdir -p /opt/brother/PTouch/ql820nwb/{cupswrapper,inf,lpd/x86_64}
cp "$BASE/cupswrapper/"* /opt/brother/PTouch/ql820nwb/cupswrapper/
cp "$BASE/inf/"*         /opt/brother/PTouch/ql820nwb/inf/
cp "$BASE/lpd/filter_ql820nwb" /opt/brother/PTouch/ql820nwb/lpd/
cp "$BASE/lpd/x86_64/"* /opt/brother/PTouch/ql820nwb/lpd/x86_64/
chmod 755 /opt/brother/PTouch/ql820nwb/lpd/x86_64/*

ln -sf /opt/brother/PTouch/ql820nwb/lpd/x86_64/brpapertoollpr_ql820nwb \
       /usr/bin/brpapertoollpr_ql820nwb

systemctl restart cups
EOF
```

### brpapertoollpr — Papierformate verwalten

Das Brother-eigene Tool fügt Formate in die PPD ein:

```bash
# Format hinzufügen
sudo brpapertoollpr_ql820nwb \
    -P Brother_QL-820NWB_IPP \
    -n custom54x56 -w 54 -h 56

# Format löschen
sudo brpapertoollpr_ql820nwb \
    -P Brother_QL-820NWB_IPP \
    -d custom54x56
```

---

## 7. CUPS-Optionen des QL-820NWB

### Vollständige lpoptions-Ausgabe (IPP-Everywhere)

```
PageSize/Media Size:
  12x12mm 17x54mm 17x87mm 23x23mm 24x24mm 29x42mm 29x52mm
  29x54mm 29x62mm *29x90mm 38x90mm 39x48mm 58x58mm 60x86mm
  62x100mm Custom.WIDTHxHEIGHT

MediaType/Media Type: *Labels Roll

cupsPrintQuality: Draft *Normal High

ColorModel/Output Mode: *Gray

OutputBin/OutputBin: *FaceDown

CutMedia/CutMedia: *None Auto EndOfPage EndOfJob
```

### PPD-Optionen (ptouch-ql-Treiber, nicht IPP-Everywhere)

```
PageSize: 12mm 29mm 38mm 50mm 54mm 62mm ... Custom.WIDTHxHEIGHT
MediaType: Labels  (Die-Cut Labels)
           *Tape   (Continuous-Length Tape)
Resolution: *300dpi  300x600dpi
AutoCut: *True False
CutLabel: *0 1 2 ... 10
ExtraMargin: *0mm 1mm ... 27mm
```

### Wichtige Unterschiede PPD vs. IPP-Everywhere

| Option | PPD (ptouch-ql) | IPP-Everywhere |
|--------|-----------------|----------------|
| Endlosband | `MediaType=Tape` | `MediaType=Roll` |
| Vorgestanzt | `MediaType=Labels` | `MediaType=Labels` |
| Auto-Cut | `AutoCut=True` | `CutMedia=EndOfPage` |
| Custom-Format | `PageSize=Custom.54x56mm` | `PageSize=Custom.54x56mm` |

---

## 8. ppdtool — PPD-Verwaltung mit Tcl

`ppdtool.tcl` ist ein Tcl-Skript zur Verwaltung von PPD-Papierformaten.
Es dient als Ersatz für `brpapertoollpr` wenn der offizielle Treiber
nicht installiert ist.

### Befehle

```bash
# Alle Formate anzeigen
sudo tclsh ppdtool.tcl list

# Einzelnes Format anzeigen (alle 4 PPD-Einträge)
sudo tclsh ppdtool.tcl detail 54mm
sudo tclsh ppdtool.tcl detail custom54x56

# Format hinzufügen (54mm breit, 56mm hoch)
sudo tclsh ppdtool.tcl add 54 56

# Format löschen
sudo tclsh ppdtool.tcl delete custom54x56
```

### Beispielausgabe list

```
PPD: /etc/cups/ppd/Brother_QL-820NWB_IPP.ppd
Alle PageSize-Einträge:
------------------------------------------------------------
  54mm                54.0 x 100.2 mm  (153 x 284 pt)
  62mm                62.1 x 100.2 mm  (176 x 284 pt)
  custom54x56         54.0 x 56.1 mm   (153 x 159 pt)
------------------------------------------------------------
```

### Backup

Vor jeder Änderung wird automatisch ein Backup erstellt:
```
/etc/cups/ppd/Brother_QL-820NWB_IPP.ppd.bak_20260325_070208
```

---

## 9. pdfiumtcl — das Tcl-Rendering-Binding

pdfiumtcl ist ein Tcl-Binding für PDFium (Googles PDF-Engine, BSD-Lizenz).

### Verfügbare Befehle

```tcl
pdfium::open    filename ?password?  -> doc-handle
pdfium::close   doc-handle
pdfium::pagecount doc-handle         -> integer
pdfium::pagesize  doc-handle pagenum -> {width_mm height_mm}
pdfium::render  doc-handle pagenum \
                ?-dpi n? \
                ?-width px? \
                ?-imagename name?    -> image-name (Tk photo)
pdfium::gettext doc-handle pagenum  -> string
```

### Wichtig: -width vs. -dpi

```tcl
# -width: rendert auf exakt diese Pixelbreite (empfohlen für QL)
pdfium::render $doc 0 -width 590

# -dpi: skaliert proportional nach DPI
pdfium::render $doc 0 -dpi 150
```

### Installation

```bash
# 1. PDFium-Binary herunterladen
wget https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-linux-x64.tgz
sudo mkdir -p /opt/pdfium
sudo tar -xzf pdfium-linux-x64.tgz -C /opt/pdfium

# 2. Abhängigkeiten
sudo apt install tcl-dev tk-dev build-essential

# 3. Kompilieren
make clean && make

# 4. Stubs prüfen (kein libtcl in ldd)
make check
# -> OK: kein direkter libtcl/libtk Link — Stubs korrekt
# -> T Pdfiumtcl_Init
```

### Portabilität

Das Binding ist stub-basiert — `ldd pdfiumtcl.so | grep tcl` liefert
nichts. Es läuft auf jedem Tcl 8.5+ ohne Neukompilieren.
`libpdfium.so` liegt mit `rpath=$ORIGIN` im gleichen Verzeichnis.

---

## 10. labelutil-Modul

`labelutil-0.0.1.tm` ist ein Tcl-Modul für Canvas-basierte Etikettenbilder.

### Pixelbreiten-Tabelle

```tcl
variable labels {
    12      106
    29      306
    38      413
    50      554
    54      590
    62      696
    102    1164
}

labelutil::labelWidth 54   ;# -> 590
labelutil::labelWidth 62   ;# -> 696
```

---

## 11. Druckpipeline: PDF → PDFium → PNG → Drucker

```
pdf4tcl                    pdfiumtcl              CUPS IPP-Everywhere
─────────                  ─────────              ──────────────────
54mm PDF ──► pdfium::render -width 590 ──► PNG ──► lp -d QL-820NWB
erzeugen                                           -o PageSize=Custom.54x56mm
                                                   -o MediaType=Roll
                                                   -o CutMedia=EndOfPage
```

---

## 12. Tcl-Code: ql_width_px und do_print_ql

### Hilfsfunktion: ql_width_px

```tcl
proc ql_width_px {mm} {
    set table {12 106  29 306  38 413  50 554  54 590  62 696  102 1164}
    if {[dict exists $table $mm]} {
        return [dict get $table $mm]
    }
    return [expr {int($mm / 25.4 * 300 + 0.5)}]
}
```

### Druckprozedur (funktionierende Version)

```tcl
proc do_print_ql {printer dpi range} {
    global state

    set w_px [ql_width_px 54]
    if {$dpi == 600} { set w_px [expr {$w_px * 2}] }

    foreach p $pages {
        set tmpfile [file join /tmp "ql_p${p}_[pid].png"]

        pdfium::render $state(doc) $p \
            -width $w_px -imagename qlpage
        qlpage write $tmpfile -format png

        set ih [image height qlpage]
        set h_mm [format "%.1f" [expr {$ih / 300.0 * 25.4}]]

        # IPP-Everywhere Druckbefehl
        exec lp -d $printer \
            -o "PageSize=Custom.54x${h_mm}mm" \
            -o MediaType=Roll \
            -o CutMedia=EndOfPage \
            $tmpfile

        catch { file delete $tmpfile }
    }
}
```

---

## 13. Etikett mit pdf4tcl erzeugen

### Minimales Etikett 54 × 56 mm

```tcl
package require pdf4tcl

proc mm {mm} { expr {$mm / 25.4 * 72.0} }

set W_pt [mm 54.0]
set H_pt [mm 56.0]   ;# variable Höhe

set pdf [::pdf4tcl::new %AUTO% \
    -paper [list $W_pt $H_pt] \
    -orient true \
    -compress 1]

$pdf startPage
$pdf setFont 14 Helvetica-Bold
$pdf text "Firmenname GmbH" \
    -x [expr {$W_pt / 2.0}] -y [expr {[mm 3]+8}] -align center
$pdf endPage
$pdf write -file etikett.pdf
$pdf destroy
```

**Wichtig:** Kein `incr y $lh` — pt-Werte sind Floats:

```tcl
# FALSCH:
incr y $lh

# RICHTIG:
set y [expr {$y + $lh}]
```

---

## 14. createpdf.sh — CLI für Etikettendruck

```bash
#!/bin/bash
# Aufruf: bash createpdf.sh [hoehe_mm]

HOEHE=${1:-56}
PRINTER="Brother_QL-820NWB_IPP"
PNG="etikett.png"

echo "==> Etikett 54 x ${HOEHE} mm..."
TCLLIBPATH=. wish test-etikett-54mm.tcl $HOEHE

echo "==> Drucke auf $PRINTER..."
lp -d "$PRINTER" \
   -o "PageSize=Custom.54x${HOEHE}mm" \
   -o MediaType=Roll \
   -o CutMedia=EndOfPage \
   "$PNG"
```

Aufruf:

```bash
bash createpdf.sh 56    # 54 x 56mm
bash createpdf.sh 90    # 54 x 90mm
bash createpdf.sh 120   # 54 x 120mm
```

---

## 15. Fehler und Fallstricke

### Drucker blinkt

```
Ursache: Falscher MediaType
Lösung:  MediaType=Roll (nicht Tape, nicht Labels)
```

### Auftrag verschwindet ohne Fehler

```
Ursache: CUPS hat keinen passenden Filter für PNG (PPD/Filter-Weg)
Lösung:  Drucker als IPP-Everywhere einrichten (-m everywhere)
```

### Filter nicht gefunden

```
E brother_lpdwrapper_ql820nwb: No such file or directory
Ursache: Brother-Treiber nicht installiert
Lösung:  Abschnitt 6 (Treiber installieren) ODER
         IPP-Everywhere verwenden (kein Filter nötig)
```

### Custom-Format funktioniert nicht

```
Problem: Custom.54x56mm wird abgelehnt
Ursache: PPD/Filter-Weg — Custom-Formate nur bei PostScript/PDF
Lösung:  IPP-Everywhere: Custom.WIDTHxHEIGHT funktioniert immer
```

### Kein Auto-Cut

```
Ursache: CutMedia nicht gesetzt (Default: None)
Lösung:  -o CutMedia=EndOfPage
```

### Falsche Pixelbreite

```
Problem: 638px statt 590px
Ursache: mm/25.4*dpi statt Tabellenwert
Lösung:  ql_width_px verwenden
```

### A4-PDF auf QL

```
Problem: Inhalt winzig
Ursache: A4 = 210mm, auf 54mm skaliert
Lösung:  PDF muss 54mm breit sein (pdf4tcl: -paper [list W_pt H_pt])
```

---

## 16. Referenz: Alle QL-Bandbreiten

| Band mm | Pixel 300 DPI | Pixel 600 DPI |
|---------|---------------|---------------|
| 12 | 106 | 212 |
| 29 | 306 | 612 |
| 38 | 413 | 826 |
| 50 | 554 | 1108 |
| **54** | **590** | **1182** |
| 62 | 696 | 1392 |
| 102 | 1164 | 2328 |

### QL-820NWB Netzwerk

- IP: `192.168.158.243`
- IPP: `ipp://192.168.158.243/ipp/print`
- IPPS: `ipps://BRN94DDF8A624B8.local:443/ipp/print`
- TCP-Port 9100 (Raster-Direktdruck)

### Schnellreferenz: vollständige Druckzeile

```bash
lp -d Brother_QL-820NWB_IPP \
   -o PageSize=Custom.54x56mm \
   -o MediaType=Roll \
   -o CutMedia=EndOfPage \
   etikett.png
```

---

## Siehe auch

- `pdfiumtcl.c` — PDFium-Binding für Tcl/Tk
- `viewer.tcl` — PDF-Viewer mit QL-Druckdialog
- `test-etikett-54mm.tcl` — Beispiel-Etikett mit pdf4tcl
- `createpdf.sh` — CLI für Etikettendruck
- `ppdtool.tcl` — PPD-Verwaltung
- `install-brother-ql820nwb.sh` — Treiber-Installation
- `labelutil-0.0.1.tm` — Canvas-basiertes Etikettenmodul
- bblanchon/pdfium-binaries — Vorkompilierte PDFium-Binaries
