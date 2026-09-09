#!/bin/sh
# pruefe-baum.sh -- laedt dieser Ordner das, was man meint?
#
# WOZU: "TCLLIBPATH" auf den Projektordner findet die pkgIndex.tcl in
# build-win-tcl9/ und laedt eine WINDOWS-DLL. Die Meldung lautet dann
# "invalid ELF header" und sagt nicht, woher die Datei kam.
#
# Dasselbe andersherum: ohne gebaute .so im Baum laedt "package
# require" das INSTALLIERTE Paket, und eine gruene Suite sagt nichts
# ueber den Arbeitsbaum. Das steht seit der fuenften Durchsicht offen.
#
# Aufruf:  sh tools/pruefe-baum.sh
set -e
cd "$(dirname "$0")/.."
echo "  Baum: $(pwd)"
# Die Nummer, die der Baum MEINT -- aus configure.ac, nicht geraten.
SOLL=$(sed -n 's/^AC_INIT(\[[^]]*\],\[\([^]]*\)\].*/\1/p' configure.ac)
echo "  configure.ac sagt: $SOLL"
gefunden=0
passend=0
for f in libtcl9pdfiumtcl*.so libpdfiumtcl*.so; do
    [ -f "$f" ] || continue
    gefunden=1
    case "$f" in
        *"$SOLL"*) echo "  Bibliothek: $f" ; passend=1 ;;
        # EINE ALTE BIBLIOTHEK IST SCHLIMMER ALS KEINE.
        #
        # Sie sieht aus wie ein gebauter Baum, und "package require"
        # nimmt sie, wenn ein Index dazu liegt. Der erste Anlauf dieses
        # Skripts nannte sie einfach "Bibliothek" -- und haette den
        # naechsten genauso in die Irre gefuehrt wie die Windows-DLL.
        *) echo "  VERALTET:   $f  (erwartet $SOLL) -- 'make clean'" ;;
    esac
done
[ "$gefunden" = 0 ] && echo "  KEINE Linux-Bibliothek im Baum -- 'make' fehlt"
[ "$gefunden" = 1 ] && [ "$passend" = 0 ] && \
    echo "  KEINE Bibliothek zur Nummer $SOLL -- 'make' fehlt"
[ -f pkgIndex.tcl ] && echo "  pkgIndex.tcl: da" \
                    || echo "  pkgIndex.tcl FEHLT -- 'configure' fehlt"
echo "  weitere pkgIndex.tcl im Baum (Ladefallen):"
find . -name pkgIndex.tcl -not -path ./pkgIndex.tcl | sed 's/^/     /'
echo ""
echo "  Was 'package require' hier faende:"
TCLLIBPATH="$(pwd)" ${TCLSH:-tclsh9.0} <<'TCL' 2>&1 | sed 's/^/     /'
if {[catch {package require pdfiumtcl} e]} {
    puts "FEHLER: $e"
} else {
    puts "pdfiumtcl [package provide pdfiumtcl]"
    puts "geladen aus: [lindex [info loaded] 0]"
}
exit 0
TCL
