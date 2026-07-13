# pkgIndex.tcl -- tclpdfium 0.5.2
#
# Waehlt automatisch das passende Unterverzeichnis:
#   linux64/          Linux  x86_64, Tcl 8.x
#   linux64-tcl9/     Linux  x86_64, Tcl 9.x
#   windows64/        Windows x64,   Tcl 8.x
#   windows64-tcl9/   Windows x64,   Tcl 9.x
#
# Hinweis: pointerSize statt wordSize verwenden --
# auf Windows (MSVC/Magicsplat) ist wordSize=4 auch bei 64-bit Tcl
# (LLP64-Modell), pointerSize=8 ist der zuverlaessige 64-bit Indikator.
#
# ---------------------------------------------------------------------------
# Starpacks / VFS
#
# pdfiumtcl haengt von einer Fremdbibliothek ab (libpdfium.so bzw. pdfium.dll).
# Liegt das Paket in einem virtuellen Dateisystem -- Starpack, Zipkit, zipfs --,
# kopiert Tcl beim "load" NUR die direkt geladene Bibliothek in ein temporaeres
# Verzeichnis. Die abhaengige Bibliothek bleibt im VFS zurueck, und der Loader
# des Betriebssystems kennt das VFS nicht: unter Linux zeigt das einkompilierte
# RPATH $ORIGIN dann ins Temp-Verzeichnis, wo libpdfium.so gerade nicht liegt.
#
# Deshalb packen wir hier BEIDE Dateien selbst aus -- in dasselbe Verzeichnis.
# Danach stimmt $ORIGIN wieder, und der Starpack bleibt eine einzige Datei.
#
# Ueber TCLPDFIUM_TMPDIR laesst sich das Zielverzeichnis umbiegen; noetig etwa,
# wenn /tmp mit "noexec" gemountet ist -- dann verweigert dlopen den Dienst.
# ---------------------------------------------------------------------------

namespace eval ::pdfium {}

# Verzeichnis fuer entpackte Bibliotheken.
proc ::pdfium::_tempdir {} {
    if {[info exists ::env(TCLPDFIUM_TMPDIR)]} {
        return $::env(TCLPDFIUM_TMPDIR)
    }
    foreach v {TMPDIR TEMP TMP} {
        if {[info exists ::env($v)] && [file isdirectory $::env($v)]} {
            return $::env($v)
        }
    }
    if {$::tcl_platform(platform) eq "windows"} { return "C:/Temp" }
    return "/tmp"
}

# Name der Fremdbibliothek auf dieser Plattform.
proc ::pdfium::_vendorlib {} {
    if {$::tcl_platform(platform) eq "windows"} { return "pdfium.dll" }
    return "libpdfium.so"
}

proc ::pdfium::_load {dir subdir version} {
    set ext [file join $dir $subdir pdfiumtcl[info sharedlibextension]]

    # Echtes Dateisystem: direkt laden. RPATH $ORIGIN findet die
    # Fremdbibliothek im selben Verzeichnis.
    if {[lindex [file system $ext] 0] eq "native"} {
        # WICHTIG: auf globaler Ebene laden. Die Erweiterung registriert ihre
        # Kommandos voll qualifiziert; ein "load" aus einem Namespace heraus
        # bliebe damit zwar korrekt, aber wir bleiben auf der sicheren Seite.
        uplevel #0 [list load $ext Pdfiumtcl]
        return
    }

    # VFS: beide Bibliotheken in EIN gemeinsames Verzeichnis auspacken.
    set vendor [_vendorlib]
    set tmp    [file join [_tempdir] tclpdfium-$version-[pid]]

    if {[catch {file mkdir $tmp} err]} {
        return -code error \
            "tclpdfium: kann Entpackverzeichnis $tmp nicht anlegen: $err\
             (TCLPDFIUM_TMPDIR setzen)"
    }

    foreach f [list $vendor pdfiumtcl[info sharedlibextension]] {
        set src [file join $dir $subdir $f]
        set dst [file join $tmp $f]
        if {![file exists $src]} {
            return -code error "tclpdfium: $f fehlt im Paket ($src)"
        }
        # Ein zweiter Interpreter im selben Prozess findet die Datei schon vor.
        if {![file exists $dst]} {
            if {[catch {file copy -force $src $dst} err]} {
                return -code error "tclpdfium: kann $f nicht auspacken: $err"
            }
        }
    }

    if {[catch {
        uplevel #0 [list load [file join $tmp pdfiumtcl[info sharedlibextension]] Pdfiumtcl]
    } err]} {
        return -code error \
            "tclpdfium: Laden aus $tmp fehlgeschlagen: $err\
             (ist das Verzeichnis mit noexec gemountet? Dann TCLPDFIUM_TMPDIR setzen)"
    }
}

apply {{dir} {
    set ver  0.5.2
    set bits 64
    if {$::tcl_platform(pointerSize) == 4} { set bits 32 }

    if {$::tcl_platform(platform) eq "windows"} {
        set subdir "windows${bits}"
    } else {
        set subdir "linux${bits}"
    }
    if {[package vsatisfies [package provide Tcl] 9.0-]} {
        append subdir "-tcl9"
    }

    package ifneeded pdfiumtcl $ver \
        [list ::pdfium::_load $dir $subdir $ver]
}} $dir
