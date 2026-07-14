# test-vfs.tcl -- Laedt tclpdfium aus einem zipfs-VFS?  (Tcl 9)
#
# Das ist der Punkt, den test-windows.tcl nicht pruefen konnte: build-tclpdfium-
# bawt.bat legt einen FLACHEN pkgIndex.tcl an (ein load, sonst nichts). Der
# VFS-faehige pkgIndex.tcl liegt im Repo-Wurzelverzeichnis und erwartet die
# Struktur mit Unterverzeichnissen. Dieses Skript baut sie selbst auf.
#
# Aufruf im Repo-Wurzelverzeichnis:
#
#   c:\Tcl9.0.4\bin\tclsh.exe test-vfs.tcl libs\windows-tcl9.0\tclpdfium test.pdf
#
# Die Frage dahinter: Tcl kopiert beim Laden aus einem VFS nur die Erweiterung
# selbst ins Temp-Verzeichnis, nicht ihre Abhaengigkeit. Der pkgIndex.tcl packt
# deshalb BEIDE DLLs dorthin aus. Findet pdfiumtcl.dll die pdfium.dll dann?
# Unter Linux ja ($ORIGIN). Unter Windows entscheidet die DLL-Suchreihenfolge --
# das ist hier zu klaeren.

set dllDir  [lindex $argv 0]
set pdfFile [lindex $argv 1]

if {$dllDir eq "" || $pdfFile eq ""} {
    puts "Aufruf: tclsh test-vfs.tcl <verzeichnis-mit-den-dlls> <test.pdf>"
    exit 2
}
set dllDir  [file normalize $dllDir]
set pdfFile [file normalize $pdfFile]

proc fail {msg} { puts "\nFEHLGESCHLAGEN: $msg" ; exit 1 }

puts "tclpdfium VFS-Test"
puts "  Tcl     : [info patchlevel]"
puts "  DLLs    : $dllDir"
puts ""

if {[llength [info commands zipfs]] == 0} {
    puts "zipfs nicht vorhanden -- dieser Test braucht Tcl 9."
    exit 2
}

# ---------------------------------------------------------------- Vorbereiten
# Version aus dem pkgIndex.tcl des Repos ziehen.
set repoIndex [file join [file dirname [info script]] pkgIndex.tcl]
if {![file exists $repoIndex]} {
    fail "pkgIndex.tcl nicht gefunden neben diesem Skript: $repoIndex"
}
set fh [open $repoIndex r]; set idxText [read $fh]; close $fh
if {![regexp {set ver\s+([0-9.]+)} $idxText -> ver]} {
    fail "Version aus $repoIndex nicht lesbar"
}
if {![string match "*file system*" $idxText]} {
    fail "$repoIndex ist nicht die VFS-faehige Fassung (kein 'file system' darin)"
}
puts "1) Der VFS-faehige pkgIndex.tcl, Version $ver"
puts "   $repoIndex"

# Paketbaum in der erwarteten Struktur aufbauen
set tmp  [file join [file dirname [file normalize $repoIndex]] _vfstest]
set tree [file join $tmp tclpdfium$ver]
set sub  [file join $tree windows64-tcl9]
file delete -force $tmp
file mkdir $sub

foreach f {pdfiumtcl.dll pdfium.dll} {
    set src [file join $dllDir $f]
    if {![file exists $src]} { fail "$f fehlt in $dllDir" }
    file copy $src [file join $sub $f]
}
file copy $repoIndex [file join $tree pkgIndex.tcl]

puts ""
puts "2) Paketbaum aufgebaut"
foreach f [lsort [glob -nocomplain -directory $tmp -tails */* */*/*]] {
    puts "   $f"
}

# ------------------------------------------------------------------- Zip bauen
set zip [file join [file dirname $tmp] pkg.zip]
file delete -force $zip
# Das dritte Argument ist der abzuschneidende Praefix. Ohne ihn landet der
# ganze absolute Pfad im Archiv, und im Mount taucht dann "home" auf statt
# "tclpdfium<ver>".
if {[catch {zipfs mkzip $zip $tmp $tmp} e]} {
    fail "zipfs mkzip: $e"
}
puts ""
puts "3) Zip gebaut: [file size $zip] Bytes"

# ------------------------------------------- Unterprozess: aus dem VFS laden
set sub2 [file join $tmp load.tcl]
set fh [open $sub2 w]
puts $fh [string map [list @ZIP@ $zip @PDF@ $pdfFile @VER@ $ver] {
    set zip {@ZIP@}
    set mnt //zipfs:/pkgtest

    # Die Argumentreihenfolge von "zipfs mount" hat zwischen den Tcl-Versionen
    # gewechselt. Beide probieren und melden, welche greift.
    set order ""
    if {![catch {zipfs mount $zip $mnt}]} {
        set order "mount ZIPFILE MOUNTPOINT"
    } elseif {![catch {zipfs mount $mnt $zip}]} {
        set order "mount MOUNTPOINT ZIPFILE"
    } else {
        puts "MOUNT-FEHLER"
        exit 3
    }
    puts "ORDER $order"

    set inhalt [glob -nocomplain -directory $mnt -tails *]
    puts "MOUNT-INHALT $inhalt"
    if {[llength $inhalt] == 0} {
        puts "LEER -- der Mountpoint zeigt auf nichts"
        exit 4
    }

    # auto_path anzufassen ist heikel (Tcl 9 haengt seinen package-unknown-
    # Mechanismus daran) und auch unnoetig: wir wollen genau EINEN bestimmten
    # pkgIndex.tcl pruefen, nicht die Suche danach. Also direkt sourcen.
    #
    # $dir ist die Variable, die ein pkgIndex.tcl erwartet -- normalerweise
    # setzt tclPkgUnknown sie.
    catch {package forget pdfiumtcl}
    set dir [file join $mnt tclpdfium@VER@]
    if {[catch {source [file join $dir pkgIndex.tcl]} e]} {
        puts "PKGINDEX-FEHLER $e"
        exit 5
    }
    if {[catch {package require pdfiumtcl @VER@} e]} {
        puts "REQUIRE-FEHLER $e"
        exit 5
    }
    set from ""
    foreach l [info loaded] {
        if {[lindex $l 1] eq "Pdfiumtcl"} { set from [lindex $l 0] }
    }
    puts "GELADEN-AUS $from"
    # Der VFS-faehige pkgIndex packt nach <temp>/tclpdfium-<ver>-<pid>/ aus.
    if {![string match "*tclpdfium-@VER@-*" $from]} {
        puts "FALSCHE-QUELLE -- nicht aus dem Auspackverzeichnis"
        exit 6
    }
    set doc [pdfium::open {@PDF@}]
    puts "SEITEN [pdfium::pagecount $doc]"
    pdfium::close $doc
    puts "OK"
    exit 0
}]
close $fh

puts ""
puts "4) Laden aus dem VFS (frischer Interpreter)"
set rc [catch {exec [info nameofexecutable] $sub2} out]
foreach line [split $out \n] {
    puts "   $line"
}

puts ""
puts "----------------------------------------------------------"
if {$rc == 0 && [string match "*OK*" $out]} {
    puts "  ERFOLG: das Paket laedt aus dem VFS."
    puts "  Beide DLLs wurden ins Temp ausgepackt und haben sich gefunden."
    puts "  -> Ein-Datei-Starpack ist unter Windows moeglich."
} else {
    puts "  FEHLGESCHLAGEN."
    puts "  Sagt die Meldung 'pdfium.dll' oder 'kann Bibliothek nicht laden',"
    puts "  dann findet pdfiumtcl.dll ihre Abhaengigkeit im Temp NICHT --"
    puts "  Windows sucht dort nicht. Dann bleibt: pdfium.dll neben die .exe,"
    puts "  und ein Ein-Datei-Starpack ist unter Windows nicht zu haben."
}
puts ""
puts "  Aufraeumen: rmdir /S /Q $tmp"
exit [expr {$rc == 0 ? 0 : 1}]
