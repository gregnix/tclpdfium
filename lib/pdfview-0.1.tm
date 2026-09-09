# pdfview-0.1.tm -- a PDF page as a Tk widget.
#
#   package require pdfview
#   pdfview .p -file rechnung.pdf
#   pack .p -fill both -expand 1
#   .p configure -page 3 -zoom fit
#
# app/viewer4.tcl is a PROGRAM -- its own window, its own menus, its state
# in one global array touched in 53 places, and widget paths like
# ".pw.left.c" written into the code. It runs, but it cannot be embedded,
# and two of them in one application would overwrite each other, down to
# the photo image, which is called "pdfpage" there.
#
# This is a WIDGET. State belongs to the instance, children hang below
# $win, and the image is named after the object.
#
# TclOO rather than snit: tclpdfium depends on nothing but Tcl and Tk, and
# a snit widget would pull in tcllib for this one file. TclOO is built in
# from 8.6 on.

package require Tk 8.6-
package require pdfiumtcl

package provide pdfview 0.1

namespace eval ::pdfview {
    variable counter 0
}

# The widget command, so that "pdfview .p -file x" reads like every other
# Tk widget and the object command ends up on the widget path.
proc ::pdfview {win args} {
    ::pdfview::View create ::pdfview::tmp$win $win {*}$args
    return $win
}

::oo::class create ::pdfview::View {

    variable win doc total img rendering lastFit opt

    constructor {path args} {
        set win $path
        set doc ""
        set total 0
        set rendering 0
        set lastFit ""

        array set opt {
            -file ""
            -page 0
            -zoom fit
            -dpi 150
            -forms 1
            -background white
            -pagechangedcommand ""
        }

        ttk::frame $win -class Pdfview

        # Named after the object. A fixed name -- as viewer4.tcl uses --
        # would be shared by every view, and the second would overwrite
        # the first.
        set img [image create photo ::pdfview::img[incr ::pdfview::counter]]

        canvas $win.c -highlightthickness 0 -takefocus 1 \
                -background $opt(-background) \
                -xscrollcommand [list $win.hs set] \
                -yscrollcommand [list $win.vs set]
        ttk::scrollbar $win.vs -orient vertical   -command [list $win.c yview]
        ttk::scrollbar $win.hs -orient horizontal -command [list $win.c xview]

        grid $win.c  -row 0 -column 0 -sticky nsew
        grid $win.vs -row 0 -column 1 -sticky ns
        grid $win.hs -row 1 -column 0 -sticky ew
        grid rowconfigure    $win 0 -weight 1
        grid columnconfigure $win 0 -weight 1

        # Put the object on the widget path so ".p configure" reaches it.
        # The frame command keeps a private name, since the object owns
        # the path from here on.
        #
        # This has to happen BEFORE the bindings are made: a binding
        # holds the command name as text, and [self] still answers with
        # the temporary name here. Bound first, the events would call
        # "::pdfview::tmp.p ConfigureEvent" and die with "invalid command
        # name" the moment the widget is mapped.
        rename ::$win ${win}_hull
        rename [self] ::$win

        bind $win.c <Next>     [list $win next]
        bind $win.c <Prior>    [list $win prev]
        bind $win.c <Home>     [list $win see 0]
        bind $win.c <End>      [list $win see end]
        bind $win.c <Button-1> [list focus $win.c]
        bind $win.c <Configure> [list $win ConfigureEvent]

        # The object dies with the widget, otherwise the document handle
        # and the image would leak.
        bind $win <Destroy> [list $win DestroyEvent %W]

        if {[llength $args]} { my configure {*}$args }
    }

    destructor {
        catch {pdfium::close $doc}
        catch {image delete $img}
        catch {rename ${win}_hull ""}
    }

    method DestroyEvent {w} {
        # <Destroy> fires for every child as well; only the widget itself
        # ends the object.
        if {$w ne $win} { return }
        catch {my destroy}
    }

    method cget {option} {
        if {![info exists opt($option)]} {
            # Anything unknown here goes to the frame, which answers for
            # -borderwidth, -relief, -width and friends.
            return [${win}_hull cget $option]
        }
        return $opt($option)
    }

    method configure {args} {
        if {[llength $args] == 0} {
            set out {}
            foreach k [lsort [array names opt]] { lappend out $k $opt($k) }
            return $out
        }
        if {[llength $args] == 1} { return [my cget [lindex $args 0]] }
        if {[llength $args] % 2} {
            error "pdfview: value for \"[lindex $args end]\" missing"
        }

        set needRender 0
        set needNotify 0
        foreach {o v} $args {
            switch -- $o {
                -file {
                    if {$v eq $opt(-file)} { continue }
                    my SetFile $v
                    set needRender 1
                    set needNotify 1
                }
                -page {
                    my SetPage $v
                    set needRender 1
                    set needNotify 1
                }
                -zoom {
                    if {$v ni {fit width} && ![string is double -strict $v]} {
                        error "pdfview: -zoom expects \"fit\", \"width\" or a number, got \"$v\""
                    }
                    set opt(-zoom) $v
                    set lastFit ""
                    set needRender 1
                }
                -forms {
                    if {![string is boolean -strict $v]} {
                        error "pdfview: -forms expects a boolean, got \"$v\""
                    }
                    set opt(-forms) [expr {$v ? 1 : 0}]
                }
                -dpi {
                    if {![string is integer -strict $v] || $v <= 0} {
                        error "pdfview: -dpi expects a positive integer, got \"$v\""
                    }
                    set opt(-dpi) $v
                    set lastFit ""
                    set needRender 1
                }
                -background {
                    set opt(-background) $v
                    $win.c configure -background $v
                }
                -pagechangedcommand {
                    set opt(-pagechangedcommand) $v
                }
                default {
                    ${win}_hull configure $o $v
                }
            }
        }
        if {$needRender} { my Render }
        if {$needNotify} { my Notify }
        return
    }

    method SetFile {value} {
        # Check and open BEFORE closing the current document. An
        # unreadable path used to leave the widget empty although the
        # call failed -- the caller saw an error and lost the page that
        # was on screen.
        set newDoc ""
        if {$value ne ""} {
            if {![file readable $value]} {
                error "pdfview: cannot read \"$value\""
            }
            if {[catch {pdfium::open $value} newDoc]} {
                error "pdfview: cannot open \"$value\": $newDoc"
            }
        }

        catch {pdfium::close $doc}
        set doc ""
        set total 0
        set opt(-file) $value
        set opt(-page) 0
        set lastFit ""
        if {$value eq ""} {
            $win.c delete all
            return
        }
        set doc $newDoc
        set total [pdfium::pagecount $doc]
    }

    method SetPage {value} {
        if {![string is integer -strict $value]} {
            error "pdfview: -page expects an integer, got \"$value\""
        }
        # Clamp to the document. Without one there is nothing to clamp
        # against and the page stays 0 -- reporting page 99 of 0 pages
        # would be a lie.
        if {$total > 0} {
            if {$value < 0}       { set value 0 }
            if {$value >= $total} { set value [expr {$total - 1}] }
        } else {
            set value 0
        }
        set opt(-page) $value
    }

    method pagecount {} { return $total }

    method see {page} {
        if {$page eq "end"} { set page [expr {$total - 1}] }
        my configure -page $page
    }

    method next {} { my see [expr {$opt(-page) + 1}] }
    method prev {} { my see [expr {$opt(-page) - 1}] }

    # The text of a page, for a search box in the application.
    method text {{page ""}} {
        if {$doc eq ""} { return "" }
        if {$page eq ""} { set page $opt(-page) }
        return [pdfium::gettext $doc $page]
    }

    method pagesize {{page ""}} {
        if {$doc eq ""} { return {0 0} }
        if {$page eq ""} { set page $opt(-page) }
        return [pdfium::pagesize $doc $page]
    }

    # The document handle, for everything this widget does not wrap --
    # search, bookmarks, annot_list, formfields, structure.
    method handle {} { return $doc }

    method ConfigureEvent {} {
        if {$opt(-zoom) ni {fit width}} { return }
        set key "[winfo width $win.c]x[winfo height $win.c]"
        if {$key eq $lastFit} { return }
        set lastFit $key
        # after idle: <Configure> arrives repeatedly while a window is
        # dragged, and rendering inside the handler makes it stutter.
        after idle [list $win Render]
    }

    method Render {} {
        if {$doc eq "" || $rendering} { return }
        set rendering 1
        try {
            set page $opt(-page)
            if {$page < 0 || $page >= $total} { return }

            set ropts [my RenderOptions $page]
            # FORMULARE MITZEICHNEN, es sei denn, jemand sagt Nein.
            #
            # Ohne "-forms 1" fehlen die Widget-Anmerkungen: ein
            # eingebettetes Formular kam leer heraus, waehrend
            # app/viewer4.tcl dieselbe Datei mit Feldern zeigte. Wer
            # einbettet, will dasselbe sehen wie im Viewer.
            #
            # Abschaltbar, weil es Geld kostet: die Formularschicht baut
            # eine Umgebung auf und zeichnet ein zweites Mal ueber die
            # Seite.
            if {$opt(-forms)} { lappend ropts -forms 1 }
            if {[catch {pdfium::render $doc $page {*}$ropts -imagename $img} err]} {
                # Not silent: a page that cannot be drawn is a fact the
                # caller wants to see.
                $win.c delete all
                $win.c create text 10 10 -anchor nw \
                        -text "pdfview: $err" -fill red
                return
            }

            $win.c delete all
            $win.c create image 0 0 -anchor nw -image $img
            $win.c configure -scrollregion \
                    [list 0 0 [image width $img] [image height $img]]
        } finally {
            set rendering 0
        }
    }

    method RenderOptions {page} {
        switch -- $opt(-zoom) {
            fit {
                # The window may not be mapped yet, in which case its
                # width is 1 -- fall back to dpi, otherwise the first
                # render is a one-pixel strip.
                set cw [winfo width  $win.c]
                set ch [winfo height $win.c]
                if {$cw <= 1 || $ch <= 1} { return [list -dpi $opt(-dpi)] }
                lassign [pdfium::pagesize $doc $page] pw ph
                if {$pw <= 0 || $ph <= 0} { return [list -dpi $opt(-dpi)] }
                # The smaller ratio decides, so nothing is cut off.
                set sx [expr {double($cw) / $pw}]
                set sy [expr {double($ch) / $ph}]
                set s  [expr {$sx < $sy ? $sx : $sy}]
                return [list -width [expr {int($pw * $s)}]]
            }
            width {
                set cw [winfo width $win.c]
                if {$cw <= 1} { return [list -dpi $opt(-dpi)] }
                return [list -width $cw]
            }
            default {
                # A number is a scale factor, 1.0 being natural size.
                #
                # Computed as a width, not passed as -scale:
                # pdfium::render knows -dpi, -width and -imagename and
                # nothing else, and it ignores an unknown option
                # SILENTLY -- "-scale 0.5" and "-scale 4.0" both gave the
                # same 1240x1754 image, so the zoom appeared dead.
                lassign [pdfium::pagesize $doc $page] pw ph
                if {$pw <= 0} { return [list -dpi $opt(-dpi)] }
                return [list -width [expr {int($pw * $opt(-zoom))}]]
            }
        }
    }

    method Notify {} {
        if {$opt(-pagechangedcommand) eq ""} { return }
        uplevel #0 [list {*}$opt(-pagechangedcommand) $opt(-page) $total]
    }

    # TclOO exports lower-case methods only, so the three that a binding
    # calls have to be exported by hand. They stay capitalised to say
    # they are not part of the interface -- nobody should call
    # "$w ConfigureEvent" from an application.
    export ConfigureEvent DestroyEvent Render
}
