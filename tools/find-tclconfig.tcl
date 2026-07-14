#!/bin/sh
# Die naechste Zeile startet das Skript mit tclsh neu \
exec tclsh "$0" ${1+"$@"}
#
# find-tclconfig.tcl -- findet tclConfig.sh / tkConfig.sh, paart sie nach
# Version und gibt fertige configure-Zeilen aus.
#
# Laeuft ueberall, wo ein tclsh liegt -- Linux, macOS, Windows.
#
#   tclsh find-tclconfig.tcl [zusaetzliche-suchpfade ...]
#
# Warum das noetig ist: Wer eine TEA-Erweiterung ohne --with-tcl/--with-tk
# konfiguriert, ueberlaesst die Wahl einer Suchheuristik. Auf einem System mit
# mehreren Installationen greift die auch mal inkonsistent daneben -- Tcl 8.6
# hier, Tk 9.0 dort. Uebersetzt wird trotzdem; der Fehler faellt spaeter um.

set roots {}

if {$tcl_platform(platform) eq "windows"} {
    # Windows kennt kein /usr/lib. Die ueblichen Orte sind Installations-
    # verzeichnisse und BAWT-Baeume.
    foreach d {C:/Tcl C:/Tcl9.0.4 C:/ActiveTcl C:/opt/Tcl C:/Bawt
               C:/msys64/mingw64 C:/msys64/ucrt64} {
        if {[file isdirectory $d]} { lappend roots $d }
    }
    # Und das Tcl, in dem wir gerade laufen.
    lappend roots [file dirname [file dirname [info nameofexecutable]]]
} else {
    foreach d {/usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64 /opt
               /Library/Frameworks /System/Library/Frameworks
               /opt/homebrew /opt/local /usr/pkg} {
        if {[file isdirectory $d]} { lappend roots $d }
    }
    if {[info exists env(HOME)]} {
        set h [file join $env(HOME) lib]
        if {[file isdirectory $h]} { lappend roots $h }
    }
}

foreach extra $argv {
    if {[file isdirectory $extra]} { lappend roots $extra }
}

if {[llength $roots] == 0} {
    puts "Keine Suchverzeichnisse gefunden. Pfad als Argument angeben."
    exit 1
}

# Rekursiv suchen. Symlink-Schleifen und unlesbare Verzeichnisse ignorieren.
proc scan {dir depth} {
    if {$depth > 8} { return {} }
    set hits {}
    foreach name {tclConfig.sh tkConfig.sh} {
        set f [file join $dir $name]
        if {[file isfile $f]} { lappend hits $f }
    }
    foreach sub [glob -nocomplain -types d -directory $dir *] {
        if {[catch {file readlink $sub}] == 0} { continue }   ;# Symlink: nicht folgen
        catch { lappend hits {*}[scan $sub [expr {$depth + 1}]] }
    }
    return $hits
}

set files {}
foreach r $roots {
    catch { lappend files {*}[scan $r 0] }
}
set files [lsort -unique $files]

if {[llength $files] == 0} {
    puts "Nichts gefunden."
    puts ""
    puts "Vermutlich fehlt das Entwicklungspaket:"
    puts "  Debian/Ubuntu   apt install tcl-dev tk-dev"
    puts "  Fedora/RHEL     dnf install tcl-devel tk-devel"
    puts "  openSUSE        zypper install tcl-devel tk-devel"
    puts "  Arch            pacman -S tcl tk"
    puts "  macOS/Homebrew  brew install tcl-tk"
    puts "  Windows         die Distribution bringt es mit (lib/tclConfig.sh)"
    exit 1
}

# Version aus der Datei lesen. Debian legt versionslose Weiterleitungen an,
# die die eigentliche Datei nur sourcen -- die haben keine Versionszeile.
proc version {f} {
    if {[catch {open $f r} fh]} { return "" }
    set txt [read $fh]
    close $fh
    if {[regexp {^(?:TCL|TK)_VERSION=['"]?([0-9.]+)} $txt -> v]} { return $v }
    foreach line [split $txt \n] {
        if {[regexp {^(?:TCL|TK)_VERSION=['"]?([0-9.]+)} $line -> v]} { return $v }
    }
    return ""
}

set rows {}
foreach f $files {
    set typ [expr {[string match "*tclConfig.sh" $f] ? "Tcl" : "Tk"}]
    set ver [version $f]
    lappend rows [list $typ $ver [file dirname $f]]
}

puts [format "%-5s %-5s %s" TYP VER VERZEICHNIS]
puts [string repeat - 66]
foreach r [lsort -index 2 $rows] {
    lassign $r typ ver dir
    puts [format "%-5s %-5s %s" $typ [expr {$ver eq "" ? "->" : $ver}] $dir]
}

puts ""
puts "Brauchbare Paare (gleiche Version fuer Tcl und Tk):"
puts [string repeat - 66]

set any 0
foreach r $rows {
    lassign $r typ ver dir
    if {$typ ne "Tcl" || $ver eq ""} { continue }

    # Tk derselben Version suchen. Und zwar NUR im selben Verzeichnis (so legt
    # es Windows und BAWT ab) oder im Geschwisterverzeichnis mit tk statt tcl
    # (so legt es Debian ab).
    #
    # Quer ueber verschiedene Installationsbaeume darf NICHT gepaart werden.
    # Zwei Baeume koennen beide "8.6" sagen und trotzdem verschiedene Builds
    # sein -- Tcl aus dem einen, Tk aus dem anderen ergibt einen Mischling, der
    # uebersetzt und spaeter umfaellt. Genau das ist die Falle, die dieses
    # Werkzeug aufdecken soll; es darf sie nicht selbst stellen.
    set tkdir ""
    foreach s $rows {
        lassign $s t v d
        if {$t eq "Tk" && $v eq $ver && $d eq $dir} { set tkdir $d ; break }
    }
    if {$tkdir eq ""} {
        set sibling [string map [list tcl$ver tk$ver] $dir]
        if {$sibling ne $dir} {
            foreach s $rows {
                lassign $s t v d
                if {$t eq "Tk" && $v eq $ver && $d eq $sibling} {
                    set tkdir $d ; break
                }
            }
        }
    }

    set any 1
    puts ""
    if {$tkdir ne ""} {
        puts "  Tcl/Tk $ver"
        puts "    ./configure --with-tcl=$dir \\"
        puts "                --with-tk=$tkdir"
    } else {
        puts "  Tcl $ver in $dir -- kein Tk daneben."
        puts "    Ohne Tk:  --with-tcl=$dir"
        puts "    Mit Tk:   ein Tk aus DEMSELBEN Baum waehlen, nicht aus einem anderen."
    }
}

if {!$any} {
    puts "  Keine -- nur Weiterleitungen gefunden?"
}

puts ""
puts "Die versionslosen Eintraege (VER = ->) NICHT verwenden: das sind"
puts "Weiterleitungen, und die fuer Tcl und die fuer Tk muessen nicht auf"
puts "dieselbe Generation zeigen. Genau daran scheitern die meisten Builds."
