#!/bin/bash
# setup.sh — PDFium herunterladen und in vendor/ einrichten
#
# Aufruf: bash scripts/setup.sh
#
# Lädt pdfium-linux-x64.tgz von bblanchon/pdfium-binaries
# und entpackt es nach vendor/pdfium/

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
VENDOR_DIR="$ROOT_DIR/vendor/pdfium"
URL="https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-linux-x64.tgz"
TGZ="/tmp/pdfium-linux-x64.tgz"

echo "==> pdfiumtcl setup"
echo "    Ziel: $VENDOR_DIR"
echo ""

# Prüfen ob libpdfium.so bereits vorhanden
if [ -f "$VENDOR_DIR/lib/libpdfium.so" ]; then
    echo "OK: libpdfium.so bereits vorhanden — überspringe Download."
    echo "    Zum Neuinstallieren: rm -rf vendor/pdfium/lib vendor/pdfium/include"
    exit 0
fi

# wget oder curl?
if command -v wget &>/dev/null; then
    DL="wget -q --show-progress -O $TGZ $URL"
elif command -v curl &>/dev/null; then
    DL="curl -L --progress-bar -o $TGZ $URL"
else
    echo "FEHLER: weder wget noch curl gefunden."
    exit 1
fi

echo "==> Lade PDFium herunter..."
echo "    $URL"
$DL

echo "==> Entpacke nach $VENDOR_DIR ..."
mkdir -p "$VENDOR_DIR"
tar -xzf "$TGZ" -C "$VENDOR_DIR"
rm -f "$TGZ"

# Prüfen
if [ -f "$VENDOR_DIR/lib/libpdfium.so" ]; then
    echo ""
    echo "OK: PDFium eingerichtet."
    echo "    lib:     $VENDOR_DIR/lib/libpdfium.so"
    echo "    include: $VENDOR_DIR/include/"
    echo ""
    echo "==> Jetzt kompilieren:"
    echo "    make clean && make"
else
    echo "FEHLER: libpdfium.so nicht gefunden nach Entpacken."
    exit 1
fi
