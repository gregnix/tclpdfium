#!/bin/bash
# createpdf.sh — Etikett erzeugen und drucken
#
# Aufruf: bash scripts/createpdf.sh [hoehe_mm]
# Default: 56mm
#
# Voraussetzung: aus dem tclpdfium-Root-Verzeichnis aufrufen

HOEHE=${1:-56}
PRINTER="Brother_QL-820NWB_IPP"
PNG="img/etikett54.png"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

echo "==> Etikett 54 x ${HOEHE} mm..."
cd "$ROOT_DIR"
TCLLIBPATH=. wish examples/test-etikett-54mm.tcl $HOEHE

echo "==> Drucke auf $PRINTER (Custom.54x${HOEHE}mm)..."
lp -d "$PRINTER" \
   -o "PageSize=Custom.54x${HOEHE}mm" \
   -o MediaType=Roll \
   -o CutMedia=EndOfPage \
   "$PNG"

echo "==> Fertig."
