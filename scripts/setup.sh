#!/bin/bash
# setup.sh — PDFium herunterladen und in vendor/ einrichten
#
# Aufruf: bash scripts/setup.sh
#
# Erkennt Plattform automatisch:
#   Linux x86_64  -> pdfium-linux-x64.tgz
#   Linux arm64   -> pdfium-linux-arm64.tgz
#   macOS arm64   -> pdfium-mac-arm64.tgz
#   macOS x86_64  -> pdfium-mac-x64.tgz
#   Windows x64   -> pdfium-win-x64.tgz (fuer Cross-Compile)
#
# Manuell:
#   PDFIUM_PLATFORM=windows bash scripts/setup.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
VENDOR_DIR="$ROOT_DIR/vendor/pdfium"
BASE_URL="https://github.com/bblanchon/pdfium-binaries/releases/latest/download"

# Plattform ermitteln
if [ -n "$PDFIUM_PLATFORM" ]; then
    PLATFORM="$PDFIUM_PLATFORM"
else
    OS="$(uname -s)"
    ARCH="$(uname -m)"
    case "$OS" in
        Linux)
            case "$ARCH" in
                aarch64|arm64) PLATFORM="linux-arm64" ;;
                armv7*)        PLATFORM="linux-arm"   ;;
                *)             PLATFORM="linux-x64"   ;;
            esac ;;
        Darwin)
            case "$ARCH" in
                arm64) PLATFORM="mac-arm64" ;;
                *)     PLATFORM="mac-x64"   ;;
            esac ;;
        MINGW*|MSYS*|CYGWIN*)
            PLATFORM="win-x64" ;;
        *)
            echo "WARNUNG: Unbekanntes System '$OS', nehme linux-x64"
            PLATFORM="linux-x64" ;;
    esac
fi

# Dateinamen und erwartete Library
ARCHIVE="pdfium-${PLATFORM}.tgz"
URL="$BASE_URL/$ARCHIVE"
TMP="/tmp/$ARCHIVE"

case "$PLATFORM" in
    win*)   LIB="lib/pdfium.dll" ;;
    mac*)   LIB="lib/libpdfium.dylib" ;;
    *)      LIB="lib/libpdfium.so" ;;
esac

echo "==> pdfiumtcl setup"
echo "    Plattform: $PLATFORM"
echo "    Ziel:      $VENDOR_DIR"
echo ""

# Pruefen ob bereits vorhanden
if [ -f "$VENDOR_DIR/$LIB" ]; then
    echo "OK: PDFium bereits vorhanden ($LIB) — ueberspringe Download."
    echo "    Zum Neuinstallieren: rm -rf vendor/pdfium/lib vendor/pdfium/include"
    exit 0
fi

# wget oder curl
if command -v wget &>/dev/null; then
    DL="wget -q --show-progress -O $TMP $URL"
elif command -v curl &>/dev/null; then
    DL="curl -L --progress-bar -o $TMP $URL"
else
    echo "FEHLER: weder wget noch curl gefunden."
    exit 1
fi

echo "==> Lade PDFium herunter..."
echo "    $URL"
$DL

echo "==> Entpacke nach $VENDOR_DIR ..."
mkdir -p "$VENDOR_DIR"
tar -xzf "$TMP" -C "$VENDOR_DIR"
rm -f "$TMP"

# Ergebnis prüfen
if [ -f "$VENDOR_DIR/$LIB" ]; then
    echo ""
    echo "OK: PDFium eingerichtet."
    echo "    $VENDOR_DIR/$LIB"
    echo "    $VENDOR_DIR/include/"
    echo ""
    case "$PLATFORM" in
        win*)
            echo "==> Cross-Compile fuer Windows:"
            echo "    make PLATFORM=windows"
            echo ""
            echo "    Tcl-Stub-Library setzen (Magicsplat-Pfad als Beispiel):"
            echo "    make PLATFORM=windows WIN_TCL_ROOT=/pfad/zu/tcl" ;;
        mac*)
            echo "==> Kompilieren:"
            echo "    make clean && make" ;;
        *)
            echo "==> Kompilieren:"
            echo "    make clean && make" ;;
    esac
else
    echo "FEHLER: $LIB nicht gefunden nach Entpacken."
    echo "        Bitte manuell pruefen: ls $VENDOR_DIR/lib/"
    exit 1
fi
