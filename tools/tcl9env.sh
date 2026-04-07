#!/bin/sh
# tcl9env.sh -- Tcl 9.0 Umgebung fuer tclpdfium
#
# Aufruf:
#   . tools/tcl9env.sh          (in aktuelle Shell einlesen)
#   source tools/tcl9env.sh
#
# Danach normal arbeiten:
#   make TCL_VERSION=9.0
#   make install90  -> ~/lib/tcl9.0/tclpdfium0.4/

HOME_DIR="${HOME:-$(getent passwd "$USER" | cut -d: -f6)}"

export TCLSH=tclsh9.0

# ~/lib/tcl9.0 vorne -- tcl9-Pakete werden zuerst gefunden
export TCLLIBPATH="$HOME_DIR/lib/tcl9.0 $HOME_DIR/lib/tcltk ${TCLLIBPATH:-}"

echo "=== Tcl 9.0 Umgebung aktiv ==="
echo "TCLSH=$TCLSH"
echo "TCLLIBPATH=$TCLLIBPATH"
echo "Tcl version: $(tclsh9.0 <<< 'puts [info patchlevel]')"
echo ""
echo "Kompilieren:"
echo "  make clean && make TCL_VERSION=9.0"
echo "  make install90"
echo "  -> $HOME_DIR/lib/tcl9.0/tclpdfium0.4/"
