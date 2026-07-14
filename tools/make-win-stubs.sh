#!/bin/sh
# make-win-stubs.sh -- Tcl/Tk stub libraries for the MinGW cross toolchain.
#
# Why this exists
# ---------------
# The Windows build notes used to say: "MSYS2 has no mingw-w64-x86_64-tcl9
# package, so cross-compiling for Tcl 9 is impossible." That conclusion mixes
# up two different things.
#
# A *stub library* (libtclstub.a) is not derived from a DLL. It is one small
# C file -- generic/tclStubLib.c, about 160 lines -- compiled from the Tcl
# source tree. It contains Tcl_InitStubs and the stub table pointer, nothing
# else. An *import library* (tcl90.lib) is the thing that is derived from a
# DLL, and that one does need MSVC. The two are unrelated.
#
# So we do not need a MinGW Tcl package. We need the Tcl source tarball and
# two invocations of gcc. That is what this script does.
#
# Usage
# -----
#   ./make-win-stubs.sh [TCLTAG] [PREFIX]
#
#   TCLTAG   Tcl/Tk source tag, e.g. core-9-0-2 (default) or core-8-6-16
#   PREFIX   output directory (default: ./win-stubs)
#
# Result
# ------
#   PREFIX/lib/libtclstub.a
#   PREFIX/lib/libtkstub.a
#   PREFIX/include/            tcl.h, tk.h, X11/, ...
#
# Feed it to the Makefile:
#
#   make PLATFORM=windows TCL_VERSION=9.0 \
#        TCL_INC="-I$PREFIX/include" TK_INC="" \
#        TCL_STUBLIB=$PREFIX/lib/libtclstub.a \
#        TK_STUBLIB=$PREFIX/lib/libtkstub.a

set -e

TAG="${1:-core-9-0-2}"
PREFIX="${2:-$(pwd)/win-stubs}"
CROSS="${CROSS:-x86_64-w64-mingw32}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

command -v "$CROSS-gcc" >/dev/null 2>&1 || {
    echo "error: $CROSS-gcc not found (apt install mingw-w64)" >&2
    exit 1
}

echo "==> tag        $TAG"
echo "==> toolchain  $CROSS-gcc"
echo "==> prefix     $PREFIX"

mkdir -p "$PREFIX/lib" "$PREFIX/include"

fetch() {
    echo "==> fetching $1 $TAG"
    curl -fsSL -o "$WORK/$1.tgz" \
        "https://codeload.github.com/tcltk/$1/tar.gz/refs/tags/$TAG"
    tar xzf "$WORK/$1.tgz" -C "$WORK"
}

fetch tcl
fetch tk

TCLSRC="$WORK/tcl-$TAG"
TKSRC="$WORK/tk-$TAG"

# Headers. The extension needs the public ones; the stub source additionally
# pulls in tclInt.h and friends, which sit in the same directory.
cp "$TCLSRC"/generic/tcl*.h        "$PREFIX/include/" 2>/dev/null || true
cp "$TCLSRC"/win/tclWinPort.h      "$PREFIX/include/" 2>/dev/null || true
cp "$TCLSRC"/libtommath/tommath.h  "$PREFIX/include/" 2>/dev/null || true
cp "$TKSRC"/generic/tk*.h          "$PREFIX/include/" 2>/dev/null || true
cp "$TKSRC"/xlib/*.h               "$PREFIX/include/" 2>/dev/null || true
cp -r "$TKSRC"/xlib/X11            "$PREFIX/include/" 2>/dev/null || true

echo "==> compiling libtclstub.a"
"$CROSS-gcc" -c -O2 -DSTATIC_BUILD -DBUILD_tcl \
    -I"$TCLSRC/generic" -I"$TCLSRC/win" -I"$TCLSRC/libtommath" \
    -o "$WORK/tclStubLib.o" "$TCLSRC/generic/tclStubLib.c"
"$CROSS-ar" rcs "$PREFIX/lib/libtclstub.a" "$WORK/tclStubLib.o"

echo "==> compiling libtkstub.a"
"$CROSS-gcc" -c -O2 -DSTATIC_BUILD -DBUILD_tk \
    -I"$TKSRC/generic" -I"$TKSRC/win" -I"$TKSRC/xlib" \
    -I"$TCLSRC/generic" -I"$TCLSRC/win" \
    -o "$WORK/tkStubLib.o" "$TKSRC/generic/tkStubLib.c"
"$CROSS-ar" rcs "$PREFIX/lib/libtkstub.a" "$WORK/tkStubLib.o"

echo "==> verifying"
for lib in tclstub tkstub; do
    if "$CROSS-nm" "$PREFIX/lib/lib$lib.a" | grep -qE "T (Tcl|Tk)_InitStubs"; then
        echo "    lib$lib.a  ok"
    else
        echo "    lib$lib.a  FAILED -- no InitStubs symbol" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# tclConfig.sh / tkConfig.sh fuer das Windows-Ziel.
#
# TEA liest aus diesen Dateien, welcher Compiler, welche Header und welche
# Stub-Bibliothek zum Ziel-Tcl gehoeren. Fuer einen Cross-Compile gibt es sie
# auf dem Linux-Rechner nicht -- also schreiben wir sie. Es sind Shell-
# Variablen, kein Geheimnis.
#
# Die Stub-Bibliotheken muessen NEBEN der Datei liegen: TEA setzt den Pfad aus
# dem Verzeichnis der tclConfig.sh und dem Namen aus TCL_STUB_LIB_FILE zusammen.
# ---------------------------------------------------------------------------

# Version aus dem Tag ableiten: core-9-0-2 -> 9.0
ver=`echo "$TAG" | sed 's/^core-//; s/-/./g' | cut -d. -f1,2`
major=`echo "$ver" | cut -d. -f1`
minor=`echo "$ver" | cut -d. -f2`

cat > "$PREFIX/lib/tclConfig.sh" <<EOF
# Erzeugt von make-win-stubs.sh -- fuer den Cross-Compile nach Windows.
TCL_VERSION='$ver'
TCL_MAJOR_VERSION='$major'
TCL_MINOR_VERSION='$minor'
TCL_CC='$CROSS-gcc'
TCL_DEFS=''
TCL_SHLIB_SUFFIX='.dll'
TCL_SHLIB_CFLAGS=''
TCL_SHLIB_LD='$CROSS-gcc -shared'
TCL_STUB_LIB_FILE='libtclstub.a'
TCL_STUB_LIB_SPEC='-L$PREFIX/lib -ltclstub'
TCL_STUB_LIB_PATH='$PREFIX/lib/libtclstub.a'
TCL_INCLUDE_SPEC='-I$PREFIX/include'
TCL_PREFIX='$PREFIX'
TCL_EXEC_PREFIX='$PREFIX'
TCL_SRC_DIR='$PREFIX'
TCL_LIB_SPEC=''
TCL_LIBS=''
TCL_THREADS='1'
TCL_CFLAGS_OPTIMIZE='-O2'
TCL_CFLAGS_DEBUG='-g'
TCL_EXTRA_CFLAGS=''
TCL_SHARED_BUILD='1'
EOF

sed -e 's/^TCL_/TK_/' \
    -e "s/'libtclstub.a'/'libtkstub.a'/" \
    -e 's/-ltclstub/-ltkstub/' \
    -e 's|/libtclstub.a|/libtkstub.a|' \
    "$PREFIX/lib/tclConfig.sh" > "$PREFIX/lib/tkConfig.sh"

echo "    tclConfig.sh  ok"
echo "    tkConfig.sh   ok"

echo
echo "done:"
echo "  $PREFIX/lib/libtclstub.a  libtkstub.a"
echo "  $PREFIX/lib/tclConfig.sh  tkConfig.sh"
echo "  $PREFIX/include/  ($(ls "$PREFIX/include"/*.h | wc -l) headers)"
echo
echo "TEA-Cross-Compile damit:"
echo
echo "  ./configure --host=$CROSS --build=\`gcc -dumpmachine\` \\"
echo "              --with-tcl=$PREFIX/lib --with-tk=$PREFIX/lib \\"
echo "              --with-pdfium=\$PWD/vendor/pdfium-win-x64"
echo "  make"
