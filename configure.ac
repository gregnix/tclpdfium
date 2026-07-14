#!/bin/bash -norc
dnl	configure.ac für pdfiumtcl
dnl	(nach dem offiziellen sampleextension: https://github.com/tcltk/sampleextension)

AC_INIT([pdfiumtcl],[0.5.3])

TEA_INIT()

AC_CONFIG_AUX_DIR(tclconfig)

TEA_PATH_TCLCONFIG
TEA_LOAD_TCLCONFIG

dnl Tk wird nur fuer render/addimagebitmap gebraucht und zur Laufzeit lazy
dnl initialisiert. Fuers Uebersetzen brauchen wir aber tk.h und die Stubs.
TEA_PATH_TKCONFIG
TEA_LOAD_TKCONFIG

#-----------------------------------------------------------------------
# Tcl und Tk muessen zusammenpassen.
#
# Ohne --with-tcl/--with-tk raet TEA. Auf einem System mit mehreren
# Installationen raet es womoeglich inkonsistent: Tcl 8.6 hier, Tk 9.0 dort.
# Das uebersetzt dann tk.h aus der einen Generation gegen tcl.h aus der
# anderen -- und faellt spaeter an einer Stelle um, die nichts damit zu tun
# hat. Also lieber jetzt abbrechen.
#-----------------------------------------------------------------------

if test "${TCL_VERSION}" != "${TK_VERSION}" ; then

    dnl Einen brauchbaren Vorschlag ausrechnen: dasselbe Verzeichnis (so legt
    dnl es etwa BAWT ab) oder das Geschwisterverzeichnis mit tk statt tcl.
    tk_hint=""
    if test -f "${TCL_BIN_DIR}/tkConfig.sh" ; then
        tk_hint="${TCL_BIN_DIR}"
    else
        tk_sibling=`echo "${TCL_BIN_DIR}" | sed "s|tcl${TCL_VERSION}|tk${TCL_VERSION}|"`
        if test -f "${tk_sibling}/tkConfig.sh" ; then
            tk_hint="${tk_sibling}"
        fi
    fi
    if test x"${tk_hint}" = x ; then
        tk_hint="DIR"
    fi

    AC_MSG_ERROR([Tcl and Tk versions do not match.

    Tcl ${TCL_VERSION}   ${TCL_BIN_DIR}/tclConfig.sh
    Tk  ${TK_VERSION}    ${TK_BIN_DIR}/tkConfig.sh

  Without --with-tcl/--with-tk the search heuristic guesses. On a system with
  several installations it may guess inconsistently -- unversioned symlinks
  such as /usr/lib/tclConfig.sh are a common cause. Name both explicitly:

    ./configure --with-tcl=${TCL_BIN_DIR} --with-tk=${tk_hint}

  Both take the DIRECTORY holding tclConfig.sh resp. tkConfig.sh, not the file.
  List what is available, with versions:

    for f in \$(find / -name 'tclConfig.sh' -o -name 'tkConfig.sh' 2>/dev/null); do
        printf '%-24s %s\\n' "\$(grep -hE '^(TCL|TK)_VERSION=' \$f | head -1)" "\$f"
    done])
fi

TEA_PREFIX
TEA_SETUP_COMPILER

#-----------------------------------------------------------------------
# PDFium: wird nicht gebaut, sondern mitgeliefert.
#
# Es gibt kein TEA-Makro dafuer -- eine vorkompilierte Fremdbibliothek ist
# ausserhalb dessen, was TEA vorsieht. Also von Hand:
#
#   --with-pdfium=DIR   Verzeichnis mit include/ und lib/ (bzw. bin/)
#
# Ohne Angabe wird vendor/pdfium-<plattform>/ gesucht, danach vendor/pdfium/.
#-----------------------------------------------------------------------

AC_ARG_WITH([pdfium],
    AS_HELP_STRING([--with-pdfium=DIR],
                   [PDFium SDK directory (contains include/ and lib/)]),
    [with_pdfium=$withval], [with_pdfium=])

if test x"${with_pdfium}" = x ; then
    # Plattform raten. Der Cross-Build braucht getrennte Verzeichnisse, weil
    # sonst Linux- und Windows-SDK einander ueberschreiben.
    if test "${TEA_PLATFORM}" = "windows" ; then
        pdfium_guess="win-x64"
    else
        pdfium_guess="linux-x64"
    fi
    for d in "${srcdir}/vendor/pdfium-${pdfium_guess}" "${srcdir}/vendor/pdfium" ; do
        if test -f "$d/include/fpdfview.h" ; then
            with_pdfium="$d"
            break
        fi
    done
fi

if test x"${with_pdfium}" = x ; then
    AC_MSG_ERROR([PDFium not found.
    Run scripts/setup.sh (or scripts\\get-pdfium.cmd on Windows),
    or pass --with-pdfium=DIR.])
fi

PDFIUM_DIR=`cd "${with_pdfium}" && pwd`
AC_MSG_CHECKING([for PDFium])

if test ! -f "${PDFIUM_DIR}/include/fpdfview.h" ; then
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([${PDFIUM_DIR}/include/fpdfview.h not found])
fi

#-----------------------------------------------------------------------
# Der Name der Linkbibliothek haengt an der Toolchain, nicht am Betriebssystem:
#
#   Linux           lib/libpdfium.so
#   Windows/MSVC    lib/pdfium.dll.lib
#   Windows/MinGW   lib/libpdfium.dll.a   -- oder ebenfalls pdfium.dll.lib,
#                                            das MinGW klaglos frisst
#
# Also nicht raten, sondern nachsehen.
#-----------------------------------------------------------------------

PDFIUM_LINK=""
for cand in \
        "${PDFIUM_DIR}/lib/libpdfium.so" \
        "${PDFIUM_DIR}/lib/libpdfium.dylib" \
        "${PDFIUM_DIR}/lib/libpdfium.dll.a" \
        "${PDFIUM_DIR}/lib/pdfium.dll.lib" ; do
    if test -f "$cand" ; then
        PDFIUM_LINK="$cand"
        break
    fi
done

if test x"${PDFIUM_LINK}" = x ; then
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([no link library under ${PDFIUM_DIR}/lib])
fi

# Die Laufzeitbibliothek -- die muss spaeter neben die Erweiterung kopiert
# werden, sonst findet der Loader sie nicht.
PDFIUM_RUNTIME=""
for cand in \
        "${PDFIUM_DIR}/lib/libpdfium.so" \
        "${PDFIUM_DIR}/lib/libpdfium.dylib" \
        "${PDFIUM_DIR}/bin/pdfium.dll" ; do
    if test -f "$cand" ; then
        PDFIUM_RUNTIME="$cand"
        break
    fi
done

AC_MSG_RESULT([${PDFIUM_DIR}])
AC_MSG_CHECKING([PDFium link library])
AC_MSG_RESULT([${PDFIUM_LINK}])
AC_MSG_CHECKING([PDFium runtime library])
AC_MSG_RESULT([${PDFIUM_RUNTIME}])

PDFIUM_RUNTIME_NAME=`basename "${PDFIUM_RUNTIME}"`

#-----------------------------------------------------------------------
# Die Fremdbibliothek liegt spaeter NEBEN der Erweiterung. Damit der Loader
# sie dort findet, braucht es unter Unix ein RPATH relativ zur Erweiterung.
# Windows kennt so etwas nicht -- dort durchsucht der Loader von sich aus das
# Verzeichnis der geladenen DLL.
#-----------------------------------------------------------------------

PDFIUM_RPATH=""
if test "${TEA_PLATFORM}" != "windows" ; then
    case "${host_os}" in
        darwin*)
            PDFIUM_RPATH="-Wl,-rpath,@loader_path"
            ;;
        *)
            dnl $ORIGIN muss den Weg durch make und die Shell unbeschadet
            dnl ueberstehen: $$ fuer make, Hochkommata fuer die Shell.
            PDFIUM_RPATH="-Wl,-rpath,'\$\$ORIGIN'"
            ;;
    esac
fi

AC_SUBST(PDFIUM_DIR)
AC_SUBST(PDFIUM_LINK)
AC_SUBST(PDFIUM_RUNTIME)
AC_SUBST(PDFIUM_RUNTIME_NAME)
AC_SUBST(PDFIUM_RPATH)

#-----------------------------------------------------------------------
# Quellen und Flags
#-----------------------------------------------------------------------

TEA_ADD_SOURCES([tclpdfiumtcl.c])
TEA_ADD_HEADERS([])
TEA_ADD_INCLUDES([-I\"${PDFIUM_DIR}/include\"])
TEA_ADD_LIBS([\"${PDFIUM_LINK}\"])
TEA_ADD_CFLAGS([])
TEA_ADD_STUB_SOURCES([])
TEA_ADD_TCL_SOURCES([])

TEA_PUBLIC_TCL_HEADERS
TEA_PUBLIC_TK_HEADERS
TEA_PATH_X

TEA_ENABLE_THREADS
TEA_ENABLE_SHARED
TEA_CONFIG_CFLAGS
TEA_ENABLE_SYMBOLS

AC_DEFINE(USE_TCL_STUBS, 1, [Use Tcl stubs])
AC_DEFINE(USE_TK_STUBS,  1, [Use Tk stubs])

TEA_MAKE_LIB
TEA_LOAD_CONFIG_LIB(TCLSTUB)
TEA_LOAD_CONFIG_LIB(TKSTUB)

TEA_PROG_TCLSH
TEA_PROG_WISH

AC_CONFIG_FILES([Makefile pkgIndex.tcl])
AC_OUTPUT
