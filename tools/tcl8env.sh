#!/bin/sh
# tcl8env.sh -- Tcl 8.6 Umgebung fuer tclpdfium
#
# Aufruf:
#   . tools/tcl8env.sh          (in aktuelle Shell einlesen)
#   source tools/tcl8env.sh
#
# Danach normal arbeiten:
#   make
#   make check
#   make install    -> ~/lib/tcl8.6/tclpdfium0.4/

HOME_DIR="${HOME:-$(getent passwd "$USER" | cut -d: -f6)}"

export TCLSH=tclsh8.6

# ~/lib/tcl8.6 und ~/lib/tcltk sind der Standard-TCLLIBPATH
export TCLLIBPATH="$HOME_DIR/lib/tcl8.6 $HOME_DIR/lib/tcltk ${TCLLIBPATH:-}"

echo "=== Tcl 8.6 Umgebung aktiv ==="
echo "TCLSH=$TCLSH"
echo "TCLLIBPATH=$TCLLIBPATH"
echo "Tcl version: $(tclsh8.6 <<< 'puts [info patchlevel]' 2>/dev/null || tclsh <<< 'puts [info patchlevel]')"
echo ""
echo "Kompilieren:"
echo "  make clean && make"
echo "  make check"
echo "  make install"
echo "  -> $HOME_DIR/lib/tcl8.6/tclpdfium0.4/"
