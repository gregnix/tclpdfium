#!/usr/bin/env bash
# build-win.sh -- baut pdfiumtcl.dll fuer Windows x64 in EINEM Lauf:
#   windows64/        (Tcl 8.6)
#   windows64-tcl9/   (Tcl 9.0)
# Tcl/Tk-Stubs werden fuer beide Versionen aus der Quelle gebaut (einmalig,
# gecacht), die pdfium-Import-Lib automatisch aus der DLL erzeugt.
# Voraussetzung: sudo apt install mingw-w64 build-essential curl
#
# ====== NUR HIER ANPASSEN ===================================================
# tclpdfium-QUELL-Repo (mit src/ und vendor/pdfium/include):
REPO="/home/greg/Project/2026/code/pdf/pdfium/tclpdfium/tclpdfium"
SRC="$REPO/src/pdfiumtcl.c"
PDFIUM_INC="$REPO/vendor/pdfium/include"

# Windows-pdfium.dll + Import-Lib holst du EINMALIG mit:
#   PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh
# (legt pdfium.dll nach vendor/pdfium/bin und pdfium.dll.lib nach vendor/pdfium/lib)
PDFIUM_DLL="$REPO/vendor/pdfium/bin/pdfium.dll"

# Echte Import-Lib -- fehlt sie, wird sie automatisch aus der DLL erzeugt:
PDFIUM_LIB="$REPO/vendor/pdfium/lib/pdfium.dll.lib"

# Ziel-BASIS. Ergebnis: $OUTBASE/windows64 und $OUTBASE/windows64-tcl9:
OUTBASE="$REPO/dist/windows"
# ============================================================================

set -euo pipefail
HOST=x86_64-w64-mingw32
WORK=${WORK:-/tmp/win-build}
WIN86=${WIN86:-/tmp/win86}     # Tcl/Tk 8.6 Windows-Stubs
WIN90=${WIN90:-/tmp/win90}     # Tcl/Tk 9.0 Windows-Stubs
TAG86=${TAG86:-core-8-6-17}
TAG90=${TAG90:-core-9-0-3}

# Pfade absolut machen (vor jedem cd), dann pruefen
SRC=$(realpath -m "$SRC"); PDFIUM_INC=$(realpath -m "$PDFIUM_INC")
PDFIUM_DLL=$(realpath -m "$PDFIUM_DLL"); OUTBASE=$(realpath -m "$OUTBASE")
[ -n "$PDFIUM_LIB" ] && PDFIUM_LIB=$(realpath -m "$PDFIUM_LIB") || true
[ -f "$SRC" ]        || { echo "SRC fehlt -> Zeile anpassen:        $SRC"; exit 1; }
[ -d "$PDFIUM_INC" ] || { echo "PDFIUM_INC fehlt -> Zeile anpassen: $PDFIUM_INC"; exit 1; }
[ -f "$PDFIUM_DLL" ] || { echo "PDFIUM_DLL fehlt -> erst:  PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh"; echo "                         (gesucht: $PDFIUM_DLL)"; exit 1; }
if [ -n "$PDFIUM_LIB" ] && [ ! -f "$PDFIUM_LIB" ]; then
  echo ">> Import-Lib nicht gefunden ($PDFIUM_LIB) -> wird aus der DLL erzeugt"; PDFIUM_LIB=""
fi
mkdir -p "$WORK"

echo "SRC        = $SRC"
echo "PDFIUM_INC = $PDFIUM_INC"
echo "PDFIUM_DLL = $PDFIUM_DLL"
echo "OUTBASE    = $OUTBASE"; echo

# --- Stubs einer Version aus der Quelle bauen (einmalig) --------------------
build_stubs() {  # $1=tag  $2=prefix
  local tag=$1 prefix=$2
  if ls "$prefix"/lib/libtclstub*.a >/dev/null 2>&1 && ls "$prefix"/lib/libtkstub*.a >/dev/null 2>&1; then
    return 0
  fi
  echo ">> baue Tcl/Tk $tag Windows-Stubs nach $prefix ..."
  cd "$WORK"
  [ -f "tcl-$tag.tgz" ] || curl -fsSL -o "tcl-$tag.tgz" "https://codeload.github.com/tcltk/tcl/tar.gz/refs/tags/$tag"
  [ -f "tk-$tag.tgz"  ] || curl -fsSL -o "tk-$tag.tgz"  "https://codeload.github.com/tcltk/tk/tar.gz/refs/tags/$tag"
  rm -rf "tcl-$tag" "tk-$tag"; tar xzf "tcl-$tag.tgz"; tar xzf "tk-$tag.tgz"
  ( cd "tcl-$tag/win" && ./configure --host=$HOST --prefix="$prefix" >/dev/null && make >/dev/null && make install >/dev/null )
  ( cd "tk-$tag/win"  && ./configure --host=$HOST --prefix="$prefix" --with-tcl="$prefix/lib" >/dev/null && make >/dev/null && make install >/dev/null )
}

# --- pdfiumtcl.dll gegen eine Stub-Version linken ---------------------------
link_one() {  # $1=prefix  $2=outdir
  local prefix=$1 outdir=$2 tclstub tkstub
  tclstub=$(ls "$prefix"/lib/libtclstub*.a | head -1)
  tkstub=$(ls "$prefix"/lib/libtkstub*.a | head -1)
  mkdir -p "$outdir"
  $HOST-gcc -shared -O2 -DUSE_TCL_STUBS -DUSE_TK_STUBS \
    -I"$prefix/include" -I"$PDFIUM_INC" \
    "$SRC" "$tclstub" "$tkstub" "$PDFIUM_LIB" \
    -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic \
    -o "$outdir/pdfiumtcl.dll"
  cp "$PDFIUM_DLL" "$outdir/pdfium.dll"
  echo "--- $outdir : $(basename "$tclstub") + $(basename "$tkstub") ---"
  $HOST-objdump -p "$outdir/pdfiumtcl.dll" | grep -i "DLL Name" | sort -u
}

# --- Import-Lib einmalig aus der DLL erzeugen (falls keine echte) -----------
if [ -z "$PDFIUM_LIB" ]; then
  PDFIUM_LIB="$WORK/pdfium.dll.lib"
  { echo "LIBRARY pdfium.dll"; echo "EXPORTS"
    $HOST-objdump -p "$PDFIUM_DLL" \
      | awk '/\[Ordinal\/Name Pointer\] Table/{f=1;next} f&&NF==0{f=0} f&&/\[[ ]*[0-9]+\]/{print $NF}'
  } > "$WORK/pdfium.def"
  $HOST-dlltool -d "$WORK/pdfium.def" -D pdfium.dll -l "$PDFIUM_LIB"
fi

build_stubs "$TAG86" "$WIN86"
build_stubs "$TAG90" "$WIN90"
link_one "$WIN86" "$OUTBASE/windows64"
link_one "$WIN90" "$OUTBASE/windows64-tcl9"
echo; echo "Fertig: $OUTBASE/{windows64,windows64-tcl9}/{pdfiumtcl.dll,pdfium.dll}"
