#!/bin/sh
# build-windows.sh -- pdfiumtcl fuer Windows bauen, auf einem Linux-Rechner.
#
#   ./tools/build-windows.sh [TCLTAG]
#
#   TCLTAG   core-9-0-2 (Vorgabe) oder core-8-6-16
#
# Was passiert:
#   1. Stub-Bibliotheken und Header fuer die MinGW-Toolchain uebersetzen
#      (aus dem Tcl/Tk-Quellbaum -- ein MSYS2-Paket braucht es dafuer nicht).
#      Dabei entstehen auch tclConfig.sh und tkConfig.sh fuers Ziel; ohne die
#      weiss TEA nicht, wogegen es baut.
#   2. PDFium fuer Windows holen, falls noch nicht da.
#   3. In einem EIGENEN Bauverzeichnis konfigurieren und uebersetzen. Der native
#      Baum bleibt unangetastet -- man kann also Linux und Windows nebeneinander
#      bauen, ohne dazwischen aufzuraeumen.
#   4. Das Ergebnis in dist-win/ einsammeln: DLL, pdfium.dll, pkgIndex.tcl.
#
# Ergebnis nach Windows kopieren, in ein Verzeichnis auf dem auto_path.

set -e

TAG="${1:-core-9-0-2}"
CROSS="${CROSS:-x86_64-w64-mingw32}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)

case "$TAG" in
    core-9-*) TCLGEN=9 ;;
    core-8-*) TCLGEN=8 ;;
    *) echo "unbekannter Tag: $TAG (core-9-0-2 oder core-8-6-16)" >&2 ; exit 1 ;;
esac

STUBS="$ROOT/win-stubs-tcl$TCLGEN"
BUILD="$ROOT/build-win-tcl$TCLGEN"
VENDOR="$ROOT/vendor/pdfium-win-x64"
DIST="$ROOT/dist-win"

command -v "$CROSS-gcc" >/dev/null 2>&1 || {
    echo "error: $CROSS-gcc nicht gefunden (apt install mingw-w64)" >&2
    exit 1
}

echo "==> Ziel: Windows x64, Tcl $TCLGEN ($TAG)"
echo

# --- 1. Stubs + Konfigurationsdateien ---------------------------------------
if [ ! -f "$STUBS/lib/tclConfig.sh" ] ; then
    echo "==> Stub-Bibliotheken bauen"
    "$ROOT/tools/make-win-stubs.sh" "$TAG" "$STUBS" | sed 's/^/    /'
else
    echo "==> Stubs vorhanden: $STUBS"
fi

# --- 2. PDFium --------------------------------------------------------------
if [ ! -f "$VENDOR/lib/pdfium.dll.lib" ] ; then
    echo "==> PDFium fuer Windows holen"
    PDFIUM_PLATFORM=win-x64 sh "$ROOT/scripts/setup.sh" | sed 's/^/    /'
else
    echo "==> PDFium vorhanden: $VENDOR"
fi

# --- 3. Konfigurieren und bauen ---------------------------------------------
# Objektdateien eines nativen Laufs muessen aus dem Quellverzeichnis weg.
#
# TEA baut ueber VPATH: make sucht Ziele auch im srcdir. Liegt dort noch ein
# tclpdfiumtcl.o vom Linux-Build, haelt make das Ziel fuer aktuell, uebersetzt
# nicht neu -- und der Linker sucht die Datei dann im Bauverzeichnis, wo sie
# nicht ist. Die Meldung lautet dann "cannot find tclpdfiumtcl.o" und klingt
# nach einem ganz anderen Problem. (Brauchen koennte er die ELF-Objektdatei
# ohnehin nicht.)
#
# Die fertige .so und das Makefile des nativen Baus bleiben unangetastet; nur
# die Objektdatei wird beim naechsten "make" neu erzeugt.
if ls "$ROOT"/*.o >/dev/null 2>&1 ; then
    echo "==> Objektdateien des nativen Baus entfernen (VPATH)"
    rm -f "$ROOT"/*.o
fi

echo "==> Konfigurieren in $BUILD"
rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"

"$ROOT/configure" \
    --host="$CROSS" \
    --build="$(gcc -dumpmachine)" \
    --with-tcl="$STUBS/lib" \
    --with-tk="$STUBS/lib" \
    --with-pdfium="$VENDOR" \
    > configure.log 2>&1 || { tail -20 configure.log ; exit 1; }

grep -E "checking (for PDFium|PDFium link)" configure.log | sed 's/^/    /'

echo "==> Uebersetzen"
make > make.log 2>&1 || { tail -20 make.log ; exit 1; }

DLL=$(ls *.dll 2>/dev/null | head -1)
[ -n "$DLL" ] || { echo "error: keine DLL entstanden" >&2 ; exit 1; }

# --- 4. Pruefen -------------------------------------------------------------
echo "==> Pruefen: $DLL"

deps=$("$CROSS-objdump" -p "$DLL" | grep "DLL Name" | awk '{print $3}')
echo "$deps" | sed 's/^/    /'

# Taucht hier tcl90.dll oder tk90.dll auf, greifen die Stubs nicht -- die
# Erweiterung waere an genau eine Tcl-Installation gefesselt.
if echo "$deps" | grep -qiE "^tcl[0-9]|^tk[0-9]" ; then
    echo "FEHLER: die DLL haengt direkt an Tcl/Tk. Stubs greifen nicht." >&2
    exit 1
fi
# Ebenso die MinGW-Laufzeit -- die wird statisch gelinkt.
if echo "$deps" | grep -qiE "libgcc|winpthread" ; then
    echo "WARNUNG: MinGW-Laufzeit dynamisch gelinkt -- diese DLLs mitliefern." >&2
fi

# Welche Version verlangt Tcl_InitStubs im Binary?
#
# Nicht mit TCL_VERSION vergleichen: die Quelle nennt die MINDEST-Version, nicht
# die des Build-Tcl. Fuer Tcl 8 steht dort "8.5" -- die Erweiterung laeuft ab
# 8.5 aufwaerts. Entscheidend ist allein, dass die GENERATION stimmt: eine
# 8er-Angabe in einer Tcl-9-DLL waere der Fehler, den wir suchen.
#
# strings schluckt per Default alles unter vier Zeichen. Ohne -n 3 sieht man die
# Angabe nie und haelt das Binary faelschlich fuer falsch gebaut.
found=$(strings -n 3 "$DLL" | grep -xE "[89]\.[0-9]" | head -1)

if [ -z "$found" ] ; then
    echo "WARNUNG: keine Versionsangabe im Binary gefunden." >&2
elif [ "${found%%.*}" = "$TCLGEN" ] ; then
    echo "    Tcl_InitStubs verlangt $found -- richtige Generation"
else
    echo "FEHLER: Binary verlangt Tcl $found, gebaut wurde fuer Tcl $TCLGEN." >&2
    exit 1
fi

# --- 5. Einsammeln ----------------------------------------------------------
SUB="windows64"
[ "$TCLGEN" = "9" ] && SUB="windows64-tcl9"

mkdir -p "$DIST/$SUB"
cp "$DLL" "$DIST/$SUB/"
cp "$VENDOR/bin/pdfium.dll" "$DIST/$SUB/"
cp pkgIndex.tcl "$DIST/$SUB/"

echo
echo "==> Fertig:"
echo "    $DIST/$SUB/$DLL"
echo "    $DIST/$SUB/pdfium.dll        (muss NEBEN der DLL liegen)"
echo "    $DIST/$SUB/pkgIndex.tcl"
echo
echo "Auf Windows testen:"
echo "    tclsh tools/test-windows.tcl dist-win/$SUB test.pdf"
