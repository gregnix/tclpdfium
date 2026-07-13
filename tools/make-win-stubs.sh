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

echo
echo "done:"
echo "  $PREFIX/lib/libtclstub.a"
echo "  $PREFIX/lib/libtkstub.a"
echo "  $PREFIX/include/  ($(ls "$PREFIX/include"/*.h | wc -l) headers)"
