/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Gregor Ebbing
 *
 * pdfiumtcl.c  --  Minimales PDFium-Binding für Tcl/Tk
 *
 * Kompilieren:
 *   gcc -shared -fPIC -o pdfiumtcl.so pdfiumtcl.c \
 *       -I/opt/pdfium/include \
 *       -L/opt/pdfium/lib -lpdfium \
 *       $(pkg-config --cflags --libs tcl tk)
 *
 * Voraussetzungen:
 *   - pdfium-linux-x64.tgz entpackt nach /opt/pdfium
 *   - tcl-dev und tk-dev installiert
 *
 * Tcl-Befehle nach "package require pdfiumtcl":
 *
 *   pdfium::open   filename ?password?  -> doc-handle
 *   pdfium::close  doc-handle
 *   pdfium::pagecount doc-handle        -> integer
 *   pdfium::render doc-handle pagenum ?-dpi 150? ?-imagename myimg?
 *                                       -> image-name (Tk photo)
 *   pdfium::gettext doc-handle pagenum  -> string
 *
 * Write / edit (0.4):
 *   pdfium::newdoc                              -> doc-handle (empty)
 *   pdfium::newpage doc index width height      -> page-handle (points)
 *   pdfium::closepage page
 *   pdfium::generatecontent page                -> 0/1
 *   pdfium::importpages dest src ?range? ?index? -> 0/1   (range "1,3,5-7")
 *   pdfium::setcropbox  doc pageindex l b r t   -> 1      (points)
 *   pdfium::setmediabox doc pageindex l b r t   -> 1      (points)
 *   pdfium::addimagejpeg page doc jpeg x y w h  -> 0/1    (points)
 *   pdfium::addimagebitmap page doc photo x y w h -> 0/1  (points, lossless)
 *   pdfium::deletepage doc index                -> 1
 *   pdfium::setrotation doc index degrees        -> 0/1   (0|90|180|270)
 *   pdfium::save doc filename ?flags?           -> 0/1
 *   pdfium::savewithversion doc filename version ?flags? -> 0/1
 */

/* localtime_r VERLANGT EIN POSIX-MERKMAL.
 *
 * Unter "gcc -std=c11 -Wall -Wextra" meldet der Uebersetzer
 * "implicit declaration of function 'localtime_r'" -- die Voreinstellung
 * gnu11 verdeckt das. Ein implizit erklaerter Funktionsaufruf ist in
 * C99 und spaeter kein Warnhinweis, sondern ein Fehler, den nur die
 * Nachsicht des Uebersetzers durchgehen laesst.
 *
 * 200809L ist POSIX.1-2008 und deckt localtime_r. Vor JEDEM include,
 * sonst ist die Kopfdatei schon gelesen.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <tcl.h>
#include <tk.h>
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_doc.h>
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdf_ppo.h>
#include <fpdf_transformpage.h>
#include <fpdf_structtree.h>
#include <fpdf_flatten.h>
#include <fpdf_formfill.h>
#include <fpdf_catalog.h>
#include <fpdf_attachment.h>
#include <fpdf_signature.h>
#include <fpdf_fwlevent.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Windows-Druck (GDI/DEVMODE): winspool fuer EnumPrinters und
 * DocumentProperties, wchar.h fuer _wcsicmp. */
#ifdef _WIN32
#include <windows.h>
#include <winspool.h>
#include <wchar.h>
#endif

/* Tcl_Size: ab Tcl 9 definiert, fuer Tcl 8 als int.
 *
 * Die zweite Bedingung ist nicht ueberfluessig: TEA reicht bei einem Tcl-8-Bau
 * ein -DTcl_Size=int auf der Kommandozeile herein. Ohne die Abfrage expandiert
 * der typedef unten zu "typedef int int;" und der Uebersetzer bricht ab. */
#if !defined(TCL_SIZE_MAX) && !defined(Tcl_Size)
    typedef int Tcl_Size;
#endif

/* Name und Version kommen von configure. TEA definiert beide auf der
 * Befehlszeile (@DEFS@ enthaelt -DPACKAGE_VERSION="..."), so dass die
 * Nummer NUR in configure.ac steht.
 *
 * Vorher stand sie hier ein zweites Mal, fest verdrahtet -- bei jedem
 * Bump muss man dann an beide denken, und wer eine vergisst, bekommt
 * beim Laden:
 *   attempt to provide package pdfiumtcl 0.6.0 failed:
 *   package pdfiumtcl 0.6.1 provided instead
 *
 * Der Rueckfall gilt nur beim Uebersetzen von Hand ohne configure. Er
 * ist bewusst auffaellig, damit eine so gebaute Bibliothek nicht
 * unbemerkt eine falsche Nummer meldet.
 */
#ifndef PACKAGE_NAME
#  define PACKAGE_NAME "pdfiumtcl"
#endif
#ifndef PACKAGE_VERSION
#  define PACKAGE_VERSION "0.0.0-nonconfigured"
#endif

/* Windows DLL-Export — noetig fuer MinGW ohne --export-all-symbols */
#ifdef _WIN32
#  define PDFIUMTCL_EXPORT __declspec(dllexport)
#else
#  define PDFIUMTCL_EXPORT
#endif

/* Vorwaerts: pdfium::close steht weiter oben als die Tippsitzung und
 * muss ihre Sitzungen trotzdem abbauen koennen. */
static void _EditCloseAllFor(FPDF_DOCUMENT doc);

/* ------------------------------------------------------------------ */
/* EINE FORMULARUMGEBUNG JE DOKUMENT                                   */
/*                                                                     */
/* Bis 0.6.4 baute jeder Aufruf -- render, formfields, formfill,        */
/* flatten -- seine eigene auf und wieder ab. Fuer eine Auskunft ist    */
/* das nur verschwenderisch; sobald aber eine Tippsitzung laeuft, ist   */
/* es falsch: PDFium vertraegt nur EINE je Dokument, und die zweite     */
/* sieht die Felder nicht. Gemessen 07.09.2026 -- formfill meldete      */
/* "could not fill", obwohl der Name stimmte.                           */
/*                                                                     */
/* Jetzt gehoert sie dem Dokument. Wer sie braucht, holt sie; gebaut    */
/* wird sie beim ersten Mal, abgebaut von pdfium::close.                */
/*                                                                     */
/* Die Rueckrufstruktur liegt als ERSTES Feld: PDFium ruft mit ihrem    */
/* Zeiger, und ein Umdeuten fuehrt zurueck.                             */
/* ------------------------------------------------------------------ */
#define PDFIUM_MAX_DOCFORM 32

typedef struct PdfiumDocForm {
    FPDF_FORMFILLINFO ffi;      /* MUSS das erste Feld sein */
    FPDF_DOCUMENT     doc;
    FPDF_FORMHANDLE   form;
    FPDF_PAGE         curPage;  /* die Seite, die gerade offen ist */
    int               curPageNum;
    /* DIE SEITE DER TIPPSITZUNG.
     *
     * Jeder Form-Befehl laedt sich seine EIGENE Seite -- ein zweites
     * Objekt derselben Seite -- und trug sie bisher in curPage ein.
     * Danach meldete er sie ab und setzte curPage auf NULL. Waehrend
     * einer Sitzung war das verheerend: FFI_GetPage gab NULL zurueck,
     * PDFium fand die Seite nicht mehr, und editrender zeichnete die
     * Feldinhalte nicht.
     *
     * Gemeldet 08.09.2026 mit sieben Bildschirmfotos: die Feldliste
     * zeigte "muster" und "company", auf dem Blatt blieben Name und
     * Firma LEER -- bis die Combobox benutzt wurde, denn die beendet
     * die Sitzung und setzt sie neu auf.
     *
     * Solange hier eine Seite steht, gehoert curPage ihr. */
    FPDF_PAGE         sessionPage;
    int               sessionPageNum;
    int               dirty;
    double            dirtyL, dirtyT, dirtyR, dirtyB;
    int               cursor;
    int               changed;
    int               timerId;
} PdfiumDocForm;

static PdfiumDocForm *pdfiumDocForms[PDFIUM_MAX_DOCFORM];
static int pdfiumDocFormCount = 0;

/* Die Rueckrufe. Sie merken sich, was PDFium meldet, und tun sonst
 * nichts -- ein Zeitgeber, der wirklich blinkt, braucht die
 * Ereignisschleife der Anwendung. */
static void
_DfInvalidate(FPDF_FORMFILLINFO *info, FPDF_PAGE page,
              double left, double top, double right, double bottom)
{
    PdfiumDocForm *f = (PdfiumDocForm *)info;
    (void)page;
    f->dirty = 1;
    f->dirtyL = left; f->dirtyT = top;
    f->dirtyR = right; f->dirtyB = bottom;
}

static void
_DfSetCursor(FPDF_FORMFILLINFO *info, int t)
{
    ((PdfiumDocForm *)info)->cursor = t;
}

static int
_DfSetTimer(FPDF_FORMFILLINFO *info, int ms, TimerCallback cb)
{
    /* Null waere ein Fehlschlag fuer PDFium -- also eine Nummer. */
    (void)ms; (void)cb;
    return ++((PdfiumDocForm *)info)->timerId;
}

static void
_DfKillTimer(FPDF_FORMFILLINFO *info, int id) { (void)info; (void)id; }

static FPDF_SYSTEMTIME
_DfGetLocalTime(FPDF_FORMFILLINFO *info)
{
    (void)info;
    FPDF_SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    time_t jetzt = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &jetzt);
#else
    localtime_r(&jetzt, &tmv);
#endif
    st.wYear = (unsigned short)(tmv.tm_year + 1900);
    st.wMonth = (unsigned short)(tmv.tm_mon + 1);
    st.wDayOfWeek = (unsigned short)tmv.tm_wday;
    st.wDay = (unsigned short)tmv.tm_mday;
    st.wHour = (unsigned short)tmv.tm_hour;
    st.wMinute = (unsigned short)tmv.tm_min;
    st.wSecond = (unsigned short)tmv.tm_sec;
    return st;
}

static FPDF_PAGE
_DfGetPage(FPDF_FORMFILLINFO *info, FPDF_DOCUMENT doc, int idx)
{
    PdfiumDocForm *f = (PdfiumDocForm *)info;
    /* Nur die Seite herausgeben, die ohnehin offen ist. Eine weitere zu
     * laden hiesse, sie auch schliessen zu muessen -- und wer das
     * vergisst, haelt sie bis zum Programmende. */
    if (doc == f->doc && idx == f->curPageNum) return f->curPage;
    return NULL;
}

static int
_DfGetRotation(FPDF_FORMFILLINFO *info, FPDF_PAGE p)
{ (void)info; (void)p; return 0; }

static void
_DfNamedAction(FPDF_FORMFILLINFO *info, FPDF_BYTESTRING a)
{ (void)info; (void)a; }

static FPDF_PAGE
_DfGetCurrentPage(FPDF_FORMFILLINFO *info, FPDF_DOCUMENT doc)
{
    PdfiumDocForm *f = (PdfiumDocForm *)info;
    return (doc == f->doc) ? f->curPage : NULL;
}

static void
_DfOnChange(FPDF_FORMFILLINFO *info)
{ ((PdfiumDocForm *)info)->changed = 1; }

/* Die Umgebung eines Dokuments holen, beim ersten Mal bauen.
 *
 * SEITE UND NUMMER MUESSEN MIT.
 *
 * PDFium fragt schon WAEHREND InitFormFillEnvironment ueber FFI_GetPage
 * nach der Seite. Steht dort noch NULL, kommt die Umgebung halb
 * aufgebaut heraus, und das naechste FPDF_FFLDraw stuerzt ab.
 *
 * Gemessen 08.09.2026: "editbegin" gefolgt von "editrender" endete im
 * Segmentierungsfehler -- aber nur, wenn editbegin die Umgebung baute.
 * Lief vorher ein "render -forms 1", ging alles. Der Unterschied war
 * nicht der Zeichenweg, sondern WER die Umgebung aufbaut und was er
 * dabei ueber die Seite sagen kann.
 */
static PdfiumDocForm *
_DocFormGet(FPDF_DOCUMENT doc, FPDF_PAGE page, int pagenum)
{
    for (int i = 0; i < pdfiumDocFormCount; i++) {
        if (pdfiumDocForms[i]->doc != doc) continue;
        /* Die Seite NUR nachziehen, wenn keine Sitzung sie haelt.
         * Sonst zeigte FFI_GetPage auf ein Seitenobjekt, das der
         * Aufrufer gleich wieder schliesst. */
        if (!pdfiumDocForms[i]->sessionPage) {
            pdfiumDocForms[i]->curPage = page;
            pdfiumDocForms[i]->curPageNum = pagenum;
        }
        return pdfiumDocForms[i];
    }
    if (pdfiumDocFormCount >= PDFIUM_MAX_DOCFORM) return NULL;
    PdfiumDocForm *f = (PdfiumDocForm *)ckalloc(sizeof(PdfiumDocForm));
    memset(f, 0, sizeof(*f));
    f->doc = doc;
    /* VOR dem Aufbau setzen -- siehe oben. */
    f->curPage = page;
    f->curPageNum = pagenum;
    f->ffi.version = 1;
    f->ffi.FFI_Invalidate         = _DfInvalidate;
    f->ffi.FFI_SetCursor          = _DfSetCursor;
    f->ffi.FFI_SetTimer           = _DfSetTimer;
    f->ffi.FFI_KillTimer          = _DfKillTimer;
    f->ffi.FFI_GetLocalTime       = _DfGetLocalTime;
    f->ffi.FFI_GetPage            = _DfGetPage;
    f->ffi.FFI_GetRotation        = _DfGetRotation;
    f->ffi.FFI_ExecuteNamedAction = _DfNamedAction;
    f->ffi.FFI_GetCurrentPage     = _DfGetCurrentPage;
    f->ffi.FFI_OnChange           = _DfOnChange;
    f->form = FPDFDOC_InitFormFillEnvironment(doc, &f->ffi);
    if (!f->form) { ckfree((char *)f); return NULL; }
    pdfiumDocForms[pdfiumDocFormCount++] = f;
    return f;
}

/* Beim Schliessen des Dokuments. */
/* Nach dem Abmelden einer transienten Seite: die Sitzungsseite wieder
 * eintragen, sonst gar keine.
 *
 * Stand an fuenf Stellen wortgleich. Eine Regel an fuenf Stellen ist
 * eine Regel, die an vier Stellen vergessen wird -- und genau das war
 * der Fehler, den sie behebt.
 */
static void
_DocFormSeiteAb(PdfiumDocForm *f)
{
    if (!f) return;
    f->curPage = f->sessionPage;
    f->curPageNum = f->sessionPage ? f->sessionPageNum : -1;
}

static void
_DocFormFree(FPDF_DOCUMENT doc)
{
    for (int i = 0; i < pdfiumDocFormCount; i++) {
        if (pdfiumDocForms[i]->doc != doc) continue;
        FPDFDOC_ExitFormFillEnvironment(pdfiumDocForms[i]->form);
        ckfree((char *)pdfiumDocForms[i]);
        pdfiumDocForms[i] = pdfiumDocForms[--pdfiumDocFormCount];
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Hilfsmakro: Fehler setzen und TCL_ERROR zurückgeben                 */
/* ------------------------------------------------------------------ */
#define PDFIUM_ERROR(interp, msg) \
    do { Tcl_SetObjResult(interp, \
         Tcl_NewStringObj((msg), -1)); return TCL_ERROR; } while(0)

/* ------------------------------------------------------------------ */
/* pdfium::open filename ?password?                                    */
/* Gibt einen Zeiger als breiten Integer zurück (doc-handle).          */
/* ------------------------------------------------------------------ */
static int
PdfiumOpenCmd(ClientData cd, Tcl_Interp *interp,
              int objc, Tcl_Obj *const objv[])
{
    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "filename ?password?");
        return TCL_ERROR;
    }

    const char *filename = Tcl_GetString(objv[1]);
    const char *password = (objc == 3) ? Tcl_GetString(objv[2]) : NULL;

    FPDF_DOCUMENT doc = FPDF_LoadDocument(filename, password);
    if (!doc) {
        unsigned long err = FPDF_GetLastError();
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "cannot open PDF '%s' (PDFium error %lu)", filename, err);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        return TCL_ERROR;
    }

    /* Zeiger als WideInt zurückgeben – wird als Handle benutzt */
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)(intptr_t)doc));
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::close doc-handle                                            */
/* ------------------------------------------------------------------ */
static int
PdfiumCloseCmd(ClientData cd, Tcl_Interp *interp,
               int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    /* OFFENE TIPPSITZUNGEN ZUERST BEENDEN.
     *
     * Ohne das zeigten Sitzung, Formularumgebung und Seite auf ein
     * freigegebenes Dokument. Gemessen 08.09.2026: "editchar" danach
     * lief DURCH -- kein Fehler, kein Absturz. Ein Fehler, der nicht
     * auffaellt und irgendwann woanders zuschlaegt.
     *
     * Beenden statt ablehnen: wer ein Dokument schliesst, will es los
     * sein. Die Sitzung merkt sich, dass sie tot ist, und ein spaeterer
     * Aufruf sagt das. */
    _EditCloseAllFor((FPDF_DOCUMENT)(intptr_t)ptr);
    /* UND DIE FORMULARUMGEBUNG DES DOKUMENTS.
     *
     * Sie zu vergessen ist teurer als ein Leck: PDFium vergibt fuer ein
     * neues Dokument gern DIESELBE Adresse, und dann findet
     * _DocFormGet den alten Eintrag -- eine Umgebung, die auf ein
     * freigegebenes Dokument zeigt.
     *
     * Gemessen 08.09.2026: der erste Durchgang tippte, der zweite auf
     * einer frisch geoeffneten Datei nicht. Der Klick meldete einen
     * Treffer, und es kam nichts an. */
    _DocFormFree((FPDF_DOCUMENT)(intptr_t)ptr);
    FPDF_CloseDocument((FPDF_DOCUMENT)(intptr_t)ptr);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::pagecount doc-handle                                        */
/* ------------------------------------------------------------------ */
static int
PdfiumPageCountCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int n = FPDF_GetPageCount((FPDF_DOCUMENT)(intptr_t)ptr);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(n));
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::render doc-handle pagenum                                   */
/*     ?-dpi n? ?-width px? ?-imagename name?                         */
/* -width px: Zielbreite in Pixeln; DPI wird automatisch berechnet.   */
/* -dpi und -width schließen sich aus; -width hat Vorrang.            */
/* ------------------------------------------------------------------ */
/* Vorwaerts-Deklaration: portable UTF-16LE -> Tcl_Obj (Definition weiter unten). */
static Tcl_Obj *_AnnotUtf16ToObj(Tcl_Interp *interp, unsigned short *buf, unsigned long bytelen);

/* Tk nur fuer render: Stubs lazy initialisieren, damit das Laden kein Tk zieht. */
static int
EnsureTk(Tcl_Interp *interp)
{
#if TCL_MAJOR_VERSION >= 9
    if (Tk_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
#else
    if (Tk_InitStubs(interp, "8.5", 0) == NULL) return TCL_ERROR;
#endif
    return TCL_OK;
}

static int
PdfiumRenderCmd(ClientData cd, Tcl_Interp *interp,
                int objc, Tcl_Obj *const objv[])
{
    if (EnsureTk(interp) != TCL_OK) return TCL_ERROR;
    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv,
                         "doc-handle pagenum ?-dpi n? ?-width px?"
                         " ?-imagename name? ?-clip {l u r o}? ?-printing 0|1?");
        return TCL_ERROR;
    }

    /* Pflichtargumente */
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    /* Optionale Argumente */
    int dpi       = 150;
    int target_w  = 0;   /* 0 = nicht gesetzt */
    int haveClip  = 0;
    double clipL = 0, clipU = 0, clipR = 0, clipO = 0;
    int printing  = 0;
    int withForms = 0;
    char imgname[64];
    snprintf(imgname, sizeof(imgname), "pdfimg%d", pagenum);

    /* An odd number of trailing words means a value is missing. Silently
     * dropping the last word hides a typo in the caller. */
    if (((objc - 3) % 2) != 0) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("value for \"%s\" missing",
                Tcl_GetString(objv[objc - 1])));
        return TCL_ERROR;
    }

    for (int i = 3; i < objc - 1; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        if (strcmp(opt, "-dpi") == 0) {
            if (Tcl_GetIntFromObj(interp, objv[i+1], &dpi) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-width") == 0) {
            if (Tcl_GetIntFromObj(interp, objv[i+1], &target_w) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-imagename") == 0) {
            strncpy(imgname, Tcl_GetString(objv[i+1]), sizeof(imgname)-1);
        } else if (strcmp(opt, "-clip") == 0) {
            /* Nur den Ausschnitt {links unten rechts oben} rendern, in
             * PUNKT und Seitenkoordinaten -- dieselben Zahlen, die
             * "search -rects 1" liefert. Ohne das muss jeder Zoom die
             * GANZE Seite bauen; bei einer A0-Zeichnung ist das der
             * Unterschied zwischen fluessig und unbrauchbar. */
            Tcl_Obj **rv; Tcl_Size rc;
            if (Tcl_ListObjGetElements(interp, objv[i+1], &rc, &rv) != TCL_OK)
                return TCL_ERROR;
            if (rc != 4) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(
                    "-clip needs {left bottom right top} in points", -1));
                return TCL_ERROR;
            }
            if (Tcl_GetDoubleFromObj(interp, rv[0], &clipL) != TCL_OK ||
                Tcl_GetDoubleFromObj(interp, rv[1], &clipU) != TCL_OK ||
                Tcl_GetDoubleFromObj(interp, rv[2], &clipR) != TCL_OK ||
                Tcl_GetDoubleFromObj(interp, rv[3], &clipO) != TCL_OK)
                return TCL_ERROR;
            if (clipR <= clipL || clipO <= clipU) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(
                    "-clip: right must exceed left and top must exceed bottom",
                    -1));
                return TCL_ERROR;
            }
            haveClip = 1;
        } else if (strcmp(opt, "-forms") == 0) {
            /* Formularfelder MITZEICHNEN.
             *
             * Ohne das fehlen sie im Bild: pdfium zeichnet Widget-
             * Annotationen nicht mit FPDF_RenderPageBitmap, sondern
             * ueber die Formularschicht (FPDF_FFLDraw). Gemessen an
             * tests/fixtures/form.pdf -- "formfields" meldete
             * "Muster GmbH", im Bild stand nur "Name:".
             *
             * Fuers ZEICHNEN reicht eine leere FPDF_FORMFILLINFO mit
             * version 1: die Rueckrufe darin sind fuer Eingaben da, und
             * die gibt es hier nicht. Ausfuellen ist etwas anderes und
             * braucht die ganze Umgebung. */
            if (Tcl_GetIntFromObj(interp, objv[i+1], &withForms) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-printing") == 0) {
            /* FPDF_PRINTING (0x800): rendern, wie ein Drucker es taete.
             * Damit laesst sich MESSEN, ob eine Ebene mit
             * /Usage /Print /PrintState /OFF beim Drucken wegbleibt --
             * bisher konnte man das nur glauben. */
            if (Tcl_GetIntFromObj(interp, objv[i+1], &printing) != TCL_OK)
                return TCL_ERROR;
        } else {
            /* Say so instead of ignoring it. An unknown option used to
             * pass unnoticed: "render -scale 2.0" -- an option that only
             * pdfium::print has -- produced the same image for every
             * factor, and the caller had no way of telling why the zoom
             * did nothing. */
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown option \"%s\": must be -dpi, -width,"
                              " -imagename, -clip, -printing or -forms", opt));
            return TCL_ERROR;
        }
    }

    /* Unvertraegliche Optionen PRUEFEN, BEVOR etwas belegt ist.
     *
     * FPDF_FFLDraw kennt keine Matrix; fuer einen Ausschnitt MIT
     * Formularfeldern gibt es ueber diese Schnittstelle keinen Weg.
     * Lieber sagen als still das Falsche zeichnen.
     *
     * Der erste Versuch meldete das mitten im Renderteil und gab dabei
     * Bitmap und Seite frei -- der normale Weg danach tat es noch
     * einmal, und das Ergebnis war ein Speicherzugriffsfehler. Eine
     * Pruefung, die aufraeumen muss, steht an der falschen Stelle. */
    if (haveClip && withForms) {
        PDFIUM_ERROR(interp,
            "-forms and -clip cannot be combined (FPDF_FFLDraw takes no"
            " matrix)");
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    /* Seitengröße in Punkten -- oder die des Ausschnitts */
    double page_w = FPDF_GetPageWidth(page);
    double page_h = FPDF_GetPageHeight(page);
    if (haveClip) {
        /* Ausserhalb des Blattes abschneiden statt leere Flaeche zu
         * rendern: ein Rechteck aus einer Suche kann am Rand liegen. */
        if (clipL < 0) clipL = 0;
        if (clipU < 0) clipU = 0;
        if (clipR > page_w) clipR = page_w;
        if (clipO > page_h) clipO = page_h;
        if (clipR <= clipL || clipO <= clipU) {
            FPDF_ClosePage(page);
            PDFIUM_ERROR(interp, "-clip lies outside the page");
        }
    }
    double w_pt = haveClip ? (clipR - clipL) : page_w;
    double h_pt = haveClip ? (clipO - clipU) : page_h;

    int w_px, h_px;
    if (target_w > 0) {
        /* -width: Breite fix, Höhe proportional */
        w_px = target_w;
        h_px = (int)(h_pt / w_pt * target_w + 0.5);
    } else {
        /* -dpi: normale DPI-basierte Berechnung */
        w_px = (int)(w_pt / 72.0 * dpi + 0.5);
        h_px = (int)(h_pt / 72.0 * dpi + 0.5);
    }

    /* PDFium-Bitmap anlegen (BGRA, hasAlpha=1) */
    FPDF_BITMAP bmp = FPDFBitmap_Create(w_px, h_px, 1 /*hasAlpha=BGRA*/);
    if (!bmp) {
        FPDF_ClosePage(page);
        PDFIUM_ERROR(interp, "cannot create bitmap");
    }

    /* Weißer Hintergrund */
    FPDFBitmap_FillRect(bmp, 0, 0, w_px, h_px, 0xFFFFFFFF);

    /* Rendern */
    int flags = FPDF_ANNOT | (printing ? FPDF_PRINTING : 0);

    /* Die Formularschicht, wenn verlangt. Sie wird NACH dem Seiteninhalt
     * gezeichnet, also erst weiter unten -- hier nur aufgebaut, damit im
     * Fehlerfall nichts halb fertig ist. */
    FPDF_FORMHANDLE form = NULL;
    PdfiumDocForm *df = NULL;
    if (withForms) {
        /* DIE UMGEBUNG DES DOKUMENTS.
         *
         * Solange render sich eine eigene baute, kamen das gewoehnliche
         * Bild und das Tippbild aus VERSCHIEDENEN Umgebungen -- dasselbe
         * Feld sah je nach Weg anders aus. Das ist Anzeige, nicht
         * Ordnungsliebe. */
        df = _DocFormGet(doc, page, pagenum);
        if (df) {
            form = df->form;
            FORM_OnAfterLoadPage(page, form);
        }
    }
    if (haveClip) {
        /* Mit Matrix: verschieben, damit die linke untere Ecke des
         * Ausschnitts auf den Ursprung faellt, und auf die Bildgroesse
         * skalieren. PDFium rechnet in Geraetekoordinaten mit y NACH
         * UNTEN, die Seite in y nach oben -- daher das Minus in f und
         * der Bezug auf die OBERE Kante des Ausschnitts.
         */
        double sx = (double)w_px / w_pt;
        double sy = (double)h_px / h_pt;
        FS_MATRIX m;
        m.a = (float)sx; m.b = 0.0f; m.c = 0.0f; m.d = (float)sy;
        m.e = (float)(-clipL * sx);
        m.f = (float)(-(page_h - clipO) * sy);
        FS_RECTF clipRect;
        clipRect.left = 0.0f; clipRect.top = 0.0f;
        clipRect.right = (float)w_px; clipRect.bottom = (float)h_px;
        FPDF_RenderPageBitmapWithMatrix(bmp, page, &m, &clipRect, flags);
    } else {
        FPDF_RenderPageBitmap(bmp, page, 0, 0, w_px, h_px,
                              0 /*rotation*/, flags);
        if (form) {
            FPDF_FFLDraw(form, bmp, page, 0, 0, w_px, h_px, 0, flags);
        }
    }
    if (form) {
        /* Nur die SEITE abmelden -- die Umgebung gehoert dem Dokument
         * und wird von pdfium::close abgebaut. */
        FORM_OnBeforeClosePage(page, form);
        if (df) {
            /* Die Seite der Sitzung wieder eintragen, wenn es eine
             * gibt -- sonst faende FFI_GetPage nichts mehr. */
            _DocFormSeiteAb(df);
        }
        form = NULL;
    }

    /* Rohpixel holen (BGRA) */
    void *buf = FPDFBitmap_GetBuffer(bmp);
    int stride = FPDFBitmap_GetStride(bmp);

    /* BGRA -> RGBA umwandeln (Tk erwartet RGBA) */
    size_t rgba_size = (size_t)w_px * h_px * 4;
    unsigned char *rgba = (unsigned char *)ckalloc(rgba_size);
    unsigned char *src  = (unsigned char *)buf;
    for (int y = 0; y < h_px; y++) {
        unsigned char *row = src + y * stride;
        unsigned char *dst = rgba + (size_t)y * w_px * 4;
        for (int x = 0; x < w_px; x++) {
            dst[0] = row[2]; /* R */
            dst[1] = row[1]; /* G */
            dst[2] = row[0]; /* B */
            dst[3] = 255;    /* A: voll opak */
            row += 4;
            dst += 4;
        }
    }

    FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(page);

    /* Tk-Photo-Image erzeugen oder ersetzen */
    Tk_PhotoHandle photo = Tk_FindPhoto(interp, imgname);
    if (!photo) {
        /* Image noch nicht vorhanden: über Tcl anlegen */
        Tcl_Obj *cmd = Tcl_ObjPrintf("image create photo %s", imgname);
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
            ckfree(rgba);
            return TCL_ERROR;
        }
        photo = Tk_FindPhoto(interp, imgname);
    }

    if (!photo) {
        ckfree(rgba);
        PDFIUM_ERROR(interp, "cannot create Tk photo image");
    }

    /* Pixeldaten in Tk-Photo schreiben */
    Tk_PhotoImageBlock block;
    block.pixelPtr  = rgba;
    block.width     = w_px;
    block.height    = h_px;
    block.pitch     = w_px * 4;
    block.pixelSize = 4;
    block.offset[0] = 0; /* R */
    block.offset[1] = 1; /* G */
    block.offset[2] = 2; /* B */
    block.offset[3] = 3; /* A */

    Tk_PhotoSetSize(interp, photo, w_px, h_px);
    Tk_PhotoPutBlock(interp, photo, &block, 0, 0, w_px, h_px,
                     TK_PHOTO_COMPOSITE_SET);

    ckfree(rgba);

    Tcl_SetResult(interp, imgname, TCL_VOLATILE);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::gettext doc-handle pagenum                                  */
/* Gibt den Textinhalt einer Seite zurück.                             */
/* ------------------------------------------------------------------ */
static int
PdfiumGetTextCmd(ClientData cd, Tcl_Interp *interp,
                 int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) {
        FPDF_ClosePage(page);
        PDFIUM_ERROR(interp, "cannot load text page");
    }

    int nchars = FPDFText_CountChars(tp);

    /* PDFium liefert UTF-16LE */
    unsigned short *buf16 =
        (unsigned short *)ckalloc((nchars + 1) * sizeof(unsigned short));
    FPDFText_GetText(tp, 0, nchars, buf16);
    buf16[nchars] = 0;

    /* UTF-16LE → Tcl-String (Tcl verwendet intern Unicode) */
    Tcl_Obj *result = _AnnotUtf16ToObj(interp, (unsigned short *)buf16, (unsigned long)((nchars + 1) * 2));
    Tcl_SetObjResult(interp, result);

    ckfree((char *)buf16);
    FPDFText_ClosePage(tp);
    FPDF_ClosePage(page);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::mctext doc-handle pagenum                                   */
/*                                                                     */
/* The text of a page grouped by marked-content ID, as a flat dict:    */
/*                                                                     */
/*     mcid1 text1 mcid2 text2 ...                                     */
/*                                                                     */
/* Why this is needed: pdfium::structure names the MCIDs of every      */
/* element, and pdfium::gettext returns the text of the whole page --  */
/* but nothing connected the two. With both, the READING ORDER can be  */
/* checked: does the text follow the structure tree, or the order it   */
/* happens to sit in the content stream? A screen reader follows the   */
/* tree, a validator does not check this, and it is the failure that   */
/* costs a reader the most.                                            */
/*                                                                     */
/* Objects without a marked-content ID are collected under the key -1, */
/* so nothing is lost silently; that key is exactly the content a      */
/* tagged document should not have.                                    */
/* ------------------------------------------------------------------ */
static int
PdfiumMcTextCmd(ClientData cd, Tcl_Interp *interp,
                int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3 && objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum ?-boxes 0|1?");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    int wantBoxes = 0;
    if (objc == 5) {
        const char *opt = Tcl_GetString(objv[3]);
        if (strcmp(opt, "-boxes") != 0) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown option \"%s\": must be -boxes", opt));
            return TCL_ERROR;
        }
        if (Tcl_GetIntFromObj(interp, objv[4], &wantBoxes) != TCL_OK)
            return TCL_ERROR;
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) {
        FPDF_ClosePage(page);
        PDFIUM_ERROR(interp, "cannot load text page");
    }

    /* One entry per MCID, in the order the objects appear in the
     * content stream -- that order is the point of the exercise. */
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);

    int nobj = FPDFPage_CountObjects(page);
    for (int i = 0; i < nobj; i++) {
        FPDF_PAGEOBJECT po = FPDFPage_GetObject(page, i);
        if (!po) continue;
        if (FPDFPageObj_GetType(po) != FPDF_PAGEOBJ_TEXT) continue;

        int mcid = FPDFPageObj_GetMarkedContentID(po);

        /* Ask for the size first, then fetch. The call returns the
         * number of BYTES including the terminating pair. */
        unsigned long need = FPDFTextObj_GetText(po, tp, NULL, 0);
        if (need < 2) continue;

        unsigned short *buf16 = (unsigned short *)ckalloc(need + 2);
        FPDFTextObj_GetText(po, tp, (FPDF_WCHAR *)buf16, need);
        Tcl_Obj *txt = _AnnotUtf16ToObj(interp, buf16, need);
        ckfree((char *)buf16);

        if (wantBoxes) {
            /* ANDERE FORM, nicht dieselbe mit einem Anhaengsel: eine
             * Liste von {mcid text {links unten rechts oben}}.
             *
             * Ohne die Option bleibt die flache Wechselliste, die sich
             * wie ein dict lesen laesst. Ein drittes Element dort
             * anzuhaengen wuerde sie still zu etwas anderem machen --
             * "dict get" auf einer ungeraden Liste ist ein Fehler, und
             * zwar erst beim Aufrufer.
             *
             * Das Rechteck ist das des TEXTOBJEKTS, nicht das eines
             * Zeichens; fuer Zeichen gibt es charboxes.
             */
            Tcl_Obj *e = Tcl_NewListObj(0, NULL);
            Tcl_ListObjAppendElement(interp, e, Tcl_NewIntObj(mcid));
            Tcl_ListObjAppendElement(interp, e, txt);
            float l, u, r, t;
            Tcl_Obj *box = Tcl_NewListObj(0, NULL);
            if (FPDFPageObj_GetBounds(po, &l, &u, &r, &t)) {
                Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(l));
                Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(u));
                Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(r));
                Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(t));
            }
            Tcl_ListObjAppendElement(interp, e, box);
            Tcl_ListObjAppendElement(interp, result, e);
        } else {
            Tcl_ListObjAppendElement(interp, result, Tcl_NewIntObj(mcid));
            Tcl_ListObjAppendElement(interp, result, txt);
        }
    }

    Tcl_SetObjResult(interp, result);
    FPDFText_ClosePage(tp);
    FPDF_ClosePage(page);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::pagesize doc-handle pagenum                                 */
/* Gibt {width_mm height_mm} zurück.                                  */
/* ------------------------------------------------------------------ */
static int
PdfiumPageSizeCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    double w_pt = FPDF_GetPageWidth(page);
    double h_pt = FPDF_GetPageHeight(page);
    FPDF_ClosePage(page);

    /* Punkte → mm: 1 pt = 25.4/72 mm */
    double w_mm = w_pt * 25.4 / 72.0;
    double h_mm = h_pt * 25.4 / 72.0;

    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(w_mm));
    Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(h_mm));
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::meta doc-handle key                                         */
/* key: Title Author Subject Keywords Creator Producer                 */
/*      CreationDate ModDate                                           */
/* Gibt den Metadaten-Wert als String zurück.                         */
/* ------------------------------------------------------------------ */
static int
PdfiumMetaCmd(ClientData cd, Tcl_Interp *interp,
              int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle key");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    const char *key = Tcl_GetString(objv[2]);
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;

    /* Puffer-Größe ermitteln */
    unsigned long len = FPDF_GetMetaText(doc, key, NULL, 0);
    if (len == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("", 0));
        return TCL_OK;
    }

    /* UTF-16LE Puffer */
    unsigned short *buf = (unsigned short *)ckalloc(len);
    FPDF_GetMetaText(doc, key, buf, len);
    int nchars = (int)((len / 2) - 1);
    if (nchars < 0) nchars = 0;

    Tcl_Obj *result = _AnnotUtf16ToObj(interp, (unsigned short *)buf, (unsigned long)len);
    Tcl_SetObjResult(interp, result);
    ckfree((char *)buf);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::rotation doc-handle pagenum                                 */
/* Gibt die Seitenrotation zurück: 0, 90, 180, 270                   */
/* ------------------------------------------------------------------ */
static int
PdfiumRotationCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    int rot = FPDFPage_GetRotation(page);
    FPDF_ClosePage(page);

    /* PDFium: 0=0°, 1=90°, 2=180°, 3=270° */
    Tcl_SetObjResult(interp, Tcl_NewIntObj(rot * 90));
    return TCL_OK;
}









/* Eine Tcl-Zeichenkette als UTF-16LE, mit Abschluss. Der Aufrufer gibt
 * den DString frei. */
static const unsigned short *
_ToUtf16(Tcl_Obj *obj, Tcl_DString *ds)
{
    Tcl_DStringInit(ds);
    Tcl_Encoding tenc = Tcl_GetEncoding(NULL, "utf-16le");
    if (!tenc) tenc = Tcl_GetEncoding(NULL, "unicode");
    if (tenc) {
        Tcl_UtfToExternalDString(tenc, Tcl_GetString(obj), -1, ds);
        Tcl_FreeEncoding(tenc);
    }
    { char _z[2] = {0,0}; Tcl_DStringAppend(ds, _z, 2); }
    return (const unsigned short *)Tcl_DStringValue(ds);
}


/* ------------------------------------------------------------------ */
/* Eine SITZUNG zum Tippen                                             */
/*                                                                     */
/*   set s [pdfium::editbegin $doc $seite]                             */
/*   pdfium::editclick  $s $x $y      Seitenkoordinaten, Punkt         */
/*   pdfium::editchar   $s "M"        ein Zeichen                      */
/*   pdfium::editkey    $s back       back|del|left|right|home|end     */
/*   pdfium::editrender $s -dpi 100 -imagename ::bild                  */
/*   pdfium::editend    $s                                             */
/*                                                                     */
/* WARUM EINE SITZUNG: Fokus, Schreibmarke und ein halb getipptes Feld  */
/* sind ZUSTAND. Die Formularumgebung gehoert seit 0.6.4 dem Dokument   */
/* und bleibt stehen; was fehlt, ist jemand, der die SEITE offenhaelt   */
/* und weiss, wo die Schreibmarke sitzt.                                */
/*                                                                     */
/* Die Sitzung haelt Umgebung UND Seite offen, solange getippt wird.    */
/* Die Lebensdauer steht damit im Aufrufer und nicht in einer stillen   */
/* Annahme -- wer "editbegin" ruft, sieht, dass er "editend" schuldet.  */
/*                                                                     */
/* GENAU EINE SEITE je Sitzung. Ueber Seiten hinweg zu tippen hiesse,   */
/* mehrere Seiten offenzuhalten und den Fokus zwischen ihnen zu         */
/* verwalten -- das waere eine zweite Sache unter demselben Namen.      */
/* ------------------------------------------------------------------ */
/* Die offenen Tippsitzungen.
 *
 * PDFium vertraegt nur EINE Formularumgebung je Dokument. Bis 0.6.4
 * baute sich jeder Befehl seine eigene, und wer waehrend einer Sitzung
 * "formfill" rief, bekam eine zweite -- die sah die Felder nicht:
 * "could not fill on page 0: kunde", obwohl das Feld da war. Seit die
 * Umgebung dem Dokument gehoert, geht beides nebeneinander (2.79).
 *
 * Das Verzeichnis bleibt: eine ZWEITE Sitzung auf demselben Dokument
 * ist weiterhin falsch, weil beide sich Fokus und Schreibmarke teilen
 * wuerden (2.84).
 *
 * Diese Meldung schickt den Leser in die falsche Richtung. Er sucht den
 * Namen, und der ist richtig. Darum wird die Lage GENANNT.
 *
 * Eine feste Zahl reicht: mehr als eine Handvoll Dokumente hat niemand
 * gleichzeitig offen, und eine wachsende Liste waere Verwaltung fuer
 * einen Fall, den es nicht gibt.
 */
#define PDFIUM_MAX_EDIT 16
/* Die Sitzung.
 *
 * Sie BESITZT weder Umgebung noch Rueckrufe -- beides gehoert dem
 * Dokument (PdfiumDocForm). Dort liegt die FPDF_FORMFILLINFO als
 * erstes Feld, damit PDFium mit ihrem Zeiger zurueckfindet.
 *
 * Bis 0.6.4 lag sie hier, und die Sitzung hatte eigene Rueckrufe --
 * doppelt und, sobald zwei Umgebungen lebten, schaedlich.
 *
 * Was die Sitzung haelt: die SEITE und die Zuordnung zur Umgebung.
 */
typedef struct PdfiumEdit {
    PdfiumDocForm      *df;    /* die Umgebung des Dokuments */
    FPDF_DOCUMENT       doc;   /* NULL = das Dokument ist zu */
    FPDF_PAGE           page;
    FPDF_FORMHANDLE     form;  /* == df->form, der Kuerze halber */
    int                 pagenum;
} PdfiumEdit;

/* Die Rueckrufe stehen bei der Dokumentumgebung (_Df*). Die Sitzung
 * hatte bis 0.6.4 eigene (_Ffi*) -- doppelt und, sobald beide lebten,
 * schaedlich. */

static PdfiumEdit *pdfiumEditList[PDFIUM_MAX_EDIT];
static int pdfiumEditCount = 0;

static int
_EditSessionOpen(FPDF_DOCUMENT doc)
{
    for (int i = 0; i < pdfiumEditCount; i++) {
        if (pdfiumEditList[i] && pdfiumEditList[i]->doc == doc) return 1;
    }
    return 0;
}

/* Eine Sitzung abbauen und das Dokument auf NULL setzen, damit ein
 * spaeterer Aufruf es MERKT.
 *
 * Ohne das lief "editchar" nach "pdfium::close" einfach durch --
 * gemessen 08.09.2026: kein Fehler, kein Absturz, Zugriff auf
 * freigegebenen Speicher. Das schlechteste Ergebnis, das ein Fehler
 * haben kann: er faellt nicht auf und schlaegt irgendwann woanders zu.
 *
 * Den Fokus VOR dem Schliessen abgeben -- PDFium schreibt den
 * Feldinhalt beim Fokusverlust fest. */
static void
_EditTeardown(PdfiumEdit *e)
{
    if (!e || !e->doc) return;
    FORM_ForceToKillFocus(e->form);
    FORM_OnBeforeClosePage(e->page, e->form);
    /* Die Umgebung NICHT abbauen -- sie gehoert dem Dokument und wird
     * von pdfium::close freigegeben. Bis 0.6.4 gehoerte sie der
     * Sitzung, und jeder andere Aufruf baute sich eine zweite. */
    if (e->df) {
        if (e->df->sessionPage == e->page) {
            e->df->sessionPage = NULL;
            e->df->sessionPageNum = -1;
        }
        if (e->df->curPage == e->page) {
            e->df->curPage = NULL;
            e->df->curPageNum = -1;
        }
    }
    FPDF_ClosePage(e->page);
    e->doc = NULL; e->page = NULL; e->form = NULL; e->df = NULL;
}

static void
_EditCloseAllFor(FPDF_DOCUMENT doc)
{
    for (int i = 0; i < pdfiumEditCount; i++) {
        if (pdfiumEditList[i] && pdfiumEditList[i]->doc == doc) {
            _EditTeardown(pdfiumEditList[i]);
        }
    }
}

static int
PdfiumEditBeginCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    /* KEINE ZWEITE SITZUNG AUF DEMSELBEN DOKUMENT.
     *
     * Die Umgebung gehoert dem Dokument, also teilten sich zwei
     * Sitzungen dieselbe -- und mit ihr Fokus und Schreibmarke. Wer in
     * der einen tippt, aendert die andere mit, und "editend" der ersten
     * meldet die Seite ab, waehrend die zweite sie noch braucht.
     *
     * formfill prueft das seit 0.6.4 nicht mehr (es geht jetzt
     * nebeneinander); hier ist es weiterhin falsch. */
    if (_EditSessionOpen(doc)) {
        FPDF_ClosePage(page);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "editbegin: a session is already open on this document --"
            " call editend first", -1));
        return TCL_ERROR;
    }
    PdfiumDocForm *df = _DocFormGet(doc, page, pagenum);
    if (!df) {
        FPDF_ClosePage(page);
        PDFIUM_ERROR(interp, "cannot init form environment");
    }
    PdfiumEdit *e = (PdfiumEdit *)ckalloc(sizeof(PdfiumEdit));
    memset(e, 0, sizeof(*e));
    e->df = df;
    e->doc = doc; e->page = page; e->pagenum = pagenum;
    e->form = df->form;
    FPDF_FORMHANDLE form = e->form;
    /* Die Seite anmelden, damit FFI_GetPage sie herausgeben kann --
     * und als SITZUNGSSEITE merken, damit kein anderer Befehl sie
     * ueberschreibt. */
    df->curPage = page;
    df->curPageNum = pagenum;
    df->sessionPage = page;
    df->sessionPageNum = pagenum;
    FORM_OnAfterLoadPage(page, form);
    if (pdfiumEditCount < PDFIUM_MAX_EDIT) {
        pdfiumEditList[pdfiumEditCount++] = e;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)(intptr_t)e));
    return TCL_OK;
}

static PdfiumEdit *
_EditFromObj(Tcl_Interp *interp, Tcl_Obj *obj)
{
    Tcl_WideInt w;
    if (Tcl_GetWideIntFromObj(interp, obj, &w) != TCL_OK) return NULL;
    PdfiumEdit *e = (PdfiumEdit *)(intptr_t)w;
    /* Nach "pdfium::close" ist doc NULL. Weiterzuarbeiten hiesse, auf
     * freigegebenem Speicher zu tippen -- und das geht eine Weile gut. */
    if (e && !e->doc) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "this edit session is over: the document was closed", -1));
        return NULL;
    }
    return e;
}

static int
PdfiumEditClickCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "session page-x page-y");
        return TCL_ERROR;
    }
    PdfiumEdit *e = _EditFromObj(interp, objv[1]);
    if (!e) return TCL_ERROR;
    double x, y;
    if (Tcl_GetDoubleFromObj(interp, objv[2], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[3], &y) != TCL_OK) return TCL_ERROR;

    /* Erst fragen, ob dort ueberhaupt ein Feld liegt. Ein Klick ins
     * Leere nimmt sonst still den Fokus weg, und der naechste
     * Tastendruck verschwindet -- was aussieht, als haette die Tastatur
     * nicht funktioniert. */
    int hat = FPDFPage_HasFormFieldAtPoint(e->form, e->page, x, y);
    if (hat < 0) hat = 0;

    /* EIN OFFENES AUFKLAPPMENUE LIEGT NICHT AUF EINEM FELD.
     *
     * PDFium zeichnet die Liste einer Combobox selbst, unterhalb des
     * Feldes. Ein Klick auf "Artikel B" landet dort AUSSERHALB jedes
     * Widget-Rechtecks -- HasFormFieldAtPoint sagt nein, und der Klick
     * wurde verschluckt. Auf dem Bildschirm blieb die Liste offen und
     * reagierte auf nichts mehr.
     *
     * Gemeldet 08.09.2026 mit zwei Bildschirmfotos: Pfeil geklickt,
     * Liste geht auf, danach tut die Maus nichts.
     *
     * Der Klick geht darum IMMER an PDFium. Ob etwas geschehen ist,
     * sagt PDFium selbst ueber FFI_Invalidate -- dafuer sind die
     * Rueckrufe da. Der frueher befuerchtete Fall (ein Klick ins Leere
     * nimmt still den Fokus) ist dabei kein Verlust, sondern richtig:
     * so schliesst man eine offene Liste.
     */
    int warDirty = e->df ? e->df->dirty : 0;
    if (e->df) e->df->dirty = 0;
    {
        /* Erst die Maus BEWEGEN, dann druecken.
         *
         * Ein Betrachter schickt vor jedem Klick Bewegungen, und PDFium
         * merkt sich daran, ueber welchem Widget der Zeiger steht.
         * Ohne das blieb der Fokus nach dem ersten Klick am ersten
         * Widget haengen: in einer Gruppe von drei Optionsfeldern
         * wirkte nur der erste Klick, die naechsten gingen ins Leere.
         *
         * Gemeldet am 06.09.2026 an pdf4tcls demo-forms.pdf. In
         * GETRENNTEN Sitzungen ging jede Option -- das war der Hinweis,
         * dass es am Sitzungszustand liegt und nicht an der Datei.
         */
        FORM_OnMouseMove(e->form, e->page, 0, x, y);
        FORM_OnLButtonDown(e->form, e->page, 0, x, y);
        FORM_OnLButtonUp(e->form, e->page, 0, x, y);
    }
    /* Getroffen heisst: dort lag ein Feld ODER PDFium hat auf den Klick
     * hin etwas neu zu zeichnen verlangt. Das zweite faengt den
     * Listeneintrag, den das erste nicht sieht. */
    int reagiert = (e->df && e->df->dirty) ? 1 : 0;
    if (e->df && warDirty) e->df->dirty = 1;
    /* DREI ANTWORTEN, NICHT ZWEI.
     *
     *   0  dort war nichts, und PDFium hat nichts zu tun
     *   1  dort lag ein Feld -- Schreibmarke gesetzt, es geht weiter
     *   2  dort lag KEIN Feld, PDFium hat trotzdem reagiert
     *
     * Der dritte Fall ist der Eintrag in einer offenen Aufklappliste.
     * Der Aufrufer muss ihn kennen: nach einer Wahl ist die Eingabe
     * FERTIG, und der Wert wird erst beim Fokusverlust festgeschrieben.
     * Wer das nicht weiss, zeigt weiter das leere Feld -- und der
     * Benutzer haelt es fuer nicht gespeichert.
     *
     * Gemeldet 08.09.2026 mit drei Bildschirmfotos: gewaehlt, im Feld
     * sichtbar, danach wieder leer. In der Datei stand der Wert.
     */
    Tcl_SetObjResult(interp,
            Tcl_NewIntObj(hat ? 1 : (reagiert ? 2 : 0)));
    return TCL_OK;
}

static int
PdfiumEditCharCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "session text");
        return TCL_ERROR;
    }
    PdfiumEdit *e = _EditFromObj(interp, objv[1]);
    if (!e) return TCL_ERROR;

    /* Zeichenweise, nicht als Zeichenkette: FORM_OnChar nimmt EIN
     * Zeichen, so wie eine Tastatur es liefert. Wer eine ganze Zeile
     * einsetzen will, nimmt formfill -- das ist der andere Weg und
     * heisst auch anders. */
    Tcl_Size len;
    const char *utf8 = Tcl_GetStringFromObj(objv[2], &len);
    int n = 0;
    const char *p2 = utf8;
    while (p2 < utf8 + len) {
        /* Tcl_UniChar, NICHT int: unter 8.6 ist der Typ "unsigned
         * short", unter 9.0 "int". Ein festgeschriebenes int liess sich
         * mit 9.0 uebersetzen und mit 8.6 nicht -- gemeldet aus einem
         * Bau gegen tcl8.6, und der Fehler war beim Bauen sichtbar und
         * nicht erst beim Laufen. Immerhin.
         *
         * Folge unter 8.6: Zeichen jenseits der Basic Multilingual Plane
         * kommen als Ersatzpaar an, also in zwei Schritten. Fuer
         * Formularfelder ist das ohne Belang; wer Schriftzeichen
         * jenseits von U+FFFF eintippt, hat andere Sorgen. */
        Tcl_UniChar ch;
        p2 += Tcl_UtfToUniChar(p2, &ch);
        FORM_OnChar(e->form, e->page, (int)ch, 0);
        n++;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(n));
    return TCL_OK;
}

static int
PdfiumEditKeyCmd(ClientData cd, Tcl_Interp *interp,
                 int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv,
                "session back|del|left|right|home|end");
        return TCL_ERROR;
    }
    PdfiumEdit *e = _EditFromObj(interp, objv[1]);
    if (!e) return TCL_ERROR;
    const char *k = Tcl_GetString(objv[2]);

    /* RUECKTASTE UEBER OnChar, nicht ueber OnKeyDown.
     *
     * Gemessen: "abc" tippen, dann FORM_OnKeyDown mit FWL_VKEY_Back --
     * es blieb "abc". Mit FORM_OnChar und 0x08 wird "ab" daraus.
     * PDFium behandelt den Rueckschritt als ZEICHEN, so wie es aus einer
     * Tastatur kommt; die Pfeiltasten dagegen ueber OnKeyDown.
     *
     * Ohne die Messung waere hier eine Taste, die es gibt und die nichts
     * tut -- und der Aufrufer haette den Fehler bei sich gesucht.
     */
    if (strcmp(k, "back") == 0) {
        FORM_OnChar(e->form, e->page, 0x08, 0);
        return TCL_OK;
    }

    int code;
    if      (strcmp(k, "tab")   == 0) code = FWL_VKEY_Tab;
    else if (strcmp(k, "del")   == 0) code = FWL_VKEY_Delete;
    else if (strcmp(k, "up")    == 0) code = FWL_VKEY_Up;
    else if (strcmp(k, "down")  == 0) code = FWL_VKEY_Down;
    else if (strcmp(k, "left")  == 0) code = FWL_VKEY_Left;
    else if (strcmp(k, "right") == 0) code = FWL_VKEY_Right;
    else if (strcmp(k, "home")  == 0) code = FWL_VKEY_Home;
    else if (strcmp(k, "end")   == 0) code = FWL_VKEY_End;
    else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "unknown key \"%s\": must be tab, back, del, up, down, left,"
            " right, home or end", k));
        return TCL_ERROR;
    }
    FORM_OnKeyDown(e->form, e->page, code, 0);
    FORM_OnKeyUp(e->form, e->page, code, 0);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::editstate session                                           */
/*                                                                     */
/* Was PDFium waehrend der Sitzung gemeldet hat:                       */
/*                                                                     */
/*   dirty   1, wenn ein Bereich neu zu zeichnen ist                   */
/*   rect    {links oben rechts unten} dieses Bereichs                 */
/*   cursor  zuletzt gewuenschte Zeigerform (0 Pfeil, 3 Textmarke)     */
/*   changed 1, wenn sich ein Feldwert geaendert hat                   */
/*                                                                     */
/* WOZU: bis 0.6.4 zeichnete der Aufrufer nach jedem Tastendruck die   */
/* ganze Seite neu, weil er nicht wusste, was sich geaendert hat. Jetzt */
/* sagt es PDFium selbst -- ueber FFI_Invalidate.                       */
/*                                                                     */
/* Das Lesen setzt "dirty" ZURUECK. Sonst muesste der Aufrufer es tun, */
/* und wer es vergisst, zeichnet fuer immer neu.                       */
/* ------------------------------------------------------------------ */
static int
PdfiumEditStateCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "session");
        return TCL_ERROR;
    }
    PdfiumEdit *e = _EditFromObj(interp, objv[1]);
    if (!e) return TCL_ERROR;
    Tcl_Obj *d = Tcl_NewListObj(0, NULL);
#define ES_PUT(k, v) do { \
        Tcl_ListObjAppendElement(interp, d, Tcl_NewStringObj((k), -1)); \
        Tcl_ListObjAppendElement(interp, d, (v)); \
    } while (0)
    ES_PUT("dirty", Tcl_NewIntObj(e->df->dirty));
    Tcl_Obj *r = Tcl_NewListObj(0, NULL);
    if (e->df->dirty) {
        Tcl_ListObjAppendElement(interp, r, Tcl_NewDoubleObj(e->df->dirtyL));
        Tcl_ListObjAppendElement(interp, r, Tcl_NewDoubleObj(e->df->dirtyT));
        Tcl_ListObjAppendElement(interp, r, Tcl_NewDoubleObj(e->df->dirtyR));
        Tcl_ListObjAppendElement(interp, r, Tcl_NewDoubleObj(e->df->dirtyB));
    }
    ES_PUT("rect", r);
    ES_PUT("cursor", Tcl_NewIntObj(e->df->cursor));
    ES_PUT("changed", Tcl_NewIntObj(e->df->changed));
#undef ES_PUT
    e->df->dirty = 0;
    e->df->changed = 0;
    Tcl_SetObjResult(interp, d);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::edittext session                                            */
/*                                                                     */
/* Der Text, der GERADE im Feld steht -- vor dem Festschreiben.        */
/*                                                                     */
/* WOZU: PDFium schreibt einen Feldwert erst beim Fokusverlust fest.   */
/* Wer tippt, sieht die Buchstaben auf der Seite, und "formfields"     */
/* meldet weiter den alten Wert. Im Mitschnitt vom 08.09.2026 stand    */
/* nach jedem editchar wieder "f_name {}" -- bis ein Tab kam, dann     */
/* "f_name fg". Auf dem Bildschirm sieht das aus, als komme nichts an. */
/*                                                                     */
/* Den Fokus dafuer abzugeben waere falsch: dann koennte man nicht     */
/* weitertippen. FORM_GetFocusedText fragt PDFium direkt.              */
/*                                                                     */
/* Leere Rueckgabe heisst: kein Feld hat den Fokus.                    */
/* ------------------------------------------------------------------ */
static int
PdfiumEditTextCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "session");
        return TCL_ERROR;
    }
    PdfiumEdit *e = _EditFromObj(interp, objv[1]);
    if (!e) return TCL_ERROR;
    /* MIT DEM NAMEN DES FELDES.
     *
     * Der Text allein reicht nicht. Ein Aufrufer, der ihn in eine
     * Feldliste eintraegt, muss wissen WOHIN -- und nach einem Tab
     * wandert der Fokus, waehrend die Auswahl in der Liste stehen
     * bleibt.
     *
     * Gemessen 08.09.2026 an einem Mitschnitt: "edittext -> 1" nach
     * mehreren Tabs. Das war der Inhalt von f_menge, und er landete in
     * der Zeile, die der Benutzer zuletzt angeklickt hatte. Auf dem
     * Bildschirm hatten "ploetzlich auch die anderen Felder Daten".
     *
     * Rueckgabe: {name text}. Beides leer heisst: nichts hat den Fokus.
     */
    Tcl_Obj *paar = Tcl_NewListObj(0, NULL);
    Tcl_Obj *name = Tcl_NewStringObj("", 0);
    Tcl_IncrRefCount(name);
    FPDF_ANNOTATION fa = NULL;
    int seite = 0;
    if (FORM_GetFocusedAnnot(e->form, &seite, &fa) && fa) {
        unsigned long nl = FPDFAnnot_GetFormFieldName(e->form, fa, NULL, 0);
        if (nl > 2) {
            unsigned short *nb = (unsigned short *)ckalloc(nl);
            FPDFAnnot_GetFormFieldName(e->form, fa, nb, nl);
            Tcl_DecrRefCount(name);
            name = _AnnotUtf16ToObj(interp, nb, nl);
            ckfree((char *)nb);
        }
        FPDFPage_CloseAnnot(fa);
    }
    Tcl_ListObjAppendElement(interp, paar, name);

    unsigned long len = FORM_GetFocusedText(e->form, e->page, NULL, 0);
    if (len > 2) {
        unsigned short *buf = (unsigned short *)ckalloc(len);
        FORM_GetFocusedText(e->form, e->page, buf, len);
        Tcl_ListObjAppendElement(interp, paar,
                _AnnotUtf16ToObj(interp, buf, len));
        ckfree((char *)buf);
    } else {
        Tcl_ListObjAppendElement(interp, paar, Tcl_NewStringObj("", 0));
    }
    Tcl_SetObjResult(interp, paar);
    return TCL_OK;
}

static int
PdfiumEditEndCmd(ClientData cd, Tcl_Interp *interp,
                 int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "session");
        return TCL_ERROR;
    }
    PdfiumEdit *e = _EditFromObj(interp, objv[1]);
    if (!e) return TCL_ERROR;
    /* Den Fokus VOR dem Schliessen abgeben: PDFium schreibt den Inhalt
     * des Feldes beim Fokusverlust fest. Ohne das ginge das zuletzt
     * getippte Feld verloren -- und zwar genau das, an dem man gerade
     * gearbeitet hat. */
    _EditTeardown(e);
    for (int i = 0; i < pdfiumEditCount; i++) {
        if (pdfiumEditList[i] == e) {
            pdfiumEditList[i] = pdfiumEditList[--pdfiumEditCount];
            break;
        }
    }
    ckfree((char *)e);
    return TCL_OK;
}


/* ------------------------------------------------------------------ */
/* pdfium::editrender session ?-dpi n? ?-imagename name?               */
/*                                                                     */
/* Die Seite einer Sitzung zeichnen -- MIT deren Formularumgebung.      */
/*                                                                     */
/* WARUM EIGENS: "render -forms 1" zeichnet ueber ein eigenes,          */
/* transientes Seitenobjekt und zeigt den FESTGESCHRIEBENEN Stand.      */
/* PDFium schreibt einen Feldwert erst beim Fokusverlust fest -- also   */
/* sieht render nicht, was gerade getippt wird. Gemessen: waehrend      */
/* einer Sitzung drei Zeichen getippt, das Bild blieb bei 1406 dunklen  */
/* Punkten; erst nach "editend" waren es 1503.                          */
/*                                                                     */
/* (Bis 0.6.4 baute render sich zusaetzlich eine eigene Umgebung. Das   */
/* ist vorbei -- es gibt eine je Dokument. Der Grund hier bleibt.)      */
/*                                                                     */
/* Man tippte also BLIND. Das ist kein Schoenheitsfehler -- wer nicht   */
/* sieht, was er schreibt, kann es auch nicht berichtigen.              */
/* ------------------------------------------------------------------ */
static int
PdfiumEditRenderCmd(ClientData cd, Tcl_Interp *interp,
                    int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc < 2 || (objc % 2) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv,
                "session ?-dpi n? ?-imagename name?");
        return TCL_ERROR;
    }
    /* TK-STUBS EINRICHTEN.
     *
     * Sie werden verzoegert geholt (EnsureTk) -- nur wer ein Tk-Bild
     * anfasst, braucht sie. "render" tut das seit jeher; "editrender"
     * war ein zweiter Weg zu Tk_FindPhoto und hat es vergessen.
     *
     * Die Folge war kein Fehler, sondern ein SEGMENTIERUNGSFEHLER: die
     * Funktionszeiger der Stubs zeigen ins Leere, solange sie niemand
     * geholt hat. Und es fiel lange nicht auf, weil in der Suite und im
     * Viewer IMMER erst gezeichnet wird -- gemessen: ein
     * "render -dpi 100" davor, und editrender laeuft.
     *
     * Ein Absturz, der nur beim ersten Aufruf auftritt, ist der
     * unangenehmste: er trifft den, der die Bindung neu benutzt.
     */
    if (EnsureTk(interp) != TCL_OK) return TCL_ERROR;

    PdfiumEdit *e = _EditFromObj(interp, objv[1]);
    if (!e) return TCL_ERROR;

    double dpi = 150.0;
    const char *imgname = "pdfpage";
    for (int i = 2; i < objc; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        if (strcmp(opt, "-dpi") == 0) {
            if (Tcl_GetDoubleFromObj(interp, objv[i+1], &dpi) != TCL_OK)
                return TCL_ERROR;
            if (dpi <= 0) {
                Tcl_SetObjResult(interp,
                    Tcl_NewStringObj("-dpi must be positive", -1));
                return TCL_ERROR;
            }
        } else if (strcmp(opt, "-imagename") == 0) {
            imgname = Tcl_GetString(objv[i+1]);
        } else {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "unknown option \"%s\": must be -dpi or -imagename", opt));
            return TCL_ERROR;
        }
    }

    double wpt = FPDF_GetPageWidth(e->page);
    double hpt = FPDF_GetPageHeight(e->page);
    int w_px = (int)(wpt * dpi / 72.0 + 0.5);
    int h_px = (int)(hpt * dpi / 72.0 + 0.5);
    if (w_px < 1) w_px = 1;
    if (h_px < 1) h_px = 1;

    FPDF_BITMAP bmp = FPDFBitmap_Create(w_px, h_px, 1);
    if (!bmp) PDFIUM_ERROR(interp, "cannot create bitmap");
    FPDFBitmap_FillRect(bmp, 0, 0, w_px, h_px, 0xFFFFFFFF);
    int flags = FPDF_ANNOT;
    FPDF_RenderPageBitmap(bmp, e->page, 0, 0, w_px, h_px, 0, flags);
    /* DIESELBE Umgebung wie beim Tippen -- das ist der ganze Sinn. */
    FPDF_FFLDraw(e->form, bmp, e->page, 0, 0, w_px, h_px, 0, flags);

    Tk_PhotoHandle photo = Tk_FindPhoto(interp, imgname);
    if (!photo) {
        Tcl_Obj *cmd = Tcl_ObjPrintf("image create photo %s", imgname);
        Tcl_IncrRefCount(cmd);
        int rc = Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL);
        Tcl_DecrRefCount(cmd);
        if (rc != TCL_OK) { FPDFBitmap_Destroy(bmp); return TCL_ERROR; }
        photo = Tk_FindPhoto(interp, imgname);
    }
    if (!photo) {
        FPDFBitmap_Destroy(bmp);
        PDFIUM_ERROR(interp, "cannot create Tk photo image");
    }

    Tk_PhotoImageBlock block;
    block.pixelPtr  = (unsigned char *)FPDFBitmap_GetBuffer(bmp);
    block.width     = w_px;
    block.height    = h_px;
    block.pitch     = FPDFBitmap_GetStride(bmp);
    block.pixelSize = 4;
    /* BGRA, wie bei render: PDFium legt Blau zuerst ab. */
    block.offset[0] = 2;
    block.offset[1] = 1;
    block.offset[2] = 0;
    block.offset[3] = 3;
    Tk_PhotoSetSize(interp, photo, w_px, h_px);
    Tk_PhotoPutBlock(interp, photo, &block, 0, 0, w_px, h_px,
                     TK_PHOTO_COMPOSITE_SET);
    FPDFBitmap_Destroy(bmp);

    Tcl_SetObjResult(interp, Tcl_ObjPrintf("%d %d", w_px, h_px));
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::formfill doc-handle pagenum dict                            */
/*                                                                     */
/* Felder einer Seite ausfuellen -- ueber PDFiums Formularumgebung,     */
/* nicht durch Setzen von /V.                                          */
/*                                                                     */
/* WARUM DER UMWEG: FPDFAnnot_SetStringValue schriebe /V und liesse die */
/* Erscheinung stehen. Genau diese Luecke hatte pdf4tcl::fillForms bis  */
/* 0.9.4.64: der Bildschirm zeigte den neuen Wert, das Papier den       */
/* alten -- bei einem leeren Feld gar nichts.                          */
/*                                                                     */
/* Die Formularumgebung geht den Weg, den ein Betrachter geht: Feld     */
/* fokussieren, Inhalt auswaehlen, ersetzen. PDFium baut den            */
/* Appearance-Strom dabei SELBST neu -- es ist der Formularmotor von    */
/* Chrome und tut nichts anderes, wenn dort jemand tippt.              */
/*                                                                     */
/* WAS GEHT, und auf welchem Weg:                                      */
/*                                                                     */
/*   Textfeld      Fokus, alles auswaehlen, ersetzen                   */
/*   Kombination   ueber die Beschriftung mit den Pfeiltasten waehlen   */
/*   Listenfeld    ebenso                                              */
/*   Ankreuzfeld   Wahrheitswert, Klick in die Mitte                    */
/*   Optionsfeld   ebenso, aber nicht abwaehlbar                        */
/*                                                                     */
/* Schaltflaeche und Signatur nehmen weder Wert noch Zustand und        */
/* werden GEMELDET, nicht uebergangen.                                  */
/*                                                                     */
/* Dieser Kommentar sagte bis zum 06.09.2026 "Grenze: Textfelder und    */
/* Kombinationsfelder", waehrend der Rumpf laengst Kaestchen und        */
/* Optionsfelder fuellte. Ein veralteter Kommentar ist schlimmer als    */
/* keiner, weil er geglaubt wird -- und er steht direkt am Code.        */
/*                                                                     */
/* Nach dem Fuellen "save" rufen, sonst ist die Arbeit mit dem          */
/* Schliessen weg.                                                     */
/* ------------------------------------------------------------------ */
static int
PdfiumFormFillCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum dict");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    Tcl_Obj **wv; Tcl_Size wc;
    if (Tcl_ListObjGetElements(interp, objv[3], &wc, &wv) != TCL_OK)
        return TCL_ERROR;
    if (wc % 2) {
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("values must be a dictionary", -1));
        return TCL_ERROR;
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;

    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    PdfiumDocForm *df = _DocFormGet(doc, page, pagenum);
    if (!df) {
        FPDF_ClosePage(page);
        PDFIUM_ERROR(interp, "cannot init form environment");
    }
    FPDF_FORMHANDLE form = df->form;
    FORM_OnAfterLoadPage(page, form);

    int gefuellt = 0;
    Tcl_Obj *fehlend = Tcl_NewListObj(0, NULL);
    int n = FPDFPage_GetAnnotCount(page);

    for (Tcl_Size k = 0; k < wc; k += 2) {
        const char *wunschName = Tcl_GetString(wv[k]);
        int getroffen = 0;

        for (int i = 0; i < n; i++) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
            if (!annot) continue;
            if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_WIDGET) {
                FPDFPage_CloseAnnot(annot);
                continue;
            }
            unsigned long nlen =
                FPDFAnnot_GetFormFieldName(form, annot, NULL, 0);
            int passt = 0;
            if (nlen > 2) {
                unsigned short *nb = (unsigned short *)ckalloc(nlen);
                FPDFAnnot_GetFormFieldName(form, annot, nb, nlen);
                Tcl_Obj *nm = _AnnotUtf16ToObj(interp, nb, nlen);
                Tcl_IncrRefCount(nm);
                ckfree((char *)nb);
                passt = (strcmp(Tcl_GetString(nm), wunschName) == 0);
                Tcl_DecrRefCount(nm);
            }
            if (!passt) {
                FPDFPage_CloseAnnot(annot);
                continue;
            }

            int typ = FPDFAnnot_GetFormFieldType(form, annot);
            int fftyp2 = typ;

            if (typ == FPDF_FORMFIELD_CHECKBOX
                    || typ == FPDF_FORMFIELD_RADIOBUTTON) {
                /* Ein Kaestchen nimmt keinen Text, sondern einen
                 * ZUSTAND. Der Wert ist darum ein Wahrheitswert.
                 *
                 * Umgeschaltet wird mit einem KLICK in die Mitte des
                 * Feldes -- denselben Weg geht ein Betrachter, und
                 * PDFium fuehrt dabei /V, /AS und die Erscheinung
                 * zusammen nach. Wer /V allein setzte, haette das
                 * Kaestchen in der Datei angekreuzt und auf dem Papier
                 * leer.
                 *
                 * VORHER MESSEN, nicht blind klicken: FPDFAnnot_IsChecked
                 * sagt den Zustand. Ein Klick auf ein bereits
                 * angekreuztes Kaestchen wuerde es abwaehlen -- und
                 * "setze auf ja" haette dann das Gegenteil bewirkt.
                 *
                 * Ein OPTIONSFELD laesst sich nicht abwaehlen: in einer
                 * Gruppe ist immer eines gewaehlt. "0" auf ein
                 * Optionsfeld wird darum gemeldet statt still zu
                 * scheitern.
                 */
                /* EIN OPTIONSFELD WIRD BEIM NAMEN GENANNT.
                 *
                 * Eine Gruppe teilt sich einen Namen; welche Option
                 * gemeint ist, sagt der EXPORTWERT des einzelnen
                 * Widgets (/normal, /express, ...). Bis hierher nahm
                 * formfill nur einen Wahrheitswert -- und der waehlte
                 * immer das erste Widget. "express" war nicht
                 * ansprechbar, und der Aufrufer bekam
                 *
                 *     "prio" is a radio button and takes a boolean
                 *
                 * was ihn in die falsche Richtung schickte: nicht der
                 * Wert war falsch, sondern der Weg fehlte.
                 *
                 * Erst wird nach dem Exportwert gesucht. Nur wenn
                 * keiner passt UND der Wert ein Wahrheitswert ist,
                 * bleibt es beim alten Verhalten -- sonst braeche eine
                 * bestehende Verwendung mit "1".
                 */
                if (typ == FPDF_FORMFIELD_RADIOBUTTON) {
                    const char *wunsch = Tcl_GetString(wv[k+1]);
                    FPDF_ANNOTATION treffer = NULL;
                    Tcl_Obj *erlaubt = Tcl_NewListObj(0, NULL);
                    int na2 = FPDFPage_GetAnnotCount(page);
                    for (int q = 0; q < na2; q++) {
                        FPDF_ANNOTATION a2 = FPDFPage_GetAnnot(page, q);
                        if (!a2) continue;
                        unsigned long nl2 =
                            FPDFAnnot_GetFormFieldName(form, a2, NULL, 0);
                        int gleicherName = 0;
                        if (nl2 > 2) {
                            unsigned short *nb2 =
                                (unsigned short *)ckalloc(nl2);
                            FPDFAnnot_GetFormFieldName(form, a2, nb2, nl2);
                            Tcl_Obj *nm2 = _AnnotUtf16ToObj(interp, nb2, nl2);
                            Tcl_IncrRefCount(nm2);
                            ckfree((char *)nb2);
                            gleicherName = (strcmp(Tcl_GetString(nm2),
                                                   wunschName) == 0);
                            Tcl_DecrRefCount(nm2);
                        }
                        if (gleicherName) {
                            unsigned long el =
                                FPDFAnnot_GetFormFieldExportValue(form, a2,
                                                                  NULL, 0);
                            if (el > 2) {
                                unsigned short *eb =
                                    (unsigned short *)ckalloc(el);
                                FPDFAnnot_GetFormFieldExportValue(form, a2,
                                                                  eb, el);
                                Tcl_Obj *ex = _AnnotUtf16ToObj(interp, eb, el);
                                ckfree((char *)eb);
                                Tcl_ListObjAppendElement(interp, erlaubt, ex);
                                if (!treffer
                                        && strcmp(Tcl_GetString(ex),
                                                  wunsch) == 0) {
                                    treffer = a2;
                                    continue;   /* nicht schliessen */
                                }
                            }
                        }
                        FPDFPage_CloseAnnot(a2);
                    }
                    if (treffer) {
                        if (!FPDFAnnot_IsChecked(form, treffer)) {
                            FS_RECTF r2;
                            if (FPDFAnnot_GetRect(treffer, &r2)) {
                                double cx = (r2.left + r2.right) / 2.0;
                                double cy = (r2.top + r2.bottom) / 2.0;
                                FORM_OnMouseMove(form, page, 0, cx, cy);
                                FORM_OnLButtonDown(form, page, 0, cx, cy);
                                FORM_OnLButtonUp(form, page, 0, cx, cy);
                            }
                        }
                        FORM_ForceToKillFocus(form);
                        FPDFPage_CloseAnnot(treffer);
                        FPDFPage_CloseAnnot(annot);
                        gefuellt++;
                        getroffen = 1;
                        break;
                    }
                    int istWahr;
                    if (Tcl_GetBooleanFromObj(NULL, wv[k+1],
                                              &istWahr) != TCL_OK) {
                        Tcl_Size ne2;
                        Tcl_ListObjLength(interp, erlaubt, &ne2);
                        Tcl_ListObjAppendElement(interp, fehlend,
                            Tcl_ObjPrintf("%s (no such option%s%s)",
                                wunschName,
                                ne2 ? "; allowed: " : "",
                                ne2 ? Tcl_GetString(erlaubt) : ""));
                        FPDFPage_CloseAnnot(annot);
                        getroffen = 1;
                        break;
                    }
                }

                int soll;
                if (Tcl_GetBooleanFromObj(interp, wv[k+1], &soll) != TCL_OK) {
                    FPDFPage_CloseAnnot(annot);
                    FORM_OnBeforeClosePage(page, form);
                    _DocFormSeiteAb(df);
                    FPDF_ClosePage(page);
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                        "formfill: \"%s\" is a %s and takes a boolean,"
                        " not \"%s\"", wunschName,
                        typ == FPDF_FORMFIELD_CHECKBOX ? "check box"
                                                       : "radio button",
                        Tcl_GetString(wv[k+1])));
                    return TCL_ERROR;
                }
                int ist = FPDFAnnot_IsChecked(form, annot) ? 1 : 0;
                if (!soll && typ == FPDF_FORMFIELD_RADIOBUTTON) {
                    FPDFPage_CloseAnnot(annot);
                    Tcl_ListObjAppendElement(interp, fehlend,
                            Tcl_ObjPrintf("%s (a radio button cannot be"
                                          " unset; select another one)",
                                          wunschName));
                    getroffen = 1;
                    break;
                }
                if (ist != soll) {
                    FS_RECTF r;
                    if (FPDFAnnot_GetRect(annot, &r)) {
                        double cx = (r.left + r.right) / 2.0;
                        double cy = (r.top + r.bottom) / 2.0;
                        FORM_OnLButtonDown(form, page, 0, cx, cy);
                        FORM_OnLButtonUp(form, page, 0, cx, cy);
                    }
                }
                FORM_ForceToKillFocus(form);
                FPDFPage_CloseAnnot(annot);
                gefuellt++;
                getroffen = 1;
                break;
            }

            if (typ != FPDF_FORMFIELD_TEXTFIELD
                    && typ != FPDF_FORMFIELD_COMBOBOX
                    && typ != FPDF_FORMFIELD_LISTBOX) {
                /* Kein Text und keine Auswahl -- also nicht zu fuellen.
                 * Still zu ueberspringen waere schlimmer: der Aufrufer
                 * haette den Namen genannt und bekaeme keine Antwort.
                 *
                 * LISTBOX gehoert seit dem 06.09.2026 dazu. Der Zweig,
                 * der sie behandelt, stand schon darunter -- diese
                 * Wache liess ihn aber nie erreichen. Toter Code, den
                 * ich beim Einbauen der Auswahl selbst hinterlassen
                 * habe; gemeldet aus einer Durchsicht, nachgemessen:
                 * dreimal "down" waehlt "Vreden". */
                FPDFPage_CloseAnnot(annot);
                Tcl_ListObjAppendElement(interp, fehlend,
                        Tcl_ObjPrintf("%s (not a fillable field)", wunschName));
                getroffen = 1;
                break;
            }

            /* AUSWAHLFELDER werden GEWAEHLT, nicht beschrieben.
             *
             * Ein Kombinationsfeld ohne Bearbeitungsflagge laesst sich
             * nicht beschreiben -- ReplaceSelection tut dort nichts, und
             * bis hierher meldete formfill trotzdem einen Erfolg.
             *
             * Gewaehlt wird mit den Pfeiltasten, so wie ein Betrachter
             * es tut: erst nach ganz oben, dann so oft nach unten, wie
             * der Eintrag von oben entfernt ist. Gemessen: nach
             * "down" stand "Artikel A" im Feld -- allerdings erst nach
             * dem Fokusverlust, wie ueberall bei PDFium.
             *
             * Gesucht wird ueber die BESCHRIFTUNG, weil der Aufrufer
             * die kennt und nicht den Index. Steht sie nicht in der
             * Liste, wird das gemeldet -- mitsamt den erlaubten Werten.
             */
            if (fftyp2 == FPDF_FORMFIELD_COMBOBOX
                    || fftyp2 == FPDF_FORMFIELD_LISTBOX) {
                int oc = FPDFAnnot_GetOptionCount(form, annot);
                int ziel = -1;
                Tcl_Obj *erlaubt = Tcl_NewListObj(0, NULL);
                for (int q = 0; q < oc; q++) {
                    unsigned long ll =
                        FPDFAnnot_GetOptionLabel(form, annot, q, NULL, 0);
                    if (ll <= 2) continue;
                    unsigned short *lb = (unsigned short *)ckalloc(ll);
                    FPDFAnnot_GetOptionLabel(form, annot, q, lb, ll);
                    Tcl_Obj *lab = _AnnotUtf16ToObj(interp, lb, ll);
                    ckfree((char *)lb);
                    Tcl_ListObjAppendElement(interp, erlaubt, lab);
                    if (ziel < 0 && strcmp(Tcl_GetString(lab),
                                           Tcl_GetString(wv[k+1])) == 0) {
                        ziel = q;
                    }
                }
                if (ziel < 0) {
                    Tcl_ListObjAppendElement(interp, fehlend,
                        Tcl_ObjPrintf("%s (no such option; allowed: %s)",
                            wunschName, Tcl_GetString(erlaubt)));
                    FPDFPage_CloseAnnot(annot);
                    getroffen = 1;
                    break;
                }
                FORM_SetFocusedAnnot(form, annot);
                /* Nach ganz oben: einmal mehr als es Eintraege gibt, dann
                 * steht die Auswahl sicher auf dem ersten -- eine
                 * Home-Taste tut hier nichts, gemessen. */
                for (int q = 0; q <= oc; q++) {
                    FORM_OnKeyDown(form, page, FWL_VKEY_Up, 0);
                    FORM_OnKeyUp(form, page, FWL_VKEY_Up, 0);
                }
                /* Wieviele Schritte nach unten? NACHFRAGEN, nicht
                 * annehmen.
                 *
                 * Beim KOMBINATIONSFELD ist nach dem Hochlaufen nichts
                 * gewaehlt -- der erste "down" waehlt erst den ersten
                 * Eintrag, also braucht es ziel+1 Schritte. Beim
                 * LISTENFELD steht die Auswahl danach auf dem ersten,
                 * also ziel.
                 *
                 * Beides gemessen, und beide Male an einem Versatz um
                 * eins aufgefallen: "Artikel C" landete auf "Artikel B",
                 * spaeter "Bremen" auf "Hamburg". Zweimal dieselbe
                 * Annahme, zweimal falsch -- darum steht hier jetzt eine
                 * Frage statt einer Regel.
                 *
                 * IsOptionSelected sagt es. Ist der erste Eintrag schon
                 * gewaehlt, sind es ziel Schritte, sonst ziel+1. */
                int schritte = ziel + 1;
                if (FPDFAnnot_IsOptionSelected(form, annot, 0)) {
                    schritte = ziel;
                }
                for (int q = 0; q < schritte; q++) {
                    FORM_OnKeyDown(form, page, FWL_VKEY_Down, 0);
                    FORM_OnKeyUp(form, page, FWL_VKEY_Down, 0);
                }
                FORM_ForceToKillFocus(form);
                FPDFPage_CloseAnnot(annot);
                gefuellt++;
                getroffen = 1;
                break;
            }

            /* Der Weg eines Betrachters: fokussieren, alles auswaehlen,
             * ersetzen. Ohne SelectAllText wuerde der neue Text an den
             * alten angehaengt statt ihn zu ersetzen. */
            FORM_SetFocusedAnnot(form, annot);
            FORM_SelectAllText(form, page);
            Tcl_DString ds;
            const unsigned short *w = _ToUtf16(wv[k+1], &ds);
            FORM_ReplaceSelection(form, page, (FPDF_WIDESTRING)w);
            Tcl_DStringFree(&ds);
            FORM_ForceToKillFocus(form);

            /* NACHSEHEN, ob es gewirkt hat.
             *
             * Bei einem Kombinationsfeld OHNE Bearbeitungsflagge tut
             * ReplaceSelection nichts: man kann dort nur waehlen, nicht
             * schreiben. Gemessen an pdf4tcls demo-forms.pdf --
             * formfill meldete "1 gefuellt" und der Wert blieb leer.
             *
             * Ein stiller Falscherfolg ist schlimmer als eine Absage:
             * der Aufrufer haelt die Datei fuer fertig. Also
             * nachlesen und, wenn nichts ankam, die erlaubten Werte
             * NENNEN -- die haben wir seit dieser Fassung. */
            unsigned long pl = FPDFAnnot_GetFormFieldValue(form, annot,
                                                           NULL, 0);
            int gleich = 0;
            if (pl > 2) {
                unsigned short *pb = (unsigned short *)ckalloc(pl);
                FPDFAnnot_GetFormFieldValue(form, annot, pb, pl);
                Tcl_Obj *jetzt = _AnnotUtf16ToObj(interp, pb, pl);
                Tcl_IncrRefCount(jetzt);
                ckfree((char *)pb);
                gleich = (strcmp(Tcl_GetString(jetzt),
                                 Tcl_GetString(wv[k+1])) == 0);
                Tcl_DecrRefCount(jetzt);
            }
            if (!gleich) {
                Tcl_Obj *erlaubt = Tcl_NewListObj(0, NULL);
                int oc = FPDFAnnot_GetOptionCount(form, annot);
                for (int q = 0; q < oc; q++) {
                    unsigned long ll =
                        FPDFAnnot_GetOptionLabel(form, annot, q, NULL, 0);
                    if (ll <= 2) continue;
                    unsigned short *lb = (unsigned short *)ckalloc(ll);
                    FPDFAnnot_GetOptionLabel(form, annot, q, lb, ll);
                    Tcl_ListObjAppendElement(interp, erlaubt,
                            _AnnotUtf16ToObj(interp, lb, ll));
                    ckfree((char *)lb);
                }
                Tcl_Size ne;
                Tcl_ListObjLength(interp, erlaubt, &ne);
                if (ne > 0) {
                    Tcl_ListObjAppendElement(interp, fehlend,
                        Tcl_ObjPrintf("%s (value did not take; allowed: %s)",
                            wunschName, Tcl_GetString(erlaubt)));
                } else {
                    Tcl_ListObjAppendElement(interp, fehlend,
                        Tcl_ObjPrintf("%s (value did not take)", wunschName));
                }
                FPDFPage_CloseAnnot(annot);
                getroffen = 1;
                break;
            }

            FPDFPage_CloseAnnot(annot);
            gefuellt++;
            getroffen = 1;
            break;
        }
        if (!getroffen) {
            Tcl_ListObjAppendElement(interp, fehlend,
                    Tcl_NewStringObj(wunschName, -1));
        }
    }

    /* DAS AUSSEHEN AUF DIE ANDEREN WIDGETS DESSELBEN FELDES.
     *
     * Ein Feld kann auf mehreren Seiten stehen -- der Durchschlagsatz
     * eines Frachtbriefs. PDFium baut den Erscheinungsstrom nur fuer
     * das Widget neu, an dem der Fokus war. Gemessen 07.09.2026:
     * Vaterfeld mit /V(Muster), Widget 1 mit frischem Strom, Widget 2
     * mit dem alten leeren -- und /NeedAppearances steht nicht da, also
     * DARF ein Betrachter den alten nehmen. Auf dem Papier blieb Seite
     * 2 leer.
     *
     * Weder ein Aufruf je Seite noch "flatten -forms" half: der Wert
     * steht ja schon, es gibt fuer PDFium nichts neu zu bauen.
     *
     * Also wird der frische Strom KOPIERT. Nur bei gleicher GROESSE:
     * der Strom ist auf die BBox seines Widgets gerechnet, und auf ein
     * anders grosses Feld gelegt saesse der Text falsch. Verschiedene
     * Groessen werden GEMELDET statt still uebergangen.
     */
    for (Tcl_Size wi = 0; wi < wc; wi += 2) {
        const char *wn = Tcl_GetString(wv[wi]);
        FPDF_ANNOTATION quelle = NULL;
        FS_RECTF qr;
        memset(&qr, 0, sizeof(qr));
        int na = FPDFPage_GetAnnotCount(page);
        /* Erst das gefuellte Widget auf DIESER Seite finden. */
        for (int i = 0; i < na; i++) {
            FPDF_ANNOTATION a = FPDFPage_GetAnnot(page, i);
            if (!a) continue;
            unsigned long nl = FPDFAnnot_GetFormFieldName(form, a, NULL, 0);
            if (nl > 2) {
                unsigned short *nb = (unsigned short *)ckalloc(nl);
                FPDFAnnot_GetFormFieldName(form, a, nb, nl);
                Tcl_Obj *nm = _AnnotUtf16ToObj(interp, nb, nl);
                Tcl_IncrRefCount(nm);
                ckfree((char *)nb);
                if (strcmp(Tcl_GetString(nm), wn) == 0) {
                    Tcl_DecrRefCount(nm);
                    quelle = a;
                    FPDFAnnot_GetRect(a, &qr);
                    break;
                }
                Tcl_DecrRefCount(nm);
            }
            FPDFPage_CloseAnnot(a);
        }
        if (!quelle) continue;
        unsigned long al = FPDFAnnot_GetAP(quelle, FPDF_ANNOT_APPEARANCEMODE_NORMAL,
                                           NULL, 0);
        unsigned short *ab = NULL;
        if (al > 2) {
            ab = (unsigned short *)ckalloc(al);
            FPDFAnnot_GetAP(quelle, FPDF_ANNOT_APPEARANCEMODE_NORMAL, ab, al);
        }
        FPDFPage_CloseAnnot(quelle);
        if (!ab) continue;

        int seiten = FPDF_GetPageCount(doc);
        for (int sp = 0; sp < seiten; sp++) {
            if (sp == pagenum) continue;
            FPDF_PAGE ap = FPDF_LoadPage(doc, sp);
            if (!ap) continue;
            int an = FPDFPage_GetAnnotCount(ap);
            for (int i = 0; i < an; i++) {
                FPDF_ANNOTATION a = FPDFPage_GetAnnot(ap, i);
                if (!a) continue;
                unsigned long nl = FPDFAnnot_GetFormFieldName(form, a, NULL, 0);
                int passt = 0;
                if (nl > 2) {
                    unsigned short *nb = (unsigned short *)ckalloc(nl);
                    FPDFAnnot_GetFormFieldName(form, a, nb, nl);
                    Tcl_Obj *nm = _AnnotUtf16ToObj(interp, nb, nl);
                    Tcl_IncrRefCount(nm);
                    ckfree((char *)nb);
                    passt = (strcmp(Tcl_GetString(nm), wn) == 0);
                    Tcl_DecrRefCount(nm);
                }
                if (passt) {
                    FS_RECTF zr;
                    if (FPDFAnnot_GetRect(a, &zr)) {
                        double bq = qr.right - qr.left, hq = qr.top - qr.bottom;
                        double bz = zr.right - zr.left, hz = zr.top - zr.bottom;
                        if (fabs(bq - bz) < 0.01 && fabs(hq - hz) < 0.01) {
                            FPDFAnnot_SetAP(a,
                                    FPDF_ANNOT_APPEARANCEMODE_NORMAL,
                                    (FPDF_WIDESTRING)ab);
                        } else {
                            Tcl_ListObjAppendElement(interp, fehlend,
                                Tcl_ObjPrintf("%s (widget on page %d has a"
                                    " different size; appearance not copied)",
                                    wn, sp + 1));
                        }
                    }
                }
                FPDFPage_CloseAnnot(a);
            }
            FPDF_ClosePage(ap);
        }
        ckfree((char *)ab);
    }

    /* Nur die Seite abmelden -- die Umgebung gehoert dem Dokument. */
    FORM_OnBeforeClosePage(page, form);
    _DocFormSeiteAb(df);
    FPDF_ClosePage(page);

    Tcl_Size nf;
    Tcl_ListObjLength(interp, fehlend, &nf);
    if (nf > 0) {
        /* Nennen, WELCHE Namen nicht ankamen. "3 von 5 gefuellt" laesst
         * den Aufrufer suchen. */
        /* Eine Sammelmeldung fuer zwei verschiedene Faelle -- "nicht
         * gefunden" und "hat nicht gewirkt". Der Vorspann muss darum
         * neutral sein: "no such text field" war falsch, sobald der
         * zweite Fall dazukam, und eine falsche Meldung schickt den
         * Leser in die falsche Richtung. Was genau war, steht je
         * Eintrag dahinter. */
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "formfill: could not fill on page %d: %s",
            pagenum, Tcl_GetString(fehlend)));
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(gefuellt));
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* Anmerkungen erzeugen und entfernen                                  */
/*                                                                     */
/*   pdfium::addannot doc-handle pagenum typ {links unten rechts oben} */
/*                    ?-color {r g b}? ?-opacity 0..1?                 */
/*                    ?-contents text? ?-author name?                  */
/*   pdfium::delannot doc-handle pagenum index                         */
/*                                                                     */
/* typ: highlight, underline, strikeout, squiggly, square, text        */
/*                                                                     */
/* DER DRITTE WEG. Ein Wort hervorheben geht auf drei Arten:            */
/*                                                                     */
/*   stempeln   (tclpdfwriter) -- Original unberuehrt, Markierung liegt */
/*              darueber, aber sie ist Zeichnung und nicht wegzunehmen  */
/*   einbrennen (flatten) -- endgueltig Teil der Seite                  */
/*   ANMERKEN   -- bleibt entfernbar und maschinell lesbar              */
/*                                                                     */
/* Fuer "dieses Wort markieren" ist der dritte meist der richtige: ein  */
/* Betrachter kann sie anklicken, ausblenden, exportieren. Mit          */
/* "search -rects 1" hat man die Koordinaten dafuer schon.              */
/*                                                                     */
/* KEIN /AP VON UNS -- und das ist eine Zusage, keine Nachlaessigkeit.  */
/* Einen korrekten Appearance-Strom fuer ein Highlight zu bauen hiesse, */
/* Transparenzgruppen und Blend-Modi von Hand zu schreiben. Ohne /AP    */
/* zeichnen Betrachter die Markierung aus /QuadPoints und /C selbst;    */
/* "render -forms 1" tut das ebenfalls, also laesst es sich messen.     */
/* Das ist dieselbe Luecke, die pdf4tcls fillForms hat -- hier von      */
/* vornherein benannt statt spaeter entdeckt.                          */
/*                                                                     */
/* Nach dem Anlegen "save" rufen, sonst ist die Arbeit mit dem          */
/* Schliessen weg.                                                     */
/* ------------------------------------------------------------------ */
static int
_AnnotTypeFromName(const char *name)
{
    if (strcmp(name, "highlight") == 0) return FPDF_ANNOT_HIGHLIGHT;
    if (strcmp(name, "underline") == 0) return FPDF_ANNOT_UNDERLINE;
    if (strcmp(name, "strikeout") == 0) return FPDF_ANNOT_STRIKEOUT;
    if (strcmp(name, "squiggly")  == 0) return FPDF_ANNOT_SQUIGGLY;
    if (strcmp(name, "square")    == 0) return FPDF_ANNOT_SQUARE;
    if (strcmp(name, "text")      == 0) return FPDF_ANNOT_TEXT;
    return -1;
}


static int
PdfiumAddAnnotCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc < 5 || ((objc - 5) % 2) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv,
                "doc-handle pagenum type {left bottom right top} ?options?");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    const char *typname = Tcl_GetString(objv[3]);
    int subtype = _AnnotTypeFromName(typname);
    if (subtype < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "unknown annotation type \"%s\": must be highlight, underline,"
            " strikeout, squiggly, square or text", typname));
        return TCL_ERROR;
    }

    /* Das Rechteck wie ueberall in diesem Paket: {links unten rechts
     * oben}. PDFium nimmt FS_RECTF mit top VOR bottom -- dieselbe
     * Vertauschungsfalle wie bei GetCharBox. Nach aussen bleibt es
     * einheitlich, und die Umsortierung steht an genau einer Stelle. */
    Tcl_Obj **rv; Tcl_Size rc;
    if (Tcl_ListObjGetElements(interp, objv[4], &rc, &rv) != TCL_OK)
        return TCL_ERROR;
    if (rc != 4) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "rectangle needs {left bottom right top} in points", -1));
        return TCL_ERROR;
    }
    double l, u, r, t;
    if (Tcl_GetDoubleFromObj(interp, rv[0], &l) != TCL_OK ||
        Tcl_GetDoubleFromObj(interp, rv[1], &u) != TCL_OK ||
        Tcl_GetDoubleFromObj(interp, rv[2], &r) != TCL_OK ||
        Tcl_GetDoubleFromObj(interp, rv[3], &t) != TCL_OK)
        return TCL_ERROR;
    if (r <= l || t <= u) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "rectangle: right must exceed left and top must exceed bottom",
            -1));
        return TCL_ERROR;
    }

    double cr = 1.0, cg = 1.0, cb = 0.0, alpha = 1.0;
    int haveColor = 0;
    Tcl_Obj *contents = NULL, *author = NULL;
    for (int i = 5; i < objc; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        if (strcmp(opt, "-color") == 0) {
            Tcl_Obj **cv; Tcl_Size cc;
            if (Tcl_ListObjGetElements(interp, objv[i+1], &cc, &cv) != TCL_OK)
                return TCL_ERROR;
            if (cc != 3) {
                Tcl_SetObjResult(interp,
                    Tcl_NewStringObj("-color needs {r g b}, each 0..1", -1));
                return TCL_ERROR;
            }
            if (Tcl_GetDoubleFromObj(interp, cv[0], &cr) != TCL_OK ||
                Tcl_GetDoubleFromObj(interp, cv[1], &cg) != TCL_OK ||
                Tcl_GetDoubleFromObj(interp, cv[2], &cb) != TCL_OK)
                return TCL_ERROR;
            haveColor = 1;
        } else if (strcmp(opt, "-opacity") == 0) {
            if (Tcl_GetDoubleFromObj(interp, objv[i+1], &alpha) != TCL_OK)
                return TCL_ERROR;
            if (alpha < 0.0 || alpha > 1.0) {
                Tcl_SetObjResult(interp,
                    Tcl_NewStringObj("-opacity must be between 0 and 1", -1));
                return TCL_ERROR;
            }
            haveColor = 1;
        } else if (strcmp(opt, "-contents") == 0) {
            contents = objv[i+1];
        } else if (strcmp(opt, "-author") == 0) {
            author = objv[i+1];
        } else {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "unknown option \"%s\": must be -color, -opacity,"
                " -contents or -author", opt));
            return TCL_ERROR;
        }
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    FPDF_ANNOTATION a = FPDFPage_CreateAnnot(page,
            (FPDF_ANNOTATION_SUBTYPE)subtype);
    if (!a) {
        FPDF_ClosePage(page);
        PDFIUM_ERROR(interp, "cannot create annotation");
    }

    FS_RECTF rect;
    rect.left = (float)l; rect.bottom = (float)u;
    rect.right = (float)r; rect.top = (float)t;
    FPDFAnnot_SetRect(a, &rect);

    /* Textmarkierungen brauchen /QuadPoints, sonst zeichnet ein
     * Betrachter nichts -- das Rechteck allein genuegt ihnen nicht
     * (ISO 32000-1 12.5.6.10). Fuer ein einzelnes Rechteck sind die
     * vier Punkte die Ecken, und zwar in der Reihenfolge
     * oben-links, oben-rechts, unten-links, unten-rechts. */
    if (subtype == FPDF_ANNOT_HIGHLIGHT || subtype == FPDF_ANNOT_UNDERLINE ||
        subtype == FPDF_ANNOT_STRIKEOUT || subtype == FPDF_ANNOT_SQUIGGLY) {
        FS_QUADPOINTSF q;
        q.x1 = (float)l; q.y1 = (float)t;
        q.x2 = (float)r; q.y2 = (float)t;
        q.x3 = (float)l; q.y3 = (float)u;
        q.x4 = (float)r; q.y4 = (float)u;
        FPDFAnnot_AppendAttachmentPoints(a, &q);
    }

    if (haveColor) {
        FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_Color,
                (unsigned int)(cr * 255.0 + 0.5),
                (unsigned int)(cg * 255.0 + 0.5),
                (unsigned int)(cb * 255.0 + 0.5),
                (unsigned int)(alpha * 255.0 + 0.5));
    }
    if (contents) {
        Tcl_DString ds;
        const unsigned short *w = _ToUtf16(contents, &ds);
        FPDFAnnot_SetStringValue(a, "Contents", (FPDF_WIDESTRING)w);
        Tcl_DStringFree(&ds);
    }
    if (author) {
        Tcl_DString ds;
        const unsigned short *w = _ToUtf16(author, &ds);
        FPDFAnnot_SetStringValue(a, "T", (FPDF_WIDESTRING)w);
        Tcl_DStringFree(&ds);
    }

    int idx = FPDFPage_GetAnnotIndex(page, a);
    FPDFPage_CloseAnnot(a);
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(idx));
    return TCL_OK;
}

static int
PdfiumDelAnnotCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum index");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum, idx;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[3], &idx) != TCL_OK) return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");
    int n = FPDFPage_GetAnnotCount(page);
    if (idx < 0 || idx >= n) {
        FPDF_ClosePage(page);
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "annotation index %d out of range (0..%d)", idx, n - 1));
        return TCL_ERROR;
    }
    FPDF_BOOL ok = FPDFPage_RemoveAnnot(page, idx);
    int rest = FPDFPage_GetAnnotCount(page);
    FPDF_ClosePage(page);
    if (!ok) PDFIUM_ERROR(interp, "cannot remove annotation");
    Tcl_SetObjResult(interp, Tcl_NewIntObj(rest));
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::signatures doc-handle                                       */
/*                                                                     */
/* Je Signatur ein dict:                                               */
/*                                                                     */
/*   index      laufende Nummer                                        */
/*   subfilter  das Verfahren, etwa "adbe.pkcs7.detached"              */
/*   reason     der angegebene Grund                                    */
/*   time       Zeitpunkt als D:YYYYMMDDHHMMSS+XX'YY'                  */
/*   docmdp     1, 2 oder 3 -- was nach der Signatur noch erlaubt ist   */
/*   ranges     die /ByteRange-Zahlen                                   */
/*   covered    wieviele Bytes die Signatur abdeckt                     */
/*   size       Laenge des PKCS#7-Blocks                                */
/*                                                                     */
/* WAS DAS NICHT IST: eine PRUEFUNG. PDFium liefert die Bestandteile,   */
/* es rechnet nichts nach. Ob die Signatur gueltig ist, ob das          */
/* Zertifikat taugt, ob es zurueckgezogen wurde -- nichts davon steht   */
/* hier. Wer aus "signatures gibt etwas zurueck" schliesst "das         */
/* Dokument ist unversehrt", irrt sich, und zwar in der gefaehrlichen   */
/* Richtung.                                                            */
/*                                                                     */
/* WOFUER ES TROTZDEM TAUGT: "covered" gegen die Dateigroesse. Deckt    */
/* die Signatur weniger ab als die Datei gross ist, wurde nach dem      */
/* Unterschreiben etwas angehaengt -- eine inkrementelle Aenderung.     */
/* Das ist keine Pruefung, aber ein Hinweis, den man ohne Krypto        */
/* bekommt.                                                             */
/* ------------------------------------------------------------------ */
static Tcl_Obj *
_SigAscii(FPDF_SIGNATURE sig,
          unsigned long (*fn)(FPDF_SIGNATURE, void *, unsigned long))
{
    unsigned long len = fn(sig, NULL, 0);
    if (len <= 1) return Tcl_NewStringObj("", 0);
    char *buf = ckalloc(len + 1);
    fn(sig, buf, len);
    buf[len] = 0;
    /* Die Laenge schliesst die abschliessende Null ein; ohne das
     * Abschneiden haengt bei jedem Wert ein Nullbyte an, und das faellt
     * erst auf, wenn jemand vergleicht. */
    Tcl_Size n = (Tcl_Size)len;
    while (n > 0 && buf[n - 1] == 0) n--;
    Tcl_Obj *o = Tcl_NewStringObj(buf, n);
    ckfree(buf);
    return o;
}

static int
PdfiumSignaturesCmd(ClientData cd, Tcl_Interp *interp,
                    int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    int n = FPDF_GetSignatureCount(doc);
    for (int i = 0; i < n; i++) {
        FPDF_SIGNATURE sig = FPDF_GetSignatureObject(doc, i);
        if (!sig) continue;
        Tcl_Obj *d = Tcl_NewListObj(0, NULL);

#define SIG_PUT(k, v) do { \
            Tcl_ListObjAppendElement(interp, d, Tcl_NewStringObj((k), -1)); \
            Tcl_ListObjAppendElement(interp, d, (v)); \
        } while (0)

        SIG_PUT("index", Tcl_NewIntObj(i));

        /* subfilter und time sind 7-Bit-ASCII, reason ist UTF-16LE --
         * drei Kodierungen in einer Schnittstelle. Wer sie gleich
         * behandelt, bekommt bei "reason" jedes zweite Zeichen als
         * Null. Steht so in fpdf_signature.h. */
        SIG_PUT("subfilter", _SigAscii(sig, (unsigned long (*)(FPDF_SIGNATURE,
                void *, unsigned long))FPDFSignatureObj_GetSubFilter));
        SIG_PUT("time", _SigAscii(sig, (unsigned long (*)(FPDF_SIGNATURE,
                void *, unsigned long))FPDFSignatureObj_GetTime));

        unsigned long rl = FPDFSignatureObj_GetReason(sig, NULL, 0);
        if (rl > 2) {
            unsigned short *rb = (unsigned short *)ckalloc(rl);
            FPDFSignatureObj_GetReason(sig, rb, rl);
            SIG_PUT("reason", _AnnotUtf16ToObj(interp, rb, rl));
            ckfree((char *)rb);
        } else {
            SIG_PUT("reason", Tcl_NewStringObj("", 0));
        }

        SIG_PUT("docmdp",
                Tcl_NewIntObj((int)FPDFSignatureObj_GetDocMDPPermission(sig)));

        /* ByteRange: Paare aus Anfang und Laenge. Die Summe der Laengen
         * ist, was die Signatur abdeckt. */
        unsigned long anz = FPDFSignatureObj_GetByteRange(sig, NULL, 0);
        Tcl_Obj *ranges = Tcl_NewListObj(0, NULL);
        Tcl_WideInt covered = 0;
        if (anz > 0) {
            int *rb = (int *)ckalloc(anz * sizeof(int));
            FPDFSignatureObj_GetByteRange(sig, rb, anz);
            for (unsigned long k = 0; k < anz; k++) {
                Tcl_ListObjAppendElement(interp, ranges, Tcl_NewIntObj(rb[k]));
                if (k % 2 == 1) { covered += rb[k]; }
            }
            ckfree((char *)rb);
        }
        SIG_PUT("ranges", ranges);
        SIG_PUT("covered", Tcl_NewWideIntObj(covered));
        SIG_PUT("size",
                Tcl_NewWideIntObj((Tcl_WideInt)
                        FPDFSignatureObj_GetContents(sig, NULL, 0)));
#undef SIG_PUT
        Tcl_ListObjAppendElement(interp, result, d);
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* Eingebettete Dateien (Anhaenge)                                     */
/*                                                                     */
/*   pdfium::attachments doc-handle                                    */
/*       Liste von {index name groesse}                                */
/*   pdfium::attachment  doc-handle index                              */
/*       der INHALT als Bytefolge                                      */
/*   pdfium::addattachment doc-handle name daten                       */
/*   pdfium::delattachment doc-handle index                            */
/*                                                                     */
/* WOZU: eine Rechnung nach ZUGFeRD traegt ihre XML-Fassung als Anhang, */
/* und ein Frachtbrief kann Belege mitfuehren. tclpdfreader holt sie    */
/* heute ueber qpdf -- also ueber ein externes Programm, das auf einer  */
/* Maschine fehlen kann. Hier gehen sie nativ, und SCHREIBEN geht auch. */
/*                                                                     */
/* UEBER DEN INDEX, nicht ueber den Namen: Namen duerfen sich           */
/* wiederholen (ISO 32000-1 7.11.4 verlangt keine Eindeutigkeit).       */
/* Wer nach Namen loeschte, traefe womoeglich den falschen -- und       */
/* merkte es nicht. "attachments" nennt den Index gleich mit.           */
/* ------------------------------------------------------------------ */
static int
PdfiumAttachmentsCmd(ClientData cd, Tcl_Interp *interp,
                     int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    int n = FPDFDoc_GetAttachmentCount(doc);
    for (int i = 0; i < n; i++) {
        FPDF_ATTACHMENT a = FPDFDoc_GetAttachment(doc, i);
        if (!a) continue;
        Tcl_Obj *e = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, e, Tcl_NewIntObj(i));

        unsigned long nl = FPDFAttachment_GetName(a, NULL, 0);
        if (nl > 2) {
            unsigned short *nb = (unsigned short *)ckalloc(nl);
            FPDFAttachment_GetName(a, nb, nl);
            Tcl_ListObjAppendElement(interp, e,
                    _AnnotUtf16ToObj(interp, nb, nl));
            ckfree((char *)nb);
        } else {
            Tcl_ListObjAppendElement(interp, e, Tcl_NewStringObj("", 0));
        }

        /* Die Groesse OHNE den Inhalt zu holen: GetFile mit einem
         * Nullpuffer schreibt nur die noetige Laenge. Eine Liste aller
         * Anhaenge soll nicht zwanzig Megabyte in den Speicher ziehen,
         * nur damit jemand die Namen sieht. */
        unsigned long fl = 0;
        if (!FPDFAttachment_GetFile(a, NULL, 0, &fl)) { fl = 0; }
        Tcl_ListObjAppendElement(interp, e, Tcl_NewWideIntObj((Tcl_WideInt)fl));
        Tcl_ListObjAppendElement(interp, result, e);
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int
PdfiumAttachmentCmd(ClientData cd, Tcl_Interp *interp,
                    int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle index");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int idx;
    if (Tcl_GetIntFromObj(interp, objv[2], &idx) != TCL_OK)
        return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;
    int n = FPDFDoc_GetAttachmentCount(doc);
    if (idx < 0 || idx >= n) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("attachment index %d out of range (0..%d)",
                          idx, n - 1));
        return TCL_ERROR;
    }
    FPDF_ATTACHMENT a = FPDFDoc_GetAttachment(doc, idx);
    if (!a) PDFIUM_ERROR(interp, "cannot get attachment");

    unsigned long need = 0;
    if (!FPDFAttachment_GetFile(a, NULL, 0, &need) || need == 0) {
        /* Ein Anhang OHNE Inhalt ist kein Fehler: der Eintrag kann da
         * sein und die Datei fehlen. Eine leere Bytefolge sagt das. */
        Tcl_SetObjResult(interp, Tcl_NewByteArrayObj((unsigned char *)"", 0));
        return TCL_OK;
    }
    unsigned char *buf = (unsigned char *)ckalloc(need);
    unsigned long got = 0;
    if (!FPDFAttachment_GetFile(a, buf, need, &got)) {
        ckfree((char *)buf);
        PDFIUM_ERROR(interp, "cannot read attachment");
    }
    /* Als BYTEARRAY, nicht als Zeichenkette: ein Anhang ist beliebiges
     * Binaermaterial, und eine Zeichenkette wuerde es durch die
     * Systemkodierung schicken. */
    Tcl_SetObjResult(interp, Tcl_NewByteArrayObj(buf, (Tcl_Size)got));
    ckfree((char *)buf);
    return TCL_OK;
}

static int
PdfiumAddAttachmentCmd(ClientData cd, Tcl_Interp *interp,
                       int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle name data");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;

    /* Der Name geht als UTF-16LE hinein -- derselbe Weg wie bei
     * "search". */
    Tcl_DString nameDs;
    Tcl_DStringInit(&nameDs);
    const char *name_utf8 = Tcl_GetString(objv[2]);
    Tcl_Encoding tenc = Tcl_GetEncoding(NULL, "utf-16le");
    if (!tenc) tenc = Tcl_GetEncoding(NULL, "unicode");
    if (tenc) {
        Tcl_UtfToExternalDString(tenc, name_utf8, -1, &nameDs);
        Tcl_FreeEncoding(tenc);
    }
    { char _z[2] = {0,0}; Tcl_DStringAppend(&nameDs, _z, 2); }
    const unsigned short *nameUni =
        (const unsigned short *)Tcl_DStringValue(&nameDs);

    FPDF_ATTACHMENT a = FPDFDoc_AddAttachment(doc, (FPDF_WIDESTRING)nameUni);
    Tcl_DStringFree(&nameDs);
    if (!a) PDFIUM_ERROR(interp, "cannot add attachment");

    Tcl_Size len = 0;
    unsigned char *data = Tcl_GetByteArrayFromObj(objv[3], &len);
    if (!FPDFAttachment_SetFile(a, doc, data, (unsigned long)len)) {
        PDFIUM_ERROR(interp, "cannot write attachment contents");
    }
    /* Den Index zurueckgeben, nicht den Namen: damit laesst sich der
     * neue Anhang sofort wieder ansprechen, ohne die Liste zu
     * durchsuchen. */
    Tcl_SetObjResult(interp,
            Tcl_NewIntObj(FPDFDoc_GetAttachmentCount(doc) - 1));
    return TCL_OK;
}

static int
PdfiumDelAttachmentCmd(ClientData cd, Tcl_Interp *interp,
                       int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle index");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int idx;
    if (Tcl_GetIntFromObj(interp, objv[2], &idx) != TCL_OK)
        return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;
    int n = FPDFDoc_GetAttachmentCount(doc);
    if (idx < 0 || idx >= n) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("attachment index %d out of range (0..%d)",
                          idx, n - 1));
        return TCL_ERROR;
    }
    if (!FPDFDoc_DeleteAttachment(doc, idx)) {
        PDFIUM_ERROR(interp, "cannot delete attachment");
    }
    /* PDFium entfernt den EINTRAG, nicht die Daten aus der Datei -- das
     * steht so in fpdf_attachment.h. Wer eine Datei loswerden will,
     * muss danach neu schreiben lassen; "save" allein reicht nicht
     * zwingend. Das gehoert dokumentiert, sonst haelt es jemand fuer
     * ein Loeschen. */
    Tcl_SetObjResult(interp,
            Tcl_NewIntObj(FPDFDoc_GetAttachmentCount(doc)));
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::catalog doc-handle                                          */
/*                                                                     */
/* Was der Katalog ueber das GANZE Dokument sagt:                       */
/*                                                                     */
/*   tagged    1, wenn ein Strukturbaum da ist (/MarkInfo /Marked)     */
/*   language  der /Lang-Eintrag, "" wenn keiner                        */
/*                                                                     */
/* WOZU "tagged": ein Pruefer der Lesereihenfolge fragt sonst indirekt  */
/* ueber einen leeren Strukturbaum -- und kann nicht unterscheiden      */
/* zwischen "nicht ausgezeichnet" und "diese eine Seite hat nichts".    */
/* Die Frage gehoert ans Dokument, nicht an die Seite.                  */
/*                                                                     */
/* WOZU "language": ohne /Lang weiss ein Vorleseprogramm nicht, in      */
/* welcher Sprache es lesen soll, und spricht deutschen Text englisch   */
/* aus. PDF/UA verlangt den Eintrag; er fehlt aber haeufig, und man     */
/* sieht es dem Dokument nicht an.                                      */
/*                                                                     */
/* GRENZE: "tagged" sagt nur, dass ein Baum DA ist -- nicht, dass er    */
/* etwas taugt. Ein Dokument, in dem jeder Absatz P heisst, ist         */
/* getaggt und sagt einem Leser trotzdem nichts.                        */
/* ------------------------------------------------------------------ */
static int
PdfiumCatalogCmd(ClientData cd, Tcl_Interp *interp,
                 int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj("tagged", -1));
    Tcl_ListObjAppendElement(interp, result,
            Tcl_NewIntObj(FPDFCatalog_IsTagged(doc) ? 1 : 0));

    Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj("language", -1));
    /* GetLanguage gibt bei fehlendem /Lang die Laenge 2 zurueck (die
     * leere Zeichenkette mit Abschluss), bei einem Fehler 0. Beides
     * ergibt aussen "" -- der Unterschied zwischen "kein Eintrag" und
     * "Fehler" ist hier keiner, den ein Aufrufer nutzen koennte. */
    unsigned long len = FPDFCatalog_GetLanguage(doc, NULL, 0);
    if (len <= 2) {
        Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj("", 0));
    } else {
        unsigned short *buf = (unsigned short *)ckalloc(len);
        FPDFCatalog_GetLanguage(doc, buf, len);
        Tcl_ListObjAppendElement(interp, result,
                _AnnotUtf16ToObj(interp, buf, len));
        ckfree((char *)buf);
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::flatten doc-handle pagenum ?-mode display|print?            */
/*                                                                     */
/* Anmerkungen und Formularfelder in den SEITENINHALT einbrennen.       */
/*                                                                     */
/* Danach sind sie Zeichnung: nicht mehr anklickbar, nicht mehr         */
/* entfernbar, aber auch nicht mehr davon abhaengig, ob ein Betrachter  */
/* sie darstellt. Genau das meint "if you need the annotations burned   */
/* in" -- ein Kommentar oder ein ausgefuelltes Feld, das ueberall gleich*/
/* aussieht.                                                           */
/*                                                                     */
/* Das ist der Gegenweg zum Ueberlagern: dort bleibt das Original       */
/* unberuehrt, hier wird es geaendert. Wer beides will, legt erst       */
/* darueber und brennt dann ein.                                       */
/*                                                                     */
/* Rueckgabe: "flattened", "nothing" (nichts einzubrennen) -- ein       */
/* Fehlschlag wird als Fehler gemeldet. PDFium nennt dabei KEINEN       */
/* Grund; das steht so in fpdf_flatten.h und laesst sich hier nicht     */
/* verbessern.                                                          */
/*                                                                     */
/* Die Seite ist danach im SPEICHER geaendert. Wer das behalten will,   */
/* muss pdfium::save rufen -- sonst ist die Arbeit mit dem Schliessen   */
/* weg.                                                                 */
/* ------------------------------------------------------------------ */
static int
PdfiumFlattenCmd(ClientData cd, Tcl_Interp *interp,
                 int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3 && objc != 5 && objc != 7) {
        Tcl_WrongNumArgs(interp, 1, objv,
                         "doc-handle pagenum ?-mode display|print?"
                         " ?-forms 0|1?");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    int flag = FLAT_NORMALDISPLAY;
    int withForms = 0;
    for (int i = 3; i + 1 < objc; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        if (strcmp(opt, "-forms") == 0) {
            /* Die Formularschicht VOR dem Einbrennen aufbauen.
             *
             * Der Grund: ein mit pdf4tcl::fillForms gefuelltes Feld
             * traegt den Wert in /V und schaltet /NeedAppearances, laesst
             * den Appearance-Strom aber leer. flatten brennt dann den
             * LEEREN Strom ein -- und der Wert ist danach ganz weg,
             * schlimmer als vorher. Gemessen am 05.09.2026: 375 dunkle
             * Punkte vor und nach dem Einbrennen, waehrend
             * "render -forms 1" 763 zeigte.
             *
             * Die Formularumgebung erzeugt die fehlenden Stroeme, und
             * erst danach hat flatten etwas zu uebernehmen. */
            if (Tcl_GetIntFromObj(interp, objv[i+1], &withForms) != TCL_OK)
                return TCL_ERROR;
            continue;
        }
        if (strcmp(opt, "-mode") != 0) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown option \"%s\": must be -mode or -forms",
                              opt));
            return TCL_ERROR;
        }
        const char *mode = Tcl_GetString(objv[i+1]);
        if (strcmp(mode, "display") == 0) {
            flag = FLAT_NORMALDISPLAY;
        } else if (strcmp(mode, "print") == 0) {
            /* Der Unterschied zaehlt bei Anmerkungen, die nur fuer den
             * Bildschirm oder nur fuers Papier gedacht sind -- dieselbe
             * Unterscheidung wie /Usage bei den Ebenen. */
            flag = FLAT_PRINT;
        } else {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown mode \"%s\": must be display or print",
                              mode));
            return TCL_ERROR;
        }
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    FPDF_FORMHANDLE form = NULL;
    PdfiumDocForm *df = NULL;
    if (withForms) {
        df = _DocFormGet(doc, page, pagenum);
        if (df) {
            form = df->form;
            FORM_OnAfterLoadPage(page, form);
        }
    }

    int rc = FPDFPage_Flatten(page, flag);

    if (form) {
        /* Nur die Seite abmelden -- die Umgebung gehoert dem Dokument. */
        FORM_OnBeforeClosePage(page, form);
        if (df) {
            /* Die Seite der Sitzung wieder eintragen, wenn es eine
             * gibt -- sonst faende FFI_GetPage nichts mehr. */
            _DocFormSeiteAb(df);
        }
    }
    FPDF_ClosePage(page);

    switch (rc) {
        case FLATTEN_SUCCESS:
            Tcl_SetObjResult(interp, Tcl_NewStringObj("flattened", -1));
            return TCL_OK;
        case FLATTEN_NOTHINGTODO:
            /* Kein Fehler: eine Seite ohne Anmerkungen und ohne
             * Formularfelder hat nichts einzubrennen. Das als Fehler zu
             * melden wuerde jeden Stapellauf ueber ein Dokument
             * abbrechen, in dem eine Seite leer ist. */
            Tcl_SetObjResult(interp, Tcl_NewStringObj("nothing", -1));
            return TCL_OK;
        default:
            PDFIUM_ERROR(interp,
                "flatten failed (PDFium gives no reason)");
    }
}

/* ------------------------------------------------------------------ */
/* pdfium::charboxes doc-handle pagenum ?-range {start count}?         */
/*                                                                     */
/* Ein Rechteck je ZEICHEN: {zeichen {links unten rechts oben}}.       */
/*                                                                     */
/* gettext gibt den Text, search die Rechtecke ganzer Treffer,          */
/* pageobjects die eines Objekts. Was dazwischen fehlte, ist die        */
/* feinste Stufe: wo steht dieses eine Zeichen. Damit laesst sich in    */
/* einem Wort etwas hervorheben, ein Zeilenumbruch nachvollziehen oder  */
/* Text neu setzen.                                                    */
/*                                                                     */
/* Ohne -range die ganze Seite -- das koennen Tausende Eintraege sein.  */
/* Mit -range {start count} nur der Ausschnitt, und start/count sind    */
/* genau die zwei Zahlen, die search ohne -rects zurueckgibt.          */
/* ------------------------------------------------------------------ */
static int
PdfiumCharBoxesCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3 && objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv,
                         "doc-handle pagenum ?-range {start count}?");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    int haveRange = 0, rStart = 0, rCount = 0;
    if (objc == 5) {
        const char *opt = Tcl_GetString(objv[3]);
        if (strcmp(opt, "-range") != 0) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown option \"%s\": must be -range", opt));
            return TCL_ERROR;
        }
        Tcl_Obj **rv; Tcl_Size rc;
        if (Tcl_ListObjGetElements(interp, objv[4], &rc, &rv) != TCL_OK)
            return TCL_ERROR;
        if (rc != 2) {
            Tcl_SetObjResult(interp,
                Tcl_NewStringObj("-range needs {start count}", -1));
            return TCL_ERROR;
        }
        if (Tcl_GetIntFromObj(interp, rv[0], &rStart) != TCL_OK ||
            Tcl_GetIntFromObj(interp, rv[1], &rCount) != TCL_OK)
            return TCL_ERROR;
        if (rStart < 0 || rCount < 0) {
            Tcl_SetObjResult(interp,
                Tcl_NewStringObj("-range: start and count must not be negative",
                                 -1));
            return TCL_ERROR;
        }
        haveRange = 1;
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) {
        FPDF_ClosePage(page);
        PDFIUM_ERROR(interp, "cannot load text page");
    }

    int n = FPDFText_CountChars(tp);
    int from = 0, to = n;
    if (haveRange) {
        from = rStart;
        to   = rStart + rCount;
        /* Abschneiden statt melden: ein Treffer am Seitenende darf
         * nicht daran scheitern, dass jemand eins zu weit gezaehlt hat.
         * Ein Bereich, der GANZ ausserhalb liegt, ergibt eine leere
         * Liste -- auch das ist eine Antwort. */
        if (from > n) from = n;
        if (to   > n) to   = n;
    }

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    for (int i = from; i < to; i++) {
        double l, r, b, t;
        if (!FPDFText_GetCharBox(tp, i, &l, &r, &b, &t)) continue;
        unsigned int uc = FPDFText_GetUnicode(tp, i);
        Tcl_Obj *e = Tcl_NewListObj(0, NULL);
        char utf[8];
        int len = Tcl_UniCharToUtf((int)uc, utf);
        Tcl_ListObjAppendElement(interp, e, Tcl_NewStringObj(utf, len));
        Tcl_Obj *box = Tcl_NewListObj(0, NULL);
        /* PDFium gibt hier left, RIGHT, bottom, top heraus -- eine
         * andere Reihenfolge als bei GetRect (left, top, right,
         * bottom). Wer beide gleich behandelt, vertauscht Kanten.
         * Nach aussen steht ueberall {links unten rechts oben}. */
        Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(l));
        Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(b));
        Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(r));
        Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(t));
        Tcl_ListObjAppendElement(interp, e, box);
        Tcl_ListObjAppendElement(interp, result, e);
    }

    FPDFText_ClosePage(tp);
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}


/* ------------------------------------------------------------------ */
/* pdfium::images doc-handle pagenum                                   */
/*                                                                     */
/* Je Bild auf der Seite ein dict:                                     */
/*                                                                     */
/*   index    Objektnummer, wie bei pageobjects                        */
/*   box      {links unten rechts oben} in Punkt                       */
/*   width    Breite in BILDPUNKTEN                                    */
/*   height   Hoehe in Bildpunkten                                     */
/*   dpix     waagerechte Aufloesung, wie sie AUF DEM BLATT ankommt    */
/*   dpiy     senkrechte                                               */
/*   bpp      Bits je Bildpunkt                                        */
/*   mcid     Marked-Content-ID, -1 wenn keine                         */
/*                                                                     */
/* WOZU: ein Scan kann tadellos aussehen und beim Druck flau werden --  */
/* das merkt man am Ergebnis, wenn das Blatt schon durch ist. "dpix"    */
/* sagt es vorher.                                                     */
/*                                                                     */
/* Die Zahl kommt VON PDFIUM, nicht aus einer eigenen Rechnung: es      */
/* setzt Bildpunkte gegen das Rechteck auf dem Blatt und beruecksichtigt*/
/* dabei die Transformationsmatrix des Objekts. Ein Bild kann gedreht   */
/* oder verzerrt eingesetzt sein, und dann sind waagerecht und senkrecht*/
/* verschieden -- "Breite durch Punkte" waere in dem Fall falsch, und   */
/* zwar unauffaellig falsch.                                           */
/*                                                                     */
/* KEINE Bewertung: was zu wenig ist, haengt am Druckweg. 150 dpi sind  */
/* fuer einen Bueroausdruck reichlich und fuer eine Druckerei zu wenig. */
/* Das Werkzeug nennt die Zahl, die Entscheidung bleibt beim Leser.     */
/* ------------------------------------------------------------------ */
static int
PdfiumImagesCmd(ClientData cd, Tcl_Interp *interp,
                int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    int n = FPDFPage_CountObjects(page);
    for (int i = 0; i < n; i++) {
        FPDF_PAGEOBJECT o = FPDFPage_GetObject(page, i);
        if (!o) continue;
        if (FPDFPageObj_GetType(o) != FPDF_PAGEOBJ_IMAGE) continue;

        FPDF_IMAGEOBJ_METADATA md;
        memset(&md, 0, sizeof(md));
        if (!FPDFImageObj_GetImageMetadata(o, page, &md)) continue;

        Tcl_Obj *d = Tcl_NewListObj(0, NULL);
#define IMG_PUT(k, v) do { \
            Tcl_ListObjAppendElement(interp, d, Tcl_NewStringObj((k), -1)); \
            Tcl_ListObjAppendElement(interp, d, (v)); \
        } while (0)
        IMG_PUT("index",  Tcl_NewIntObj(i));
        float l, u, r, t;
        Tcl_Obj *box = Tcl_NewListObj(0, NULL);
        if (FPDFPageObj_GetBounds(o, &l, &u, &r, &t)) {
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(l));
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(u));
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(r));
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(t));
        }
        IMG_PUT("box",    box);
        IMG_PUT("width",  Tcl_NewWideIntObj((Tcl_WideInt)md.width));
        IMG_PUT("height", Tcl_NewWideIntObj((Tcl_WideInt)md.height));
        IMG_PUT("dpix",   Tcl_NewDoubleObj(md.horizontal_dpi));
        IMG_PUT("dpiy",   Tcl_NewDoubleObj(md.vertical_dpi));
        IMG_PUT("bpp",    Tcl_NewIntObj((int)md.bits_per_pixel));
        IMG_PUT("mcid",   Tcl_NewIntObj(md.marked_content_id));
#undef IMG_PUT
        Tcl_ListObjAppendElement(interp, result, d);
    }
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::pageobjects doc-handle pagenum ?-marks 0|1?                 */
/*                                                                     */
/* Woraus besteht die Seite? Liefert je Objekt                          */
/*   {index typ {links unten rechts oben}}                             */
/* mit typ aus text, path, image, shading, form, unknown und dem        */
/* Rechteck in PUNKT, Seitenkoordinaten, Ursprung unten links -- also   */
/* dieselben Zahlen wie search -rects und render -clip.                 */
/*                                                                     */
/* gettext sagt, WAS auf der Seite steht, structure sagt, wie es        */
/* ausgezeichnet ist. Woraus sie GEZEICHNET ist, sagte bisher nichts:   */
/* ob ein Kasten ein Pfad oder ein Bild ist, ob hinter dem Text ein     */
/* Scan liegt, wo ein Form-XObject sitzt.                               */
/* ------------------------------------------------------------------ */
static int
PdfiumPageObjectsCmd(ClientData cd, Tcl_Interp *interp,
                     int objc, Tcl_Obj *const objv[])
{
    if (objc < 3 || (objc % 2) == 0) {
        Tcl_WrongNumArgs(interp, 1, objv,
                "doc-handle pagenum ?-marks 0|1? ?-fonts 0|1?");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    int wantMarks = 0;
    int wantFonts = 0;
    for (int i = 3; i + 1 < objc; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        int *ziel;
        if (strcmp(opt, "-marks") == 0)      { ziel = &wantMarks; }
        else if (strcmp(opt, "-fonts") == 0) { ziel = &wantFonts; }
        else {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "unknown option \"%s\": must be -marks or -fonts", opt));
            return TCL_ERROR;
        }
        if (Tcl_GetIntFromObj(interp, objv[i+1], ziel) != TCL_OK)
            return TCL_ERROR;
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    int n = FPDFPage_CountObjects(page);
    for (int i = 0; i < n; i++) {
        FPDF_PAGEOBJECT o = FPDFPage_GetObject(page, i);
        if (!o) continue;
        const char *typ;
        switch (FPDFPageObj_GetType(o)) {
            case FPDF_PAGEOBJ_TEXT:    typ = "text";    break;
            case FPDF_PAGEOBJ_PATH:    typ = "path";    break;
            case FPDF_PAGEOBJ_IMAGE:   typ = "image";   break;
            case FPDF_PAGEOBJ_SHADING: typ = "shading"; break;
            case FPDF_PAGEOBJ_FORM:    typ = "form";    break;
            default:                   typ = "unknown"; break;
        }
        Tcl_Obj *e = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, e, Tcl_NewIntObj(i));
        Tcl_ListObjAppendElement(interp, e, Tcl_NewStringObj(typ, -1));
        float l, u, r, t;
        Tcl_Obj *box = Tcl_NewListObj(0, NULL);
        /* Kein Rechteck ist kein Fehler: ein leeres Objekt hat keines.
         * Eine leere Liste sagt das, eine Liste aus Nullen wuerde
         * behaupten, es liege in der Ecke. */
        if (FPDFPageObj_GetBounds(o, &l, &u, &r, &t)) {
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(l));
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(u));
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(r));
            Tcl_ListObjAppendElement(interp, box, Tcl_NewDoubleObj(t));
        }
        Tcl_ListObjAppendElement(interp, e, box);
        /* SCHRIFT je Textobjekt -- nur bei -fonts, damit die
         * Rueckgabe nicht fuer jeden waechst, der nur Rechtecke will.
         *
         * {name groesse eingebettet flags}. Beim Nachbauen eines
         * fremden Vordrucks die entscheidende Auskunft: mit welcher
         * Schrift und in welcher Groesse steht da etwas. Und fuer
         * readorder/overlaps eine bessere Grundlage als das blosse
         * Rechteck -- eine 6-Punkt-Zeile und eine Ueberschrift sehen
         * als Rechteck gleich aus.
         *
         * "eingebettet" ist die Frage, die ueber Portabilitaet
         * entscheidet: eine nicht eingebettete Schrift sieht auf einer
         * anderen Maschine anders aus, und das faellt erst dort auf.
         */
        if (wantFonts) {
            Tcl_Obj *fi = Tcl_NewListObj(0, NULL);
            if (FPDFPageObj_GetType(o) == FPDF_PAGEOBJ_TEXT) {
                FPDF_FONT fo = FPDFTextObj_GetFont(o);
                char nm[128];
                nm[0] = 0;
                if (fo) {
                    size_t got = FPDFFont_GetBaseFontName(fo, nm, sizeof(nm));
                    if (got == 0 || got > sizeof(nm)) nm[0] = 0;
                }
                Tcl_ListObjAppendElement(interp, fi,
                        Tcl_NewStringObj(nm, -1));
                float fs = 0.0f;
                FPDFTextObj_GetFontSize(o, &fs);
                Tcl_ListObjAppendElement(interp, fi, Tcl_NewDoubleObj(fs));
                Tcl_ListObjAppendElement(interp, fi, Tcl_NewIntObj(
                        (fo && FPDFFont_GetIsEmbedded(fo) == 1) ? 1 : 0));
                Tcl_ListObjAppendElement(interp, fi, Tcl_NewIntObj(
                        fo ? FPDFFont_GetFlags(fo) : 0));
            }
            /* Bei einem Pfad oder Bild bleibt die Liste LEER und nicht
             * weg -- sonst muesste der Aufrufer die Laenge je Art
             * unterscheiden. */
            Tcl_ListObjAppendElement(interp, e, fi);
        }
        if (wantMarks) {
            /* Die NAMEN der Marked-Content-Marken, in denen das Objekt
             * liegt -- "Artifact", "OC", "P" und so fort.
             *
             * Nur die Namen: die Parameter kommen bei "OC" als Typ 0
             * zurueck (gemessen 05.09.2026), also sagt pdfium zwar,
             * DASS ein Objekt in einer Ebene liegt, aber nicht in
             * welcher. Der Name allein reicht fuer die Frage, um die es
             * hier geht: ist dieser Text ein Artefakt -- also Kopfzeile
             * oder Seitenzahl, die ein Vorleseprogramm ueberspringen
             * soll -- oder einfach nicht ausgezeichnet? Beides hat
             * keine MCID, und ohne den Namen sind sie nicht zu
             * unterscheiden.
             */
            Tcl_Obj *marks = Tcl_NewListObj(0, NULL);
            int mc = FPDFPageObj_CountMarks(o);
            for (int k = 0; k < mc; k++) {
                FPDF_PAGEOBJECTMARK mk = FPDFPageObj_GetMark(o, k);
                if (!mk) continue;
                unsigned short buf[128];
                unsigned long len = 0;
                if (!FPDFPageObjMark_GetName(mk, buf, sizeof(buf), &len))
                    continue;
                /* GetName liefert UTF-16; die Namen sind ASCII. */
                char nm[128];
                unsigned long j = 0;
                for (unsigned long q = 0; q < len / 2 && j < sizeof(nm) - 1; q++) {
                    nm[j++] = (char)buf[q];
                }
                nm[j] = 0;
                Tcl_ListObjAppendElement(interp, marks,
                        Tcl_NewStringObj(nm, -1));
            }
            Tcl_ListObjAppendElement(interp, e, marks);
        }
        Tcl_ListObjAppendElement(interp, result, e);
    }
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::search doc-handle pagenum searchtext ?-case 0|1? ?-rects 0|1? */
/*                                                                     */
/* Ohne -rects: Liste von {startpos count} -- Zeichenpositionen, wie   */
/* bisher. Mit -rects 1: {startpos count {rechteck ...}}, jedes        */
/* Rechteck {links unten rechts oben} in PUNKT im Seitenkoordinaten-   */
/* system (Ursprung unten links), also genau die Zahlen, mit denen ein */
/* Stempel oder ein Streichstrich gesetzt wird.                        */
/*                                                                     */
/* WARUM MEHRERE Rechtecke je Treffer: ein Fundstueck kann ueber einen */
/* Zeilenumbruch gehen oder in mehreren Textstuecken stehen. PDFium    */
/* liefert dann ein Rechteck je zusammenhaengendem Stueck. Ein einziges*/
/* Rechteck zurueckzugeben hiesse, den Umbruchfall stillschweigend     */
/* falsch zu zeichnen.                                                 */
/*                                                                     */
/* Ohne diese Angabe war "dieses Wort durchstreichen" nicht machbar:   */
/* die Zeichenposition sagt, DASS etwas da ist, nicht WO.              */
/* ------------------------------------------------------------------ */
static int
PdfiumSearchCmd(ClientData cd, Tcl_Interp *interp,
                int objc, Tcl_Obj *const objv[])
{
    if (objc < 4) {
        Tcl_WrongNumArgs(interp, 1, objv,
                         "doc-handle pagenum searchtext ?-case 0|1?");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    int casesensitive = 0;
    int wantRects = 0;
    /* Die Optionen paarweise durchgehen. Vorher wurde nur EIN Paar an
     * fester Stelle gelesen (objv[4]/objv[5]); ein zweites blieb
     * unbemerkt liegen, statt gemeldet zu werden. */
    if (((objc - 4) % 2) != 0) {
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("option without value", -1));
        return TCL_ERROR;
    }
    for (int i = 4; i < objc; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        if (strcmp(opt, "-case") == 0) {
            /* The result was not checked: "search doc 0 wort -case ja"
             * left casesensitive at 0 and reported success. */
            if (Tcl_GetIntFromObj(interp, objv[i+1], &casesensitive) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-rects") == 0) {
            if (Tcl_GetIntFromObj(interp, objv[i+1], &wantRects) != TCL_OK)
                return TCL_ERROR;
        } else {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown option \"%s\": must be -case or -rects",
                              opt));
            return TCL_ERROR;
        }
    }

    /* Suchbegriff als UTF-16LE -- portabel (Tcl 8 + 9); NICHT
       Tcl_GetUnicodeFromObj (Tcl_UniChar ist in Tcl 9 32-bit). */
    const char *term_utf8 = Tcl_GetString(objv[3]);
    Tcl_DString termDs;
    Tcl_DStringInit(&termDs);
    Tcl_Encoding tenc = Tcl_GetEncoding(NULL, "utf-16le");
    if (!tenc) tenc = Tcl_GetEncoding(NULL, "unicode");
    if (tenc) {
        Tcl_UtfToExternalDString(tenc, term_utf8, -1, &termDs);
        Tcl_FreeEncoding(tenc);
    }
    { char _z[2] = {0,0}; Tcl_DStringAppend(&termDs, _z, 2); }
    const unsigned short *termUni =
        (const unsigned short *)Tcl_DStringValue(&termDs);

    FPDF_DOCUMENT  doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE      page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    FPDF_TEXTPAGE  tp   = FPDFText_LoadPage(page);
    FPDF_SCHHANDLE sh   = FPDFText_FindStart(tp,
                              (FPDF_WIDESTRING)termUni,
                              casesensitive ? FPDF_MATCHCASE : 0, 0);

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    while (FPDFText_FindNext(sh)) {
        int pos = FPDFText_GetSchResultIndex(sh);
        int cnt = FPDFText_GetSchCount(sh);
        Tcl_Obj *hit = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, hit, Tcl_NewIntObj(pos));
        Tcl_ListObjAppendElement(interp, hit, Tcl_NewIntObj(cnt));
        if (wantRects) {
            /* CountRects MUSS vor GetRect laufen: es rechnet die
             * Rechtecke aus und legt sie ab, GetRect holt sie nur.
             * Ohne den Aufruf liefert GetRect Nullen. */
            Tcl_Obj *rects = Tcl_NewListObj(0, NULL);
            int nr = FPDFText_CountRects(tp, pos, cnt);
            for (int r = 0; r < nr; r++) {
                double left, top, right, bottom;
                if (!FPDFText_GetRect(tp, r, &left, &top, &right, &bottom))
                    continue;
                Tcl_Obj *rc = Tcl_NewListObj(0, NULL);
                /* In der Reihenfolge {links unten rechts oben} -- wie ein
                 * PDF-Rechteck (/MediaBox, /Rect) geschrieben wird.
                 * PDFium gibt top vor bottom heraus; wer das eins zu eins
                 * durchreicht, vertauscht sie an der naechsten Stelle. */
                Tcl_ListObjAppendElement(interp, rc, Tcl_NewDoubleObj(left));
                Tcl_ListObjAppendElement(interp, rc, Tcl_NewDoubleObj(bottom));
                Tcl_ListObjAppendElement(interp, rc, Tcl_NewDoubleObj(right));
                Tcl_ListObjAppendElement(interp, rc, Tcl_NewDoubleObj(top));
                Tcl_ListObjAppendElement(interp, rects, rc);
            }
            Tcl_ListObjAppendElement(interp, hit, rects);
        }
        Tcl_ListObjAppendElement(interp, result, hit);
    }

    FPDFText_FindClose(sh);
    FPDFText_ClosePage(tp);
    FPDF_ClosePage(page);
    Tcl_DStringFree(&termDs);

    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::links doc-handle pagenum                                    */
/* Gibt Liste von URLs zurück die auf der Seite vorkommen.            */
/* ------------------------------------------------------------------ */
static int
PdfiumLinksCmd(ClientData cd, Tcl_Interp *interp,
               int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    FPDF_PAGELINK pl = FPDFLink_LoadWebLinks(tp);

    int n = FPDFLink_CountWebLinks(pl);
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);

    for (int i = 0; i < n; i++) {
        int len = FPDFLink_GetURL(pl, i, NULL, 0);
        if (len > 0) {
            unsigned short *buf =
                (unsigned short *)ckalloc(len * sizeof(unsigned short));
            FPDFLink_GetURL(pl, i, buf, len);
            int nchars = len - 1;
            if (nchars < 0) nchars = 0;
            Tcl_Obj *url = _AnnotUtf16ToObj(interp, (unsigned short *)buf, (unsigned long)(len * 2));
            Tcl_ListObjAppendElement(interp, result, url);
            ckfree((char *)buf);
        }
    }

    FPDFLink_CloseWebLinks(pl);
    FPDFText_ClosePage(tp);
    FPDF_ClosePage(page);

    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::bookmarks doc-handle                                        */
/* Gibt Liste von {titel pagenum level} zurück.                       */
/* ------------------------------------------------------------------ */

static void
CollectBookmarks(FPDF_DOCUMENT doc, FPDF_BOOKMARK bm,
                 int level, Tcl_Interp *interp, Tcl_Obj *result)
{
    while (bm) {
        /* Titel als UTF-16LE holen */
        unsigned long len = FPDFBookmark_GetTitle(bm, NULL, 0);
        unsigned short *buf = (unsigned short *)ckalloc(len + 2);
        FPDFBookmark_GetTitle(bm, buf, len);

        /* UTF-16LE -> UTF-8 via Tcl Encoding
         * Tcl 9: "utf-16le"
         * Tcl 8: "unicode" (entspricht UTF-16LE auf little-endian) */
        Tcl_DString ds;
        Tcl_DStringInit(&ds);
        Tcl_Encoding enc = Tcl_GetEncoding(NULL, "utf-16le");
        if (!enc) {
            enc = Tcl_GetEncoding(NULL, "unicode");
        }
        if (enc) {
            Tcl_ExternalToUtfDString(enc, (char *)buf, (int)(len - 2), &ds);
            Tcl_FreeEncoding(enc);
        } else {
            /* Letzter Fallback: direkt als UniChar */
            int nchars = (int)((len / 2) - 1);
            if (nchars < 0) nchars = 0;
            Tcl_UniCharToUtfDString((Tcl_UniChar *)buf, nchars, &ds);
        }
        Tcl_Obj *title = Tcl_NewStringObj(Tcl_DStringValue(&ds),
                                           Tcl_DStringLength(&ds));
        Tcl_DStringFree(&ds);
        ckfree((char *)buf);

        /* Ziel-Seite */
        FPDF_DEST dest   = FPDFBookmark_GetDest(doc, bm);
        int       pagenum = dest ? FPDFDest_GetDestPageIndex(doc, dest) : -1;

        /* Eintrag als Liste {titel pagenum level} */
        Tcl_Obj *entry = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, entry, title);
        Tcl_ListObjAppendElement(interp, entry, Tcl_NewIntObj(pagenum));
        Tcl_ListObjAppendElement(interp, entry, Tcl_NewIntObj(level));
        Tcl_ListObjAppendElement(interp, result, entry);

        /* Kinder rekursiv */
        FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(doc, bm);
        if (child)
            CollectBookmarks(doc, child, level + 1, interp, result);

        bm = FPDFBookmark_GetNextSibling(doc, bm);
    }
}

/* ------------------------------------------------------------------ */
/* pdfium::structure doc-handle pagenum                                */
/*                                                                     */
/* Gibt den Strukturbaum einer Seite so zurueck, wie PDFium ihn sieht  */
/* -- also so, wie ihn die Engine sieht, die in Chrome und Edge steckt. */
/* Das ist der Zweck: eine ZWEITE, unabhaengige Lesung neben der        */
/* eigenen. Ein Baum, den man selbst geschrieben hat, mit dem eigenen   */
/* Werkzeug zu lesen bestaetigt einen per Konstruktion.                 */
/*                                                                     */
/* Rueckgabe: verschachtelte Liste, je Element ein Dict:                */
/*                                                                     */
/*   type      /S des Elements ("P", "H1", "Table", ...)                */
/*   title     /T, wenn vorhanden                                       */
/*   alt       /Alt (Alternativtext)                                    */
/*   actual    /ActualText                                              */
/*   lang      /Lang                                                    */
/*   id        /ID                                                      */
/*   mcids     Liste ALLER marked-content-IDs des Elements              */
/*   attrs     Dict der Attribute (/Scope, /Headers, ...)               */
/*   children  Liste der Kindelemente, gleiche Form                     */
/*                                                                     */
/* mcids ist bewusst eine LISTE. FPDF_StructElement_GetMarkedContentID  */
/* liefert nur eine einzige, und ein Absatz, der ueber einen            */
/* Seitenumbruch laeuft, hat zwei -- die zweite Haelfte faellt damit    */
/* still unter den Tisch. Deshalb GetMarkedContentIdCount/AtIndex.      */
/* ------------------------------------------------------------------ */

/* UTF-16LE aus PDFium in ein Tcl-Objekt. len ist die Byte-Anzahl
 * einschliesslich der abschliessenden zwei Nullbytes, so wie PDFium
 * sie meldet. Liefert NULL, wenn nichts da ist. */
static Tcl_Obj *
PdfiumUtf16ToObj(const unsigned short *buf, unsigned long len)
{
    Tcl_DString ds;
    Tcl_Obj *obj;

    if (buf == NULL || len < 2) {
        return NULL;
    }
    Tcl_DStringInit(&ds);
    /* Tcl 9 kennt "utf-16le", Tcl 8.6 nennt es "unicode" -- auf
     * little-endian dasselbe. Gemessen: beide liefern fuer "AB" die
     * Bytes 41 00 42 00. */
    Tcl_Encoding enc = Tcl_GetEncoding(NULL, "utf-16le");
    if (!enc) {
        enc = Tcl_GetEncoding(NULL, "unicode");
    }
    if (enc) {
        Tcl_ExternalToUtfDString(enc, (const char *)buf,
                                 (int)(len - 2), &ds);
        Tcl_FreeEncoding(enc);
    } else {
        int nchars = (int)((len / 2) - 1);
        if (nchars < 0) nchars = 0;
        Tcl_UniCharToUtfDString((const Tcl_UniChar *)buf, nchars, &ds);
    }
    obj = Tcl_NewStringObj(Tcl_DStringValue(&ds), Tcl_DStringLength(&ds));
    Tcl_DStringFree(&ds);
    return obj;
}

/* Ein String-Feld eines Elements holen. getter ist eine der
 * FPDF_StructElement_Get*-Funktionen mit der ueblichen
 * (element, buffer, buflen) -> laenge Signatur. */
typedef unsigned long (*PdfiumStructGetter)(FPDF_STRUCTELEMENT,
                                            void *, unsigned long);

static Tcl_Obj *
PdfiumStructString(FPDF_STRUCTELEMENT el, PdfiumStructGetter getter)
{
    unsigned long len = getter(el, NULL, 0);
    if (len < 2) {
        return NULL;
    }
    unsigned short *buf = (unsigned short *)ckalloc(len + 2);
    getter(el, buf, len);
    Tcl_Obj *obj = PdfiumUtf16ToObj(buf, len);
    ckfree((char *)buf);
    return obj;
}

/* Attribute eines Elements als Dict. /Scope und /Headers stehen hier --
 * genau das, was pdf4tcllib bei Tabellen setzt und was ohne diese
 * Schleife unsichtbar bliebe. */
static Tcl_Obj *
PdfiumStructAttrs(Tcl_Interp *interp, FPDF_STRUCTELEMENT el)
{
    int count = FPDF_StructElement_GetAttributeCount(el);
    if (count <= 0) {
        return NULL;
    }
    Tcl_Obj *dict = Tcl_NewDictObj();
    for (int i = 0; i < count; i++) {
        FPDF_STRUCTELEMENT_ATTR attr =
                FPDF_StructElement_GetAttributeAtIndex(el, i);
        if (!attr) continue;
        int n = FPDF_StructElement_Attr_GetCount(attr);
        for (int j = 0; j < n; j++) {
            char name[128];
            unsigned long namelen = 0;
            if (!FPDF_StructElement_Attr_GetName(attr, j, name, sizeof(name),
                                                 &namelen)) {
                continue;
            }
            FPDF_STRUCTELEMENT_ATTR_VALUE val =
                    FPDF_StructElement_Attr_GetValue(attr, name);
            if (!val) continue;
            Tcl_Obj *vobj = NULL;
            switch (FPDF_StructElement_Attr_GetType(val)) {
            case FPDF_OBJECT_STRING:
            case FPDF_OBJECT_NAME: {
                unsigned long slen = 0;
                FPDF_StructElement_Attr_GetStringValue(val, NULL, 0, &slen);
                if (slen >= 2) {
                    unsigned short *sbuf = (unsigned short *)ckalloc(slen + 2);
                    FPDF_StructElement_Attr_GetStringValue(val, sbuf, slen,
                                                           &slen);
                    vobj = PdfiumUtf16ToObj(sbuf, slen);
                    ckfree((char *)sbuf);
                }
                break;
            }
            case FPDF_OBJECT_NUMBER: {
                float f = 0;
                if (FPDF_StructElement_Attr_GetNumberValue(val, &f)) {
                    vobj = Tcl_NewDoubleObj((double)f);
                }
                break;
            }
            case FPDF_OBJECT_BOOLEAN: {
                FPDF_BOOL b = 0;
                if (FPDF_StructElement_Attr_GetBooleanValue(val, &b)) {
                    vobj = Tcl_NewBooleanObj(b ? 1 : 0);
                }
                break;
            }
            default:
                break;
            }
            if (vobj) {
                /* namelen zaehlt das abschliessende Nullbyte mit. Ohne
                 * das -1 endete jeder Attributname auf \0, und ein
                 * "dict get $attrs O" fand nichts -- gemessen an einer
                 * Liste, deren Attribute als "ListNumbering\0" und
                 * "O\0" herauskamen. */
                int nlen = (int)namelen;
                if (nlen > 0 && name[nlen - 1] == '\0') nlen--;
                Tcl_DictObjPut(interp, dict,
                               Tcl_NewStringObj(name, nlen), vobj);
            }
        }
    }
    return dict;
}

static Tcl_Obj *
PdfiumStructElement(Tcl_Interp *interp, FPDF_STRUCTELEMENT el, int depth)
{
    /* Ein zyklischer oder absurd tiefer Baum soll den Interpreter nicht
     * mitnehmen. 64 Ebenen sind mehr, als ein Dokument je braucht. */
    if (el == NULL || depth > 64) {
        return NULL;
    }
    Tcl_Obj *dict = Tcl_NewDictObj();
    Tcl_Obj *o;

    o = PdfiumStructString(el, FPDF_StructElement_GetType);
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("type", -1),
                   o ? o : Tcl_NewStringObj("", -1));

    if ((o = PdfiumStructString(el, FPDF_StructElement_GetTitle)) != NULL)
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("title", -1), o);
    if ((o = PdfiumStructString(el, FPDF_StructElement_GetAltText)) != NULL)
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("alt", -1), o);
    if ((o = PdfiumStructString(el, FPDF_StructElement_GetActualText)) != NULL)
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("actual", -1), o);
    if ((o = PdfiumStructString(el, FPDF_StructElement_GetLang)) != NULL)
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("lang", -1), o);
    if ((o = PdfiumStructString(el, FPDF_StructElement_GetID)) != NULL)
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("id", -1), o);

    /* ALLE MCIDs, nicht nur die erste. */
    Tcl_Obj *mcids = Tcl_NewListObj(0, NULL);
    int mccount = FPDF_StructElement_GetMarkedContentIdCount(el);
    for (int i = 0; i < mccount; i++) {
        int mcid = FPDF_StructElement_GetMarkedContentIdAtIndex(el, i);
        if (mcid >= 0) {
            Tcl_ListObjAppendElement(interp, mcids, Tcl_NewIntObj(mcid));
        }
    }
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("mcids", -1), mcids);

    Tcl_Obj *attrs = PdfiumStructAttrs(interp, el);
    if (attrs) {
        Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("attrs", -1), attrs);
    }

    Tcl_Obj *kids = Tcl_NewListObj(0, NULL);
    int n = FPDF_StructElement_CountChildren(el);
    for (int i = 0; i < n; i++) {
        FPDF_STRUCTELEMENT kid = FPDF_StructElement_GetChildAtIndex(el, i);
        Tcl_Obj *kobj = PdfiumStructElement(interp, kid, depth + 1);
        if (kobj) {
            Tcl_ListObjAppendElement(interp, kids, kobj);
        }
    }
    Tcl_DictObjPut(interp, dict, Tcl_NewStringObj("children", -1), kids);
    return dict;
}

static int
PdfiumStructureCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    int pagenum;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE page = FPDF_LoadPage(doc, pagenum);
    if (!page) {
        Tcl_SetObjResult(interp,
                Tcl_NewStringObj("cannot load page", -1));
        return TCL_ERROR;
    }

    FPDF_STRUCTTREE tree = FPDF_StructTree_GetForPage(page);
    if (!tree) {
        /* Kein Strukturbaum ist kein Fehler -- die allermeisten PDFs
         * haben keinen. Leere Liste, und der Aufrufer entscheidet. */
        FPDF_ClosePage(page);
        Tcl_SetObjResult(interp, Tcl_NewListObj(0, NULL));
        return TCL_OK;
    }

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    int n = FPDF_StructTree_CountChildren(tree);
    for (int i = 0; i < n; i++) {
        FPDF_STRUCTELEMENT el = FPDF_StructTree_GetChildAtIndex(tree, i);
        Tcl_Obj *o = PdfiumStructElement(interp, el, 0);
        if (o) {
            Tcl_ListObjAppendElement(interp, result, o);
        }
    }

    FPDF_StructTree_Close(tree);
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int
PdfiumBookmarksCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_BOOKMARK root = FPDFBookmark_GetFirstChild(doc, NULL);

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    CollectBookmarks(doc, root, 0, interp, result);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* pdfium::formcheck doc-handle ?pagenum?                              */
/*                                                                     */
/* Was an diesem Formular verdaechtig ist -- nicht, was drinsteht.     */
/*                                                                     */
/* AUSLESEN UND PRUEFEN SIND ZWEI SACHEN. "formfields" ist auf acht    */
/* Elemente gewachsen, und jede weitere Auskunft macht es unleserlicher.*/
/* Ein zweiter Befehl, der nur Befunde meldet, haelt beides klein.      */
/*                                                                     */
/* Rueckgabe: Liste von {code seite feld text}. LEER heisst: nichts    */
/* aufgefallen -- und das soll man sehen koennen, statt es zu           */
/* vermuten.                                                           */
/*                                                                     */
/* NUR BELEGTE CODES. Ein Entwurf vom 08.09.2026 nannte vierzehn; drei */
/* davon haben an diesem Tag wirklich Zeit gekostet, die uebrigen elf  */
/* waren gut begruendete Vermutungen. Ein Code, den nie eine echte      */
/* Datei ausloest, wird nie rot -- und niemand erfaehrt, ob er richtig  */
/* misst. Genau das ist mir an diesem Tag viermal mit eigenen Tests     */
/* passiert.                                                           */
/*                                                                     */
/*   EMPTY_AP      Erscheinungsstrom der Laenge 0. PDFium zeichnet     */
/*                 dann NICHTS -- gemessen. Ein LEERER Strom sagt "so  */
/*                 sieht das Feld aus: gar nicht", und der Betrachter  */
/*                 glaubt es.                                          */
/*   VALUE_NO_AP   dasselbe, aber das Feld hat einen WERT. Das ist der */
/*                 Fall "formfields nennt den Wert, im Betrachter      */
/*                 sieht man nichts" -- er hat einen halben Tag        */
/*                 gekostet.                                           */
/*   CHOICE_VALUE_INVALID                                              */
/*                 der Wert eines Auswahlfeldes steht nicht unter      */
/*                 seinen Optionen.                                    */
/*                                                                     */
/* WAS HIER NICHT GEHT, und darum auch nicht behauptet wird: "kein /AP */
/* vorhanden" ist von "leerer /AP" ueber diese Bindung NICHT zu        */
/* unterscheiden. Fehlt der Strom ganz, baut PDFium sich selbst einen, */
/* und die Laenge ist dann groesser null -- gemessen an einer Datei    */
/* mit null /AP: apLength 75. Wer das trennen will, muss die Rohdatei  */
/* lesen, und das waere eine zweite Wahrheitsquelle.                   */
/* ------------------------------------------------------------------ */
static int
PdfiumFormCheckCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle ?pagenum?");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;
    int von = 0, bis = FPDF_GetPageCount(doc);
    if (objc == 3) {
        int p;
        if (Tcl_GetIntFromObj(interp, objv[2], &p) != TCL_OK) return TCL_ERROR;
        if (p < 0 || p >= bis) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "formcheck: page %d out of range (0..%d)", p, bis - 1));
            return TCL_ERROR;
        }
        von = p; bis = p + 1;
    }
    Tcl_Obj *aus = Tcl_NewListObj(0, NULL);
    for (int pn = von; pn < bis; pn++) {
        FPDF_PAGE page = FPDF_LoadPage(doc, pn);
        if (!page) continue;
        PdfiumDocForm *df = _DocFormGet(doc, page, pn);
        FPDF_FORMHANDLE form = df ? df->form : NULL;
        if (form) FORM_OnAfterLoadPage(page, form);
        int n = FPDFPage_GetAnnotCount(page);
        for (int i = 0; i < n && form; i++) {
            FPDF_ANNOTATION a = FPDFPage_GetAnnot(page, i);
            if (!a) continue;
            if (FPDFAnnot_GetSubtype(a) != FPDF_ANNOT_WIDGET) {
                FPDFPage_CloseAnnot(a); continue;
            }
            /* Name */
            Tcl_Obj *name = Tcl_NewStringObj("", 0);
            Tcl_IncrRefCount(name);
            unsigned long nl = FPDFAnnot_GetFormFieldName(form, a, NULL, 0);
            if (nl > 2) {
                unsigned short *nb = (unsigned short *)ckalloc(nl);
                FPDFAnnot_GetFormFieldName(form, a, nb, nl);
                Tcl_DecrRefCount(name);
                name = _AnnotUtf16ToObj(interp, nb, nl);
                Tcl_IncrRefCount(name);
                ckfree((char *)nb);
            }
            /* Wert */
            Tcl_Obj *wert = Tcl_NewStringObj("", 0);
            Tcl_IncrRefCount(wert);
            unsigned long vl = FPDFAnnot_GetFormFieldValue(form, a, NULL, 0);
            if (vl > 2) {
                unsigned short *vb = (unsigned short *)ckalloc(vl);
                FPDFAnnot_GetFormFieldValue(form, a, vb, vl);
                Tcl_DecrRefCount(wert);
                wert = _AnnotUtf16ToObj(interp, vb, vl);
                Tcl_IncrRefCount(wert);
                ckfree((char *)vb);
            }
            const char *wstr = Tcl_GetString(wert);
            int hatWert = (wstr[0] != '\0'
                           && strcmp(wstr, "Off") != 0);
            /* Erscheinungsstrom */
            unsigned long apl = FPDFAnnot_GetAP(a,
                    FPDF_ANNOT_APPEARANCEMODE_NORMAL, NULL, 0);
            long ap = (apl > 2) ? (long)((apl - 2) / 2) : 0;

#define FC_MELDE(code, txt) do {                                        \
        Tcl_Obj *e = Tcl_NewListObj(0, NULL);                           \
        Tcl_ListObjAppendElement(interp, e, Tcl_NewStringObj(code, -1)); \
        Tcl_ListObjAppendElement(interp, e, Tcl_NewIntObj(pn));         \
        Tcl_ListObjAppendElement(interp, e, Tcl_DuplicateObj(name));    \
        Tcl_ListObjAppendElement(interp, e, Tcl_NewStringObj(txt, -1)); \
        Tcl_ListObjAppendElement(interp, aus, e);                       \
    } while (0)

            if (ap == 0) {
                if (hatWert) {
                    FC_MELDE("VALUE_NO_AP",
                        "field carries a value but its appearance stream is"
                        " empty -- nothing will be drawn");
                } else {
                    FC_MELDE("EMPTY_AP",
                        "appearance stream is empty -- the field draws"
                        " nothing, not even its border");
                }
            }
            /* Auswahlfeld: Wert unter den Optionen? */
            int typ = FPDFAnnot_GetFormFieldType(form, a);
            if (hatWert && (typ == FPDF_FORMFIELD_COMBOBOX
                            || typ == FPDF_FORMFIELD_LISTBOX)) {
                int oc = FPDFAnnot_GetOptionCount(form, a);
                int gefunden = 0;
                for (int o = 0; o < oc; o++) {
                    unsigned long ll =
                        FPDFAnnot_GetOptionLabel(form, a, o, NULL, 0);
                    if (ll <= 2) continue;
                    unsigned short *lb = (unsigned short *)ckalloc(ll);
                    FPDFAnnot_GetOptionLabel(form, a, o, lb, ll);
                    Tcl_Obj *lo = _AnnotUtf16ToObj(interp, lb, ll);
                    Tcl_IncrRefCount(lo);
                    ckfree((char *)lb);
                    if (strcmp(Tcl_GetString(lo), wstr) == 0) gefunden = 1;
                    Tcl_DecrRefCount(lo);
                    if (gefunden) break;
                }
                if (oc > 0 && !gefunden) {
                    FC_MELDE("CHOICE_VALUE_INVALID",
                        "value is not among the field's options");
                }
            }
#undef FC_MELDE
            Tcl_DecrRefCount(name);
            Tcl_DecrRefCount(wert);
            FPDFPage_CloseAnnot(a);
        }
        if (form) {
            FORM_OnBeforeClosePage(page, form);
            _DocFormSeiteAb(df);
        }
        FPDF_ClosePage(page);
    }
    Tcl_SetObjResult(interp, aus);
    return TCL_OK;
}

static int
PdfiumFormFieldsCmd(ClientData cd, Tcl_Interp *interp,
                    int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    /* UEBER DIE FORMULAR-SCHNITTSTELLE, nicht ueber das Annotations-
     * woerterbuch.
     *
     * Bis hierher wurden /T, /V und /FT mit FPDFAnnot_GetStringValue
     * direkt am Widget gelesen. Das trifft den haeufigen Fall -- Feld
     * und Widget in einem Objekt --, aber nicht den, in dem sie am
     * VATER stehen und vom Widget nur geerbt werden (ISO 32000-1
     * 12.7.3.1). Gemessen an einem Formular mit einem Feld und zwei
     * Widgets:
     *
     *     {widget {} {}} {widget {} {}}
     *
     * Typ, Name und Wert leer -- und ohne jede Meldung. Bei einem
     * verschachtelten Namen kam "city" statt "person.city".
     *
     * FPDFAnnot_GetFormField* loest die Vererbung auf und setzt den
     * vollen Namen zusammen. Dafuer braucht es ein FPDF_FORMHANDLE.
     */
    PdfiumDocForm *df = _DocFormGet(doc, page, pagenum);
    FPDF_FORMHANDLE form = df ? df->form : NULL;
    if (form) FORM_OnAfterLoadPage(page, form);

    int n = FPDFPage_GetAnnotCount(page);
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);

    for (int i = 0; i < n; i++) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;
        if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_WIDGET) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        Tcl_Obj *name  = Tcl_NewStringObj("", 0);
        Tcl_Obj *value = Tcl_NewStringObj("", 0);
        int flags = 0;
        const char *typstr = "widget";
        int fftyp = -1;

        if (form) {
            unsigned long nlen =
                FPDFAnnot_GetFormFieldName(form, annot, NULL, 0);
            if (nlen > 2) {
                unsigned short *nb = (unsigned short *)ckalloc(nlen);
                FPDFAnnot_GetFormFieldName(form, annot, nb, nlen);
                name = _AnnotUtf16ToObj(interp, nb, nlen);
                ckfree((char *)nb);
            }
            unsigned long vlen =
                FPDFAnnot_GetFormFieldValue(form, annot, NULL, 0);
            if (vlen > 2) {
                unsigned short *vb = (unsigned short *)ckalloc(vlen);
                FPDFAnnot_GetFormFieldValue(form, annot, vb, vlen);
                value = _AnnotUtf16ToObj(interp, vb, vlen);
                ckfree((char *)vb);
            }
            flags = FPDFAnnot_GetFormFieldFlags(form, annot);
            fftyp = FPDFAnnot_GetFormFieldType(form, annot);
            /* Die Namen der Konstanten sind fest, die Werte nicht --
             * das steht so in fpdf_formfill.h. Darum ueber die Namen
             * und nicht ueber Zahlen. */
            switch (fftyp) {
                case FPDF_FORMFIELD_PUSHBUTTON:  typstr = "pushbutton"; break;
                case FPDF_FORMFIELD_CHECKBOX:    typstr = "checkbox";   break;
                case FPDF_FORMFIELD_RADIOBUTTON: typstr = "radiobutton";break;
                case FPDF_FORMFIELD_COMBOBOX:    typstr = "combobox";   break;
                case FPDF_FORMFIELD_LISTBOX:     typstr = "listbox";    break;
                case FPDF_FORMFIELD_TEXTFIELD:   typstr = "text";       break;
                case FPDF_FORMFIELD_SIGNATURE:   typstr = "signature";  break;
                default:                         typstr = "unknown";    break;
            }
        }

        Tcl_Obj *entry = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, entry,
                Tcl_NewStringObj(typstr, -1));
        Tcl_ListObjAppendElement(interp, entry, name);
        Tcl_ListObjAppendElement(interp, entry, value);
        Tcl_ListObjAppendElement(interp, entry, Tcl_NewIntObj(flags));
        /* Das Rechteck gehoert dazu: wer ein Feld anklicken oder
         * hervorheben will, braucht es, und es zweimal zu holen waere
         * ein zweiter Weg zu denselben Zahlen. Wie ueberall
         * {links unten rechts oben}. */
        FS_RECTF r;
        Tcl_Obj *rect = Tcl_NewListObj(0, NULL);
        if (FPDFAnnot_GetRect(annot, &r)) {
            Tcl_ListObjAppendElement(interp, rect, Tcl_NewDoubleObj(r.left));
            Tcl_ListObjAppendElement(interp, rect, Tcl_NewDoubleObj(r.bottom));
            Tcl_ListObjAppendElement(interp, rect, Tcl_NewDoubleObj(r.right));
            Tcl_ListObjAppendElement(interp, rect, Tcl_NewDoubleObj(r.top));
        }
        Tcl_ListObjAppendElement(interp, entry, rect);

        /* AUSWAHLFELDER: die erlaubten Werte mitgeben.
         *
         * Bis hierher meldete formfields "combobox" oder "listbox" und
         * verschwieg, WELCHE Werte gehen. Wer eines fuellen wollte,
         * musste raten -- eine Auskunft, die halb ist, ist schlechter
         * als eine, die fehlt: man haelt sie fuer vollstaendig.
         *
         * Je Eintrag {index label gewaehlt}. Der Index gehoert dazu,
         * weil er beim Setzen gebraucht wird und weil zwei Eintraege
         * dieselbe Beschriftung tragen duerfen.
         *
         * Bei allen anderen Feldarten bleibt die Liste LEER und nicht
         * etwa weg: eine Rueckgabe, die je nach Feldart verschieden
         * lang ist, zwingt jeden Aufrufer zu einer Fallunterscheidung.
         */
        Tcl_Obj *opts = Tcl_NewListObj(0, NULL);
        if (form && (fftyp == FPDF_FORMFIELD_COMBOBOX
                  || fftyp == FPDF_FORMFIELD_LISTBOX)) {
            int oc = FPDFAnnot_GetOptionCount(form, annot);
            for (int k = 0; k < oc; k++) {
                Tcl_Obj *one = Tcl_NewListObj(0, NULL);
                Tcl_ListObjAppendElement(interp, one, Tcl_NewIntObj(k));
                unsigned long ll =
                    FPDFAnnot_GetOptionLabel(form, annot, k, NULL, 0);
                if (ll > 2) {
                    unsigned short *lb = (unsigned short *)ckalloc(ll);
                    FPDFAnnot_GetOptionLabel(form, annot, k, lb, ll);
                    Tcl_ListObjAppendElement(interp, one,
                            _AnnotUtf16ToObj(interp, lb, ll));
                    ckfree((char *)lb);
                } else {
                    Tcl_ListObjAppendElement(interp, one,
                            Tcl_NewStringObj("", 0));
                }
                Tcl_ListObjAppendElement(interp, one, Tcl_NewIntObj(
                        FPDFAnnot_IsOptionSelected(form, annot, k) ? 1 : 0));
                Tcl_ListObjAppendElement(interp, opts, one);
            }
        }
        Tcl_ListObjAppendElement(interp, entry, opts);

        /* /TU -- der Name FUER MENSCHEN.
         *
         * Ein Betrachter zeigt ihn als Erklaerung an, wenn der Zeiger
         * ueber dem Feld steht. In einer Feldliste steht damit
         * "Empfaenger, Name und Anschrift" statt "f_kunde_2" -- bei
         * einem fremden Frachtbrief der Unterschied zwischen bedienbar
         * und Raetselraten.
         *
         * LEER, wenn die Datei keinen traegt. Dann bleibt der
         * technische Name die einzige Auskunft, und das soll man
         * sehen, statt ihn hier stillschweigend zu wiederholen: eine
         * Verdopplung sieht aus wie eine Erklaerung und ist keine.
         */
        Tcl_Obj *alt = Tcl_NewStringObj("", 0);
        Tcl_IncrRefCount(alt);
        if (form) {
            unsigned long al =
                FPDFAnnot_GetFormFieldAlternateName(form, annot, NULL, 0);
            if (al > 2) {
                unsigned short *ab = (unsigned short *)ckalloc(al);
                FPDFAnnot_GetFormFieldAlternateName(form, annot, ab, al);
                Tcl_DecrRefCount(alt);
                alt = _AnnotUtf16ToObj(interp, ab, al);
                ckfree((char *)ab);
            }
        }
        Tcl_ListObjAppendElement(interp, entry, alt);

        /* WIE LANG DER ERSCHEINUNGSSTROM IST.
         *
         * Der haeufigste Grund fuer "das Feld ist da und man sieht
         * nichts": /V steht, /AP ist LEER. Die beiden sind in PDF
         * getrennte Dinge (ISO 32000-1 12.5.5) -- formfields nennt den
         * Wert, und PDFium zeichnet den Strom.
         *
         * Ohne diese Zahl sucht man den Fehler in der Bindung, im
         * Renderweg oder in den Rueckrufen. Mit ihr sieht man sofort:
         * die Datei sagt nicht, wie das Feld aussieht.
         *
         * 0 heisst LEER, nicht "nicht vorhanden" -- ein fehlendes /AP
         * und ein leerer Strom sind fuer den Betrachter dasselbe, und
         * eine Unterscheidung, die niemand nutzen kann, waere Ballast.
         */
        unsigned long apl = FPDFAnnot_GetAP(annot,
                FPDF_ANNOT_APPEARANCEMODE_NORMAL, NULL, 0);
        /* GetAP zaehlt in Bytes einschliesslich der abschliessenden
         * Null; zwei Bytes sind die leere Zeichenkette. */
        Tcl_ListObjAppendElement(interp, entry,
                Tcl_NewWideIntObj(apl > 2 ? (Tcl_WideInt)(apl - 2) / 2 : 0));

        Tcl_ListObjAppendElement(interp, result, entry);

        FPDFPage_CloseAnnot(annot);
    }

    if (form) {
        /* Nur die Seite abmelden -- die Umgebung gehoert dem Dokument. */
        FORM_OnBeforeClosePage(page, form);
        if (df) {
            /* Die Seite der Sitzung wieder eintragen, wenn es eine
             * gibt -- sonst faende FFI_GetPage nichts mehr. */
            _DocFormSeiteAb(df);
        }
    }
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* _AnnotUtf16ToObj  --  UTF-16LE Buffer -> Tcl_Obj (Tcl 8 + 9)      */
/* ------------------------------------------------------------------ */
static Tcl_Obj *
_AnnotUtf16ToObj(Tcl_Interp *interp, unsigned short *buf, unsigned long bytelen)
{
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    Tcl_Encoding enc = Tcl_GetEncoding(NULL, "utf-16le");
    if (!enc) enc = Tcl_GetEncoding(NULL, "unicode");
    if (enc) {
        Tcl_ExternalToUtfDString(enc, (char *)buf,
                                 (int)(bytelen > 2 ? bytelen - 2 : 0), &ds);
        Tcl_FreeEncoding(enc);
    }
    Tcl_Obj *obj = Tcl_NewStringObj(Tcl_DStringValue(&ds),
                                     Tcl_DStringLength(&ds));
    Tcl_DStringFree(&ds);
    return obj;
}

/* ------------------------------------------------------------------ */
/* pdfium::annot_list doc-handle pagenum                               */
/* Gibt Liste aller Annotationen einer Seite zurueck:                  */
/*   {type subtype rect content author date}                           */
/*                                                                     */
/* type  = text|highlight|underline|strikeout|squiggly|link|           */
/*          freetext|line|square|circle|stamp|widget|popup|...         */
/* rect  = {x1 y1 x2 y2} in Seitenkoordinaten (Punkte)               */
/* ------------------------------------------------------------------ */
static int
PdfiumAnnotListCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pagenum");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;

    int pagenum;
    if (Tcl_GetIntFromObj(interp, objv[2], &pagenum) != TCL_OK)
        return TCL_ERROR;

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    int n = FPDFPage_GetAnnotCount(page);
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);

    for (int i = 0; i < n; i++) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;

        /* Annotationstyp als String */
        FPDF_ANNOTATION_SUBTYPE sub = FPDFAnnot_GetSubtype(annot);
        const char *typstr;
        switch (sub) {
            case FPDF_ANNOT_TEXT:       typstr = "text";       break;
            case FPDF_ANNOT_LINK:       typstr = "link";       break;
            case FPDF_ANNOT_FREETEXT:   typstr = "freetext";   break;
            case FPDF_ANNOT_LINE:       typstr = "line";       break;
            case FPDF_ANNOT_SQUARE:     typstr = "square";     break;
            case FPDF_ANNOT_CIRCLE:     typstr = "circle";     break;
            case FPDF_ANNOT_POLYGON:    typstr = "polygon";    break;
            case FPDF_ANNOT_POLYLINE:   typstr = "polyline";   break;
            case FPDF_ANNOT_HIGHLIGHT:  typstr = "highlight";  break;
            case FPDF_ANNOT_UNDERLINE:  typstr = "underline";  break;
            case FPDF_ANNOT_SQUIGGLY:   typstr = "squiggly";   break;
            case FPDF_ANNOT_STRIKEOUT:  typstr = "strikeout";  break;
            case FPDF_ANNOT_STAMP:      typstr = "stamp";      break;
            case FPDF_ANNOT_CARET:      typstr = "caret";      break;
            case FPDF_ANNOT_INK:        typstr = "ink";        break;
            case FPDF_ANNOT_POPUP:      typstr = "popup";      break;
            case FPDF_ANNOT_FILEATTACHMENT: typstr = "fileattachment"; break;
            case FPDF_ANNOT_SOUND:      typstr = "sound";      break;
            case FPDF_ANNOT_MOVIE:      typstr = "movie";      break;
            case FPDF_ANNOT_WIDGET:     typstr = "widget";     break;
            case FPDF_ANNOT_SCREEN:     typstr = "screen";     break;
            case FPDF_ANNOT_PRINTERMARK: typstr = "printermark"; break;
            case FPDF_ANNOT_TRAPNET:    typstr = "trapnet";    break;
            case FPDF_ANNOT_WATERMARK:  typstr = "watermark";  break;
            case FPDF_ANNOT_THREED:     typstr = "threed";     break;
            case FPDF_ANNOT_RICHMEDIA:  typstr = "richmedia";  break;
            case FPDF_ANNOT_XFAWIDGET: typstr = "xfawidget";  break;
            default:                    typstr = "unknown";    break;
        }

        /* Bounding-Rect in Seitenkoordinaten */
        FS_RECTF rect = {0, 0, 0, 0};
        FPDFAnnot_GetRect(annot, &rect);
        Tcl_Obj *rectobj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, rectobj,
                                 Tcl_NewDoubleObj((double)rect.left));
        Tcl_ListObjAppendElement(interp, rectobj,
                                 Tcl_NewDoubleObj((double)rect.bottom));
        Tcl_ListObjAppendElement(interp, rectobj,
                                 Tcl_NewDoubleObj((double)rect.right));
        Tcl_ListObjAppendElement(interp, rectobj,
                                 Tcl_NewDoubleObj((double)rect.top));

        /* Inhalt (Contents) */
        Tcl_Obj *content;
        unsigned long clen = FPDFAnnot_GetStringValue(annot, "Contents", NULL, 0);
        if (clen > 2) {
            unsigned short *cbuf = (unsigned short *)ckalloc(clen + 2);
            FPDFAnnot_GetStringValue(annot, "Contents", cbuf, clen);
            content = _AnnotUtf16ToObj(interp, cbuf, clen);
            ckfree((char *)cbuf);
        } else {
            content = Tcl_NewStringObj("", 0);
        }

        /* Autor (T) */
        Tcl_Obj *author;
        unsigned long alen = FPDFAnnot_GetStringValue(annot, "T", NULL, 0);
        if (alen > 2) {
            unsigned short *abuf = (unsigned short *)ckalloc(alen + 2);
            FPDFAnnot_GetStringValue(annot, "T", abuf, alen);
            author = _AnnotUtf16ToObj(interp, abuf, alen);
            ckfree((char *)abuf);
        } else {
            author = Tcl_NewStringObj("", 0);
        }

        /* Datum (M = ModDate oder CreationDate) */
        Tcl_Obj *date;
        unsigned long dlen = FPDFAnnot_GetStringValue(annot, "M", NULL, 0);
        if (dlen <= 2)
            dlen = FPDFAnnot_GetStringValue(annot, "CreationDate", NULL, 0);
        if (dlen > 2) {
            unsigned short *dbuf = (unsigned short *)ckalloc(dlen + 2);
            FPDFAnnot_GetStringValue(annot, "M", dbuf, dlen);
            if (dlen <= 2)
                FPDFAnnot_GetStringValue(annot, "CreationDate", dbuf, dlen);
            date = _AnnotUtf16ToObj(interp, dbuf, dlen);
            ckfree((char *)dbuf);
        } else {
            date = Tcl_NewStringObj("", 0);
        }

        /* Eintrag: {type rect content author date} */
        Tcl_Obj *entry = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, entry,
                                 Tcl_NewStringObj(typstr, -1));
        Tcl_ListObjAppendElement(interp, entry, rectobj);
        Tcl_ListObjAppendElement(interp, entry, content);
        Tcl_ListObjAppendElement(interp, entry, author);
        Tcl_ListObjAppendElement(interp, entry, date);
        Tcl_ListObjAppendElement(interp, result, entry);

        FPDFPage_CloseAnnot(annot);
    }

    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* ------------------------------------------------------------------ */
/* Pdfiumtcl_Init  --  wird von "load" aufgerufen                      */
/* ------------------------------------------------------------------ */
/* ================================================================== */
/* Write / edit commands (pdfiumtcl 0.4: pdfium becomes write-capable) */
/* ================================================================== */

/* ---- file writer for FPDF_SaveAsCopy ----------------------------- */
typedef struct {
    FPDF_FILEWRITE base;
    FILE          *fp;
} TclFileWrite;

static int
WriteBlockToFile(FPDF_FILEWRITE *self, const void *data, unsigned long size)
{
    TclFileWrite *w = (TclFileWrite *)self;
    return (fwrite(data, 1, size, w->fp) == size) ? 1 : 0;
}

/* ---- in-memory file reader for LoadJpegFileInline ---------------- */
typedef struct {
    const unsigned char *data;
    unsigned long        len;
} MemBuf;

static int
MemGetBlock(void *param, unsigned long pos, unsigned char *buf, unsigned long size)
{
    MemBuf *m = (MemBuf *)param;
    if ((unsigned long)pos + size > m->len) return 0;
    memcpy(buf, m->data + pos, size);
    return 1;
}

/* pdfium::newdoc  -> doc-handle (empty document) */
static int
PdfiumNewDocCmd(ClientData cd, Tcl_Interp *interp,
                int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) { Tcl_WrongNumArgs(interp, 1, objv, ""); return TCL_ERROR; }
    FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
    if (!doc) PDFIUM_ERROR(interp, "cannot create new document");
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)(intptr_t)doc));
    return TCL_OK;
}

/* pdfium::newpage doc-handle index width height  -> page-handle (points) */
static int
PdfiumNewPageCmd(ClientData cd, Tcl_Interp *interp,
                 int objc, Tcl_Obj *const objv[])
{
    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle index width height");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr; int index; double w, h;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[2], &index)   != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[3], &w)     != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[4], &h)     != TCL_OK) return TCL_ERROR;
    FPDF_PAGE page = FPDFPage_New((FPDF_DOCUMENT)(intptr_t)ptr, index, w, h);
    if (!page) PDFIUM_ERROR(interp, "cannot create page");
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)(intptr_t)page));
    return TCL_OK;
}

/* pdfium::closepage page-handle */
static int
PdfiumClosePageCmd(ClientData cd, Tcl_Interp *interp,
                   int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) { Tcl_WrongNumArgs(interp, 1, objv, "page-handle"); return TCL_ERROR; }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    FPDF_ClosePage((FPDF_PAGE)(intptr_t)ptr);
    return TCL_OK;
}

/* pdfium::generatecontent page-handle  -> 0/1 */
static int
PdfiumGenerateContentCmd(ClientData cd, Tcl_Interp *interp,
                         int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) { Tcl_WrongNumArgs(interp, 1, objv, "page-handle"); return TCL_ERROR; }
    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    FPDF_BOOL ok = FPDFPage_GenerateContent((FPDF_PAGE)(intptr_t)ptr);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(ok));
    return TCL_OK;
}

/* pdfium::importpages dest-handle src-handle ?pagerange? ?index?  -> 0/1
 * pagerange: "1,3,5-7" (1-based) or "" / omitted for all pages. */
static int
PdfiumImportPagesCmd(ClientData cd, Tcl_Interp *interp,
                     int objc, Tcl_Obj *const objv[])
{
    if (objc < 3 || objc > 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "dest-handle src-handle ?pagerange? ?index?");
        return TCL_ERROR;
    }
    Tcl_WideInt dptr, sptr; int index = 0;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &dptr) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetWideIntFromObj(interp, objv[2], &sptr) != TCL_OK) return TCL_ERROR;
    const char *range = (objc >= 4) ? Tcl_GetString(objv[3]) : NULL;
    if (range && range[0] == '\0') range = NULL;
    if (objc == 5 && Tcl_GetIntFromObj(interp, objv[4], &index) != TCL_OK) return TCL_ERROR;
    FPDF_BOOL ok = FPDF_ImportPages((FPDF_DOCUMENT)(intptr_t)dptr,
                                    (FPDF_DOCUMENT)(intptr_t)sptr, range, index);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(ok));
    return TCL_OK;
}

/* shared box setter: which==1 crop, 0 media */
static int
PdfiumSetBox(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], int isCrop)
{
    if (objc != 7) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle pageindex left bottom right top");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr; int idx; double l, b, r, t;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[2], &idx)     != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[3], &l)     != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[4], &b)     != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[5], &r)     != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[6], &t)     != TCL_OK) return TCL_ERROR;
    FPDF_PAGE page = FPDF_LoadPage((FPDF_DOCUMENT)(intptr_t)ptr, idx);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");
    if (isCrop) FPDFPage_SetCropBox(page, (float)l, (float)b, (float)r, (float)t);
    else        FPDFPage_SetMediaBox(page, (float)l, (float)b, (float)r, (float)t);
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
    return TCL_OK;
}

/* pdfium::setcropbox doc-handle pageindex left bottom right top  -> 1 */
static int
PdfiumSetCropBoxCmd(ClientData cd, Tcl_Interp *interp,
                    int objc, Tcl_Obj *const objv[])
{ return PdfiumSetBox(interp, objc, objv, 1); }

/* pdfium::setmediabox doc-handle pageindex left bottom right top  -> 1 */
static int
PdfiumSetMediaBoxCmd(ClientData cd, Tcl_Interp *interp,
                     int objc, Tcl_Obj *const objv[])
{ return PdfiumSetBox(interp, objc, objv, 0); }

/* pdfium::addimagejpeg page-handle doc-handle jpegfile x y w h  -> 0/1
 * Embeds a JPEG as an image object on the page, scaled to w x h points,
 * positioned at (x,y) in points (origin bottom-left). */
static int
PdfiumAddImageJpegCmd(ClientData cd, Tcl_Interp *interp,
                      int objc, Tcl_Obj *const objv[])
{
    if (objc != 8) {
        Tcl_WrongNumArgs(interp, 1, objv, "page-handle doc-handle jpegfile x y w h");
        return TCL_ERROR;
    }
    Tcl_WideInt pptr, dptr; double x, y, w, h;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &pptr) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetWideIntFromObj(interp, objv[2], &dptr) != TCL_OK) return TCL_ERROR;
    const char *fn = Tcl_GetString(objv[3]);
    if (Tcl_GetDoubleFromObj(interp, objv[4], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[5], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[6], &w) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[7], &h) != TCL_OK) return TCL_ERROR;

    FILE *fp = fopen(fn, "rb");
    if (!fp) PDFIUM_ERROR(interp, "cannot open JPEG file");
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); PDFIUM_ERROR(interp, "empty JPEG file"); }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(fp); PDFIUM_ERROR(interp, "out of memory"); }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); PDFIUM_ERROR(interp, "cannot read JPEG file");
    }
    fclose(fp);

    MemBuf mb; mb.data = buf; mb.len = (unsigned long)sz;
    FPDF_FILEACCESS fa;
    fa.m_FileLen  = (unsigned long)sz;
    fa.m_GetBlock = MemGetBlock;
    fa.m_Param    = &mb;

    FPDF_DOCUMENT  doc  = (FPDF_DOCUMENT)(intptr_t)dptr;
    FPDF_PAGE      page = (FPDF_PAGE)(intptr_t)pptr;
    FPDF_PAGEOBJECT obj = FPDFPageObj_NewImageObj(doc);
    if (!obj) { free(buf); PDFIUM_ERROR(interp, "cannot create image object"); }

    FPDF_PAGE pages[1]; pages[0] = page;
    FPDF_BOOL ok = FPDFImageObj_LoadJpegFileInline(pages, 1, obj, &fa);
    if (ok) {
        FPDFImageObj_SetMatrix(obj, w, 0, 0, h, x, y);
        FPDFPage_InsertObject(page, obj);
    } else {
        FPDFPageObj_Destroy(obj);
    }
    free(buf);  /* Inline variant copies the data into the document */
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(ok));
    return TCL_OK;
}

/* pdfium::save doc-handle filename ?flags?  -> 0/1
 * flags default = FPDF_NO_INCREMENTAL (clean full rewrite). */
static int
PdfiumSaveCmd(ClientData cd, Tcl_Interp *interp,
              int objc, Tcl_Obj *const objv[])
{
    if (objc < 3 || objc > 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle filename ?flags?");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr; int flags = FPDF_NO_INCREMENTAL;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    const char *fn = Tcl_GetString(objv[2]);
    if (objc == 4 && Tcl_GetIntFromObj(interp, objv[3], &flags) != TCL_OK) return TCL_ERROR;

    FILE *fp = fopen(fn, "wb");
    if (!fp) PDFIUM_ERROR(interp, "cannot open output file for writing");
    TclFileWrite w;
    w.base.version    = 1;
    w.base.WriteBlock = WriteBlockToFile;
    w.fp              = fp;
    FPDF_BOOL ok = FPDF_SaveAsCopy((FPDF_DOCUMENT)(intptr_t)ptr,
                                   &w.base, (FPDF_DWORD)flags);
    fclose(fp);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(ok));
    return TCL_OK;
}

/* pdfium::addimagebitmap page-handle doc-handle photoName x y w h  -> 0/1
 * Embeds a Tk photo image (lossless, no JPEG artifacts) as an image object,
 * scaled to w x h points, positioned at (x,y) in points (origin bottom-left).
 * Pixels are read via the Tk stub API (Tk_FindPhoto / Tk_PhotoGetImage), so
 * this works unchanged on Windows/macOS/Linux. */
static int
PdfiumAddImageBitmapCmd(ClientData cd, Tcl_Interp *interp,
                        int objc, Tcl_Obj *const objv[])
{
    /* Reads a Tk photo -- Tk stubs must be live, or Tk_FindPhoto dereferences
     * a NULL tkStubsPtr and the process dies. Same lazy init as render. */
    if (EnsureTk(interp) != TCL_OK) return TCL_ERROR;
    if (objc != 8) {
        Tcl_WrongNumArgs(interp, 1, objv, "page-handle doc-handle photo x y w h");
        return TCL_ERROR;
    }
    Tcl_WideInt pptr, dptr; double x, y, w, h;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &pptr) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetWideIntFromObj(interp, objv[2], &dptr) != TCL_OK) return TCL_ERROR;
    const char *photoName = Tcl_GetString(objv[3]);
    if (Tcl_GetDoubleFromObj(interp, objv[4], &x) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[5], &y) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[6], &w) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetDoubleFromObj(interp, objv[7], &h) != TCL_OK) return TCL_ERROR;

    Tk_PhotoHandle photo = Tk_FindPhoto(interp, photoName);
    if (!photo) PDFIUM_ERROR(interp, "no such photo image");
    Tk_PhotoImageBlock blk;
    if (!Tk_PhotoGetImage(photo, &blk))
        PDFIUM_ERROR(interp, "cannot read photo image");
    int iw = blk.width, ih = blk.height;
    if (iw <= 0 || ih <= 0) PDFIUM_ERROR(interp, "empty photo image");

    /* pdfium expects a BGRA, top-down bitmap; Tk photo rows are top-down too. */
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(iw, ih, FPDFBitmap_BGRA, NULL, 0);
    if (!bmp) PDFIUM_ERROR(interp, "cannot create bitmap");
    unsigned char *dstbuf = (unsigned char *)FPDFBitmap_GetBuffer(bmp);
    int stride = FPDFBitmap_GetStride(bmp);
    int hasAlpha = (blk.pixelSize >= 4);
    for (int row = 0; row < ih; row++) {
        unsigned char *src = blk.pixelPtr + (size_t)row * blk.pitch;
        unsigned char *dst = dstbuf + (size_t)row * stride;
        for (int col = 0; col < iw; col++) {
            unsigned char *sp = src + (size_t)col * blk.pixelSize;
            unsigned char *dp = dst + (size_t)col * 4;
            dp[0] = sp[blk.offset[2]];                 /* B */
            dp[1] = sp[blk.offset[1]];                 /* G */
            dp[2] = sp[blk.offset[0]];                 /* R */
            dp[3] = hasAlpha ? sp[blk.offset[3]] : 255;/* A */
        }
    }

    FPDF_DOCUMENT   doc  = (FPDF_DOCUMENT)(intptr_t)dptr;
    FPDF_PAGE       page = (FPDF_PAGE)(intptr_t)pptr;
    FPDF_PAGEOBJECT obj  = FPDFPageObj_NewImageObj(doc);
    if (!obj) { FPDFBitmap_Destroy(bmp);
                PDFIUM_ERROR(interp, "cannot create image object"); }

    FPDF_PAGE pages[1]; pages[0] = page;
    FPDF_BOOL ok = FPDFImageObj_SetBitmap(pages, 1, obj, bmp);
    if (ok) {
        FPDFImageObj_SetMatrix(obj, w, 0, 0, h, x, y);
        FPDFPage_InsertObject(page, obj);
    } else {
        FPDFPageObj_Destroy(obj);
    }
    /* SetBitmap retains its own copy; safe to destroy now. */
    FPDFBitmap_Destroy(bmp);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(ok));
    return TCL_OK;
}

/* pdfium::deletepage doc-handle index  -> 1  (removes a page) */
static int
PdfiumDeletePageCmd(ClientData cd, Tcl_Interp *interp,
                    int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle index");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr; int idx;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[2], &idx)     != TCL_OK) return TCL_ERROR;
    FPDFPage_Delete((FPDF_DOCUMENT)(intptr_t)ptr, idx);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
    return TCL_OK;
}

/* pdfium::setrotation doc-handle index degrees  -> 0/1
 * degrees must be 0, 90, 180 or 270. */
static int
PdfiumSetRotationCmd(ClientData cd, Tcl_Interp *interp,
                     int objc, Tcl_Obj *const objv[])
{
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle index degrees");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr; int idx, deg;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[2], &idx)     != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[3], &deg)     != TCL_OK) return TCL_ERROR;
    if (deg % 90 != 0) PDFIUM_ERROR(interp, "degrees must be 0, 90, 180 or 270");
    int rot = ((deg / 90) % 4 + 4) % 4;   /* normalise, accept negatives */
    FPDF_PAGE page = FPDF_LoadPage((FPDF_DOCUMENT)(intptr_t)ptr, idx);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");
    FPDFPage_SetRotation(page, rot);
    FPDF_ClosePage(page);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
    return TCL_OK;
}

/* pdfium::savewithversion doc-handle filename version ?flags?  -> 0/1
 * version: PDF version as integer, e.g. 14 (1.4) .. 17 (1.7). */
static int
PdfiumSaveWithVersionCmd(ClientData cd, Tcl_Interp *interp,
                         int objc, Tcl_Obj *const objv[])
{
    if (objc < 4 || objc > 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle filename version ?flags?");
        return TCL_ERROR;
    }
    Tcl_WideInt ptr; int version, flags = FPDF_NO_INCREMENTAL;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK) return TCL_ERROR;
    const char *fn = Tcl_GetString(objv[2]);
    if (Tcl_GetIntFromObj(interp, objv[3], &version) != TCL_OK) return TCL_ERROR;
    if (objc == 5 && Tcl_GetIntFromObj(interp, objv[4], &flags) != TCL_OK) return TCL_ERROR;

    FILE *fp = fopen(fn, "wb");
    if (!fp) PDFIUM_ERROR(interp, "cannot open output file for writing");
    TclFileWrite w;
    w.base.version    = 1;
    w.base.WriteBlock = WriteBlockToFile;
    w.fp              = fp;
    FPDF_BOOL ok = FPDF_SaveWithVersion((FPDF_DOCUMENT)(intptr_t)ptr,
                                        &w.base, (FPDF_DWORD)flags, version);
    fclose(fp);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(ok));
    return TCL_OK;
}

/* ==================================================================== */
/* Windows printing (GDI / DEVMODE)                                     */
/*                                                                      */
/* FPDF_RenderPage(HDC, ...) exists only in Windows builds of libpdfium; */
/* Linux builds do not export the symbol. On Linux/macOS CUPS handles    */
/* PDFs natively, so nothing is needed there.                           */
/*                                                                      */
/* Commands: canprint printers defaultprinter papers printercaps print  */
/* Link with: gdi32 winspool                                            */
/* ==================================================================== */

#ifdef _WIN32

#define PDFIUM_MIN(a,b) ((a) < (b) ? (a) : (b))

/* -------------------------------------------------------------------- */
/* UTF-8 (Tcl) <-> UTF-16 (Win32). Free results with ckfree().           */
/* Deliberately not Tcl_WinUtfToTChar: removed in Tcl 9.                 */
/* -------------------------------------------------------------------- */
static WCHAR *
PdfiumUtf8ToWide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    WCHAR *w = (WCHAR *)ckalloc((size_t)n * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static Tcl_Obj *
PdfiumWideToObj(const WCHAR *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return Tcl_NewStringObj("", 0);
    char *s = (char *)ckalloc((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    Tcl_Obj *o = Tcl_NewStringObj(s, n - 1);
    ckfree(s);
    return o;
}

/* Resolve the printer name: explicit argument or system default.        */
/* Returns a WCHAR* to free with ckfree, or NULL (interp result set).    */
static WCHAR *
PdfiumResolvePrinter(Tcl_Interp *interp, const char *given)
{
    if (given && *given) return PdfiumUtf8ToWide(given);

    DWORD len = 0;
    GetDefaultPrinterW(NULL, &len);
    if (len == 0) {
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("no printer given and no system default", -1));
        return NULL;
    }
    WCHAR *w = (WCHAR *)ckalloc(len * sizeof(WCHAR));
    if (!GetDefaultPrinterW(w, &len)) {
        ckfree((char *)w);
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("cannot query default printer", -1));
        return NULL;
    }
    return w;
}

/* -------------------------------------------------------------------- */
/* pdfium::printers  --  list of installed printers                      */
/* -------------------------------------------------------------------- */
static int
PdfiumPrintersCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }

    DWORD needed = 0, count = 0;
    const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;

    EnumPrintersW(flags, NULL, 4, NULL, 0, &needed, &count);
    if (needed == 0) {
        Tcl_SetObjResult(interp, Tcl_NewListObj(0, NULL));
        return TCL_OK;
    }

    BYTE *buf = (BYTE *)ckalloc(needed);
    if (!EnumPrintersW(flags, NULL, 4, buf, needed, &needed, &count)) {
        ckfree((char *)buf);
        PDFIUM_ERROR(interp, "EnumPrinters failed");
    }

    PRINTER_INFO_4W *pi = (PRINTER_INFO_4W *)buf;
    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    for (DWORD i = 0; i < count; i++)
        Tcl_ListObjAppendElement(interp, list,
                                 PdfiumWideToObj(pi[i].pPrinterName));

    ckfree((char *)buf);
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/* -------------------------------------------------------------------- */
/* pdfium::defaultprinter                                                */
/* -------------------------------------------------------------------- */
static int
PdfiumDefaultPrinterCmd(ClientData cd, Tcl_Interp *interp,
                        int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    WCHAR *w = PdfiumResolvePrinter(interp, NULL);
    if (!w) return TCL_ERROR;
    Tcl_SetObjResult(interp, PdfiumWideToObj(w));
    ckfree((char *)w);
    return TCL_OK;
}

/* -------------------------------------------------------------------- */
/* pdfium::papers ?printer?  --  the forms the driver offers             */
/*                                                                       */
/* Decisive for label printers: Brother QL drivers expose their tapes as */
/* named forms ("62mm x 100mm"). A DMPAPER_USER with free dimensions is  */
/* frequently ignored by them -- one has to use the reported code.       */
/* -------------------------------------------------------------------- */
static int
PdfiumPapersCmd(ClientData cd, Tcl_Interp *interp,
                int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc > 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?printer?");
        return TCL_ERROR;
    }

    WCHAR *wp = PdfiumResolvePrinter(interp,
                    (objc == 2) ? Tcl_GetString(objv[1]) : NULL);
    if (!wp) return TCL_ERROR;

    int n = (int)DeviceCapabilitiesW(wp, NULL, DC_PAPERNAMES, NULL, NULL);
    if (n <= 0) {
        ckfree((char *)wp);
        Tcl_SetObjResult(interp, Tcl_NewListObj(0, NULL));
        return TCL_OK;
    }

    /* DC_PAPERNAMES: n blocks of 64 WCHAR, not necessarily terminated. */
    WCHAR *names = (WCHAR *)ckalloc((size_t)n * 64 * sizeof(WCHAR));
    WORD  *codes = (WORD  *)ckalloc((size_t)n * sizeof(WORD));
    POINT *sizes = (POINT *)ckalloc((size_t)n * sizeof(POINT));

    DeviceCapabilitiesW(wp, NULL, DC_PAPERNAMES, (LPWSTR)names, NULL);
    DeviceCapabilitiesW(wp, NULL, DC_PAPERS,     (LPWSTR)codes, NULL);
    DeviceCapabilitiesW(wp, NULL, DC_PAPERSIZE,  (LPWSTR)sizes, NULL);

    Tcl_Obj *list = Tcl_NewListObj(0, NULL);
    for (int i = 0; i < n; i++) {
        WCHAR buf[65];
        memcpy(buf, names + (size_t)i * 64, 64 * sizeof(WCHAR));
        buf[64] = L'\0';

        Tcl_Obj *e = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, e, PdfiumWideToObj(buf));
        Tcl_ListObjAppendElement(interp, e, Tcl_NewIntObj((int)codes[i]));
        Tcl_ListObjAppendElement(interp, e,
            Tcl_NewDoubleObj(sizes[i].x / 10.0));   /* 0.1 mm -> mm */
        Tcl_ListObjAppendElement(interp, e,
            Tcl_NewDoubleObj(sizes[i].y / 10.0));
        Tcl_ListObjAppendElement(interp, list, e);
    }

    ckfree((char *)names); ckfree((char *)codes);
    ckfree((char *)sizes); ckfree((char *)wp);
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/* Form name -> DEVMODE code, -1 when not found. */
static int
PdfiumPaperCodeByName(const WCHAR *printer, const char *utf8name)
{
    int n = (int)DeviceCapabilitiesW(printer, NULL, DC_PAPERNAMES, NULL, NULL);
    if (n <= 0) return -1;

    WCHAR *names = (WCHAR *)ckalloc((size_t)n * 64 * sizeof(WCHAR));
    WORD  *codes = (WORD  *)ckalloc((size_t)n * sizeof(WORD));
    DeviceCapabilitiesW(printer, NULL, DC_PAPERNAMES, (LPWSTR)names, NULL);
    DeviceCapabilitiesW(printer, NULL, DC_PAPERS,     (LPWSTR)codes, NULL);

    WCHAR *want = PdfiumUtf8ToWide(utf8name);
    int found = -1;
    for (int i = 0; i < n && found < 0; i++) {
        WCHAR buf[65];
        memcpy(buf, names + (size_t)i * 64, 64 * sizeof(WCHAR));
        buf[64] = L'\0';
        if (_wcsicmp(buf, want) == 0) found = (int)codes[i];
    }
    ckfree((char *)want); ckfree((char *)names); ckfree((char *)codes);
    return found;
}

/* Tray name -> DMBIN_* code. Note: DC_BINNAMES uses 24 WCHAR blocks,
 * not 64 as DC_PAPERNAMES does. */
static int
PdfiumBinCodeByName(const WCHAR *printer, const char *utf8name)
{
    int n = (int)DeviceCapabilitiesW(printer, NULL, DC_BINNAMES, NULL, NULL);
    if (n <= 0) return -1;

    WCHAR *names = (WCHAR *)ckalloc((size_t)n * 24 * sizeof(WCHAR));
    WORD  *codes = (WORD  *)ckalloc((size_t)n * sizeof(WORD));
    DeviceCapabilitiesW(printer, NULL, DC_BINNAMES, (LPWSTR)names, NULL);
    DeviceCapabilitiesW(printer, NULL, DC_BINS,     (LPWSTR)codes, NULL);

    WCHAR *want = PdfiumUtf8ToWide(utf8name);
    int found = -1;
    for (int i = 0; i < n && found < 0; i++) {
        WCHAR buf[25];
        memcpy(buf, names + (size_t)i * 24, 24 * sizeof(WCHAR));
        buf[24] = L'\0';
        if (_wcsicmp(buf, want) == 0) found = (int)codes[i];
    }
    ckfree((char *)want); ckfree((char *)names); ckfree((char *)codes);
    return found;
}

/* -------------------------------------------------------------------- */
/* DEVMODE construction                                                  */
/*                                                                       */
/* The Win32 sequence is four steps and step 4 is not optional: without  */
/* it some drivers silently discard inconsistent combinations and the    */
/* problem only surfaces in the spooler.                                 */
/*   1. DocumentProperties(0)              -> required size              */
/*   2. DocumentProperties(DM_OUT_BUFFER)  -> driver defaults            */
/*   3. set fields plus their dmFields bits                              */
/*   4. DocumentProperties(DM_IN|DM_OUT)   -> driver validates           */
/*                                                                       */
/* Step 2 also returns the private driver data behind dmDriverExtra. We  */
/* only overwrite public fields, so driver-specific settings (e.g. the   */
/* Brother auto-cut behaviour) survive into the job.                     */
/* -------------------------------------------------------------------- */
typedef struct {
    int         paper_code;     /* -1 = unchanged */
    const char *paper_name;     /* NULL = unused  */
    double      paperw_mm, paperh_mm;   /* 0 = unused */
    int         orientation;    /* 0 = unchanged, 1 portrait, 2 landscape */
    int         duplex;         /* -1 = unchanged, else DMDUP_* */
    int         source_code;    /* -1 = unchanged */
    const char *source_name;
    int         quality;        /* 0 = unchanged, else DMRES_* or DPI */
    int         copies;         /* 0 = not through DEVMODE */
    int         color;          /* 0 = unchanged, else DMCOLOR_* */
    int         mediatype;      /* 0 = unchanged, else DMMEDIA_* */
} PdfiumDevmodeWish;

static void
PdfiumWishInit(PdfiumDevmodeWish *w)
{
    memset(w, 0, sizeof(*w));
    w->paper_code  = -1;
    w->duplex      = -1;
    w->source_code = -1;
}

static DEVMODEW *
PdfiumBuildDevMode(Tcl_Interp *interp, const WCHAR *printer,
                   const PdfiumDevmodeWish *wish)
{
    HANDLE hPrinter = NULL;
    if (!OpenPrinterW((LPWSTR)printer, &hPrinter, NULL)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "OpenPrinter failed (Win32 error %lu)",
            (unsigned long)GetLastError()));
        return NULL;
    }

    LONG need = DocumentPropertiesW(NULL, hPrinter, (LPWSTR)printer,
                                    NULL, NULL, 0);
    if (need <= 0) {
        ClosePrinter(hPrinter);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "DocumentProperties: driver returned no DEVMODE", -1));
        return NULL;
    }

    DEVMODEW *dm = (DEVMODEW *)ckalloc((size_t)need);
    memset(dm, 0, (size_t)need);

    if (DocumentPropertiesW(NULL, hPrinter, (LPWSTR)printer,
                            dm, NULL, DM_OUT_BUFFER) != IDOK) {
        ckfree((char *)dm); ClosePrinter(hPrinter);
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("cannot read printer defaults", -1));
        return NULL;
    }

    DWORD supported = dm->dmFields;

    /* paper */
    if (wish->paperw_mm > 0.0 && wish->paperh_mm > 0.0) {
        dm->dmPaperSize   = DMPAPER_USER;
        dm->dmPaperWidth  = (short)(wish->paperw_mm * 10.0 + 0.5);
        dm->dmPaperLength = (short)(wish->paperh_mm * 10.0 + 0.5);
        dm->dmFields |= DM_PAPERSIZE | DM_PAPERWIDTH | DM_PAPERLENGTH;
    } else {
        int code = wish->paper_code;
        if (code < 0 && wish->paper_name) {
            code = PdfiumPaperCodeByName(printer, wish->paper_name);
            if (code < 0) {
                ckfree((char *)dm); ClosePrinter(hPrinter);
                Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                    "unknown paper \"%s\" -- see ::pdfium::papers",
                    wish->paper_name));
                return NULL;
            }
        }
        if (code >= 0) {
            dm->dmPaperSize = (short)code;
            dm->dmFields |= DM_PAPERSIZE;
        }
    }

    /* orientation */
    if (wish->orientation) {
        dm->dmOrientation = (short)(wish->orientation == 2
                                    ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT);
        dm->dmFields |= DM_ORIENTATION;
    }

    /* duplex -- deliberately a hard failure: printing a duplex job
     * silently single-sided costs more than an error message */
    if (wish->duplex >= 0) {
        if (!(supported & DM_DUPLEX) ||
            DeviceCapabilitiesW(printer, NULL, DC_DUPLEX, NULL, NULL) != 1) {
            if (wish->duplex != DMDUP_SIMPLEX) {
                ckfree((char *)dm); ClosePrinter(hPrinter);
                Tcl_SetObjResult(interp,
                    Tcl_NewStringObj("printer does not support duplex", -1));
                return NULL;
            }
        }
        dm->dmDuplex = (short)wish->duplex;
        dm->dmFields |= DM_DUPLEX;
    }

    /* paper source */
    {
        int bin = wish->source_code;
        if (bin < 0 && wish->source_name) {
            bin = PdfiumBinCodeByName(printer, wish->source_name);
            if (bin < 0) {
                ckfree((char *)dm); ClosePrinter(hPrinter);
                Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                    "unknown paper source \"%s\"", wish->source_name));
                return NULL;
            }
        }
        if (bin >= 0) {
            dm->dmDefaultSource = (short)bin;
            dm->dmFields |= DM_DEFAULTSOURCE;
        }
    }

    /* quality: positive = DPI, negative = DMRES_* */
    if (wish->quality) {
        dm->dmPrintQuality = (short)wish->quality;
        dm->dmFields |= DM_PRINTQUALITY;
        if (wish->quality > 0) {
            dm->dmYResolution = (short)wish->quality;
            dm->dmFields |= DM_YRESOLUTION;
        }
    }

    /* colour and media type describe hardware, not geometry -- these
     * belong in the DEVMODE, unlike scaling and n-up */
    if (wish->color) {
        dm->dmColor = (short)wish->color;
        dm->dmFields |= DM_COLOR;
    }
    if (wish->mediatype) {
        dm->dmMediaType = (DWORD)wish->mediatype;
        dm->dmFields |= DM_MEDIATYPE;
    }

    /* copies through the driver: one spool job, collated */
    if (wish->copies > 1) {
        dm->dmCopies  = (short)wish->copies;
        dm->dmCollate = DMCOLLATE_TRUE;
        dm->dmFields |= DM_COPIES | DM_COLLATE;
    }

    if (DocumentPropertiesW(NULL, hPrinter, (LPWSTR)printer,
                            dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK) {
        ckfree((char *)dm); ClosePrinter(hPrinter);
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("driver rejected the print settings", -1));
        return NULL;
    }

    ClosePrinter(hPrinter);
    return dm;
}

/* -------------------------------------------------------------------- */
/* pdfium::printercaps ?printer? ?-paper form?                           */
/*                                                                       */
/* Borderless is a property of the driver, not an option. It can be      */
/* measured: on a borderless form the printable area reaches the sheet   */
/* or overfills it, so the margins are zero or negative.                 */
/* -------------------------------------------------------------------- */
static int
PdfiumPrinterCapsCmd(ClientData cd, Tcl_Interp *interp,
                     int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    const char *paper  = NULL;
    const char *pname  = NULL;
    double paperw_mm = 0.0, paperh_mm = 0.0;
    int argi = 1;

    if (objc >= 2 && Tcl_GetString(objv[1])[0] != '-') {
        pname = Tcl_GetString(objv[1]);
        argi = 2;
    }
    if (((objc - argi) % 2) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv,
            "?printer? ?-paper form? ?-paperw mm -paperh mm?");
        return TCL_ERROR;
    }
    for (int i = argi; i < objc; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        if (strcmp(opt, "-paper") == 0) {
            paper = Tcl_GetString(objv[i + 1]);
        } else if (strcmp(opt, "-paperw") == 0) {
            if (Tcl_GetDoubleFromObj(interp, objv[i + 1], &paperw_mm) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-paperh") == 0) {
            if (Tcl_GetDoubleFromObj(interp, objv[i + 1], &paperh_mm) != TCL_OK)
                return TCL_ERROR;
        } else {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown option \"%s\"", opt));
            return TCL_ERROR;
        }
    }

    WCHAR *wprinter = PdfiumResolvePrinter(interp, pname);
    if (!wprinter) return TCL_ERROR;

    /* Selecting a form or a custom size matters: otherwise one only ever
     * measures the driver default. Custom sizes are the interesting case
     * on continuous-tape printers, where the named forms carry only a
     * nominal length -- a Brother QL-820NWB reports 29 mm for every tape
     * regardless of how long the label actually is. Measuring shows
     * whether the driver honours dmPaperLength or quietly ignores it. */
    DEVMODEW *dm = NULL;
    if (paper || (paperw_mm > 0.0 && paperh_mm > 0.0)) {
        PdfiumDevmodeWish wish;
        PdfiumWishInit(&wish);
        if (paperw_mm > 0.0 && paperh_mm > 0.0) {
            wish.paperw_mm = paperw_mm;
            wish.paperh_mm = paperh_mm;
        } else {
            int code;
            Tcl_Obj *o = Tcl_NewStringObj(paper, -1);
            Tcl_IncrRefCount(o);
            if (Tcl_GetIntFromObj(NULL, o, &code) == TCL_OK)
                wish.paper_code = code;
            else
                wish.paper_name = paper;
            Tcl_DecrRefCount(o);
        }
        dm = PdfiumBuildDevMode(interp, wprinter, &wish);
        if (!dm) { ckfree((char *)wprinter); return TCL_ERROR; }
    }

    HDC hdc = CreateDCW(NULL, wprinter, NULL, dm);
    if (dm) ckfree((char *)dm);
    if (!hdc) {
        ckfree((char *)wprinter);
        PDFIUM_ERROR(interp, "cannot open printer device context");
    }

    int paper_w = GetDeviceCaps(hdc, PHYSICALWIDTH);
    int paper_h = GetDeviceCaps(hdc, PHYSICALHEIGHT);
    int off_x   = GetDeviceCaps(hdc, PHYSICALOFFSETX);
    int off_y   = GetDeviceCaps(hdc, PHYSICALOFFSETY);
    int area_w  = GetDeviceCaps(hdc, HORZRES);
    int area_h  = GetDeviceCaps(hdc, VERTRES);
    int dpi_x   = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpi_y   = GetDeviceCaps(hdc, LOGPIXELSY);
    int planes  = GetDeviceCaps(hdc, PLANES);
    int bits    = GetDeviceCaps(hdc, BITSPIXEL);
    int colres  = GetDeviceCaps(hdc, NUMCOLORS);

    DeleteDC(hdc);

    if (dpi_x <= 0) dpi_x = 1;
    if (dpi_y <= 0) dpi_y = 1;

    double mmx = 25.4 / dpi_x;
    double mmy = 25.4 / dpi_y;

    double m_l = off_x * mmx;
    double m_t = off_y * mmy;
    double m_r = (paper_w - off_x - area_w) * mmx;
    double m_b = (paper_h - off_y - area_h) * mmy;

    int borderless = (m_l <= 0.1 && m_t <= 0.1 && m_r <= 0.1 && m_b <= 0.1);

    Tcl_Obj *d = Tcl_NewDictObj();
#define PDFIUM_PUTD(k, v) \
    Tcl_DictObjPut(interp, d, Tcl_NewStringObj((k), -1), (v))

    PDFIUM_PUTD("printer",     PdfiumWideToObj(wprinter));
    PDFIUM_PUTD("dpi_x",       Tcl_NewIntObj(dpi_x));
    PDFIUM_PUTD("dpi_y",       Tcl_NewIntObj(dpi_y));
    PDFIUM_PUTD("paper_w_mm",  Tcl_NewDoubleObj(paper_w * mmx));
    PDFIUM_PUTD("paper_h_mm",  Tcl_NewDoubleObj(paper_h * mmy));
    PDFIUM_PUTD("print_w_mm",  Tcl_NewDoubleObj(area_w * mmx));
    PDFIUM_PUTD("print_h_mm",  Tcl_NewDoubleObj(area_h * mmy));
    PDFIUM_PUTD("margin_l_mm", Tcl_NewDoubleObj(m_l));
    PDFIUM_PUTD("margin_r_mm", Tcl_NewDoubleObj(m_r));
    PDFIUM_PUTD("margin_t_mm", Tcl_NewDoubleObj(m_t));
    PDFIUM_PUTD("margin_b_mm", Tcl_NewDoubleObj(m_b));
    PDFIUM_PUTD("borderless",  Tcl_NewBooleanObj(borderless));
    /* What was asked for, so the caller can compare against what the
     * driver actually delivered. */
    PDFIUM_PUTD("want_w_mm",   Tcl_NewDoubleObj(paperw_mm));
    PDFIUM_PUTD("want_h_mm",   Tcl_NewDoubleObj(paperh_mm));
    PDFIUM_PUTD("planes",      Tcl_NewIntObj(planes));
    PDFIUM_PUTD("bitspixel",   Tcl_NewIntObj(bits));
    PDFIUM_PUTD("numcolors",   Tcl_NewIntObj(colres));
#undef PDFIUM_PUTD

    ckfree((char *)wprinter);
    Tcl_SetObjResult(interp, d);
    return TCL_OK;
}

/* -------------------------------------------------------------------- */
/* Grid for n pages per sheet.                                           */
/*                                                                       */
/* The split follows the sheet, not a fixed table: on landscape two      */
/* pages belong side by side, on portrait one above the other.           */
/* -------------------------------------------------------------------- */
static void
PdfiumNupGrid(int n, int area_w, int area_h, int *cols, int *rows)
{
    int a, b;
    switch (n) {
        case 1:  a = 1; b = 1; break;
        case 2:  a = 2; b = 1; break;
        case 4:  a = 2; b = 2; break;
        case 6:  a = 3; b = 2; break;
        case 8:  a = 4; b = 2; break;
        case 9:  a = 3; b = 3; break;
        case 16: a = 4; b = 4; break;
        default: a = n; b = 1; break;
    }
    if (area_w >= area_h) { *cols = a; *rows = b; }
    else                  { *cols = b; *rows = a; }
}

/* -------------------------------------------------------------------- */
/* pdfium::print doc ?-option value ...?                                 */
/* -------------------------------------------------------------------- */
static int
PdfiumPrintCmd(ClientData cd, Tcl_Interp *interp,
               int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc < 2 || (objc % 2) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv, "doc-handle ?-option value ...?");
        return TCL_ERROR;
    }

    Tcl_WideInt ptr;
    if (Tcl_GetWideIntFromObj(interp, objv[1], &ptr) != TCL_OK)
        return TCL_ERROR;
    FPDF_DOCUMENT doc = (FPDF_DOCUMENT)(intptr_t)ptr;

    int total = FPDF_GetPageCount(doc);
    if (total <= 0) PDFIUM_ERROR(interp, "document has no pages");

    /* defaults */
    const char *printer = NULL;
    const char *docname = "Tcl PDFium Job";
    int    from = 0, to = total - 1, copies = 1;
    int    mode = 0;                  /* FPDF_PRINTMODE_EMF */
    int    rotate_deg = 0, fit = 1;
    int    nup = 1, nuporder_cols = 0;
    double scale_pct = 0.0;           /* 0 = off, -fit applies */
    double mm_l = 0.0, mm_r = 0.0, mm_t = 0.0, mm_b = 0.0;
    int    have_margin[4] = {0, 0, 0, 0};

    PdfiumDevmodeWish wish;
    PdfiumWishInit(&wish);

    for (int i = 2; i < objc; i += 2) {
        const char *opt = Tcl_GetString(objv[i]);
        Tcl_Obj    *val = objv[i + 1];

        if (strcmp(opt, "-printer") == 0) {
            printer = Tcl_GetString(val);
        } else if (strcmp(opt, "-docname") == 0) {
            docname = Tcl_GetString(val);
        } else if (strcmp(opt, "-from") == 0) {
            if (Tcl_GetIntFromObj(interp, val, &from) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-to") == 0) {
            if (Tcl_GetIntFromObj(interp, val, &to) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-copies") == 0) {
            if (Tcl_GetIntFromObj(interp, val, &copies) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-mode") == 0) {
            if (Tcl_GetIntFromObj(interp, val, &mode) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-rotate") == 0) {
            if (Tcl_GetIntFromObj(interp, val, &rotate_deg) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-fit") == 0) {
            if (Tcl_GetBooleanFromObj(interp, val, &fit) != TCL_OK)
                return TCL_ERROR;

        } else if (strcmp(opt, "-paper") == 0) {
            const char *v = Tcl_GetString(val);
            if      (!strcmp(v, "a4"))     wish.paper_code = DMPAPER_A4;
            else if (!strcmp(v, "a5"))     wish.paper_code = DMPAPER_A5;
            else if (!strcmp(v, "a3"))     wish.paper_code = DMPAPER_A3;
            else if (!strcmp(v, "letter")) wish.paper_code = DMPAPER_LETTER;
            else if (!strcmp(v, "legal"))  wish.paper_code = DMPAPER_LEGAL;
            else {
                int code;
                if (Tcl_GetIntFromObj(NULL, val, &code) == TCL_OK)
                    wish.paper_code = code;
                else
                    wish.paper_name = v;
            }
        } else if (strcmp(opt, "-paperw") == 0) {
            if (Tcl_GetDoubleFromObj(interp, val, &wish.paperw_mm) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-paperh") == 0) {
            if (Tcl_GetDoubleFromObj(interp, val, &wish.paperh_mm) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-orientation") == 0) {
            const char *v = Tcl_GetString(val);
            if      (!strcmp(v, "portrait"))  wish.orientation = 1;
            else if (!strcmp(v, "landscape")) wish.orientation = 2;
            else PDFIUM_ERROR(interp, "-orientation: portrait|landscape");
        } else if (strcmp(opt, "-duplex") == 0) {
            const char *v = Tcl_GetString(val);
            if      (!strcmp(v, "off")   || !strcmp(v, "simplex"))
                wish.duplex = DMDUP_SIMPLEX;
            else if (!strcmp(v, "long")  || !strcmp(v, "vertical"))
                wish.duplex = DMDUP_VERTICAL;
            else if (!strcmp(v, "short") || !strcmp(v, "horizontal"))
                wish.duplex = DMDUP_HORIZONTAL;
            else PDFIUM_ERROR(interp, "-duplex: off|long|short");
        } else if (strcmp(opt, "-source") == 0) {
            int code;
            if (Tcl_GetIntFromObj(NULL, val, &code) == TCL_OK)
                wish.source_code = code;
            else
                wish.source_name = Tcl_GetString(val);
        } else if (strcmp(opt, "-quality") == 0) {
            const char *v = Tcl_GetString(val);
            if      (!strcmp(v, "draft"))  wish.quality = DMRES_DRAFT;
            else if (!strcmp(v, "low"))    wish.quality = DMRES_LOW;
            else if (!strcmp(v, "medium")) wish.quality = DMRES_MEDIUM;
            else if (!strcmp(v, "high"))   wish.quality = DMRES_HIGH;
            else if (Tcl_GetIntFromObj(interp, val, &wish.quality) != TCL_OK)
                return TCL_ERROR;
        } else if (strcmp(opt, "-color") == 0) {
            const char *v = Tcl_GetString(val);
            if      (!strcmp(v, "mono")) wish.color = DMCOLOR_MONOCHROME;
            else if (!strcmp(v, "auto")) wish.color = DMCOLOR_COLOR;
            else PDFIUM_ERROR(interp, "-color: auto|mono");
        } else if (strcmp(opt, "-mediatype") == 0) {
            if (Tcl_GetIntFromObj(interp, val, &wish.mediatype) != TCL_OK)
                return TCL_ERROR;

        } else if (strcmp(opt, "-nup") == 0) {
            if (Tcl_GetIntFromObj(interp, val, &nup) != TCL_OK)
                return TCL_ERROR;
            if (nup < 1 || nup > 64) PDFIUM_ERROR(interp, "-nup: 1..64");
        } else if (strcmp(opt, "-nuporder") == 0) {
            const char *v = Tcl_GetString(val);
            if      (!strcmp(v, "rows")) nuporder_cols = 0;
            else if (!strcmp(v, "cols")) nuporder_cols = 1;
            else PDFIUM_ERROR(interp, "-nuporder: rows|cols");
        } else if (strcmp(opt, "-scale") == 0) {
            if (Tcl_GetDoubleFromObj(interp, val, &scale_pct) != TCL_OK)
                return TCL_ERROR;
            if (scale_pct <= 0.0)
                PDFIUM_ERROR(interp, "-scale must be positive");
        } else if (strcmp(opt, "-margin") == 0) {
            double m;
            if (Tcl_GetDoubleFromObj(interp, val, &m) != TCL_OK)
                return TCL_ERROR;
            if (!have_margin[0]) mm_l = m;
            if (!have_margin[1]) mm_r = m;
            if (!have_margin[2]) mm_t = m;
            if (!have_margin[3]) mm_b = m;
        } else if (strcmp(opt, "-marginl") == 0) {
            if (Tcl_GetDoubleFromObj(interp, val, &mm_l) != TCL_OK)
                return TCL_ERROR;
            have_margin[0] = 1;
        } else if (strcmp(opt, "-marginr") == 0) {
            if (Tcl_GetDoubleFromObj(interp, val, &mm_r) != TCL_OK)
                return TCL_ERROR;
            have_margin[1] = 1;
        } else if (strcmp(opt, "-margint") == 0) {
            if (Tcl_GetDoubleFromObj(interp, val, &mm_t) != TCL_OK)
                return TCL_ERROR;
            have_margin[2] = 1;
        } else if (strcmp(opt, "-marginb") == 0) {
            if (Tcl_GetDoubleFromObj(interp, val, &mm_b) != TCL_OK)
                return TCL_ERROR;
            have_margin[3] = 1;

        } else {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown option \"%s\"", opt));
            return TCL_ERROR;
        }
    }

    if (from < 0) from = 0;
    if (to >= total) to = total - 1;
    if (from > to)  PDFIUM_ERROR(interp, "empty page range");
    if (copies < 1) copies = 1;

    int rotate = ((rotate_deg % 360) + 360) % 360 / 90;   /* 0..3 */

    WCHAR *wprinter = PdfiumResolvePrinter(interp, printer);
    if (!wprinter) return TCL_ERROR;

    /* Prefer driver-side copies (one spool job, collated). If the driver
     * cannot do them, the loop below stays responsible. */
    int hw_copies = (int)DeviceCapabilitiesW(wprinter, NULL,
                                             DC_COPIES, NULL, NULL);
    int loop_copies = copies;
    if (copies > 1 && hw_copies >= copies) {
        wish.copies = copies;
        loop_copies = 1;
    }

    DEVMODEW *dm = PdfiumBuildDevMode(interp, wprinter, &wish);
    if (!dm) { ckfree((char *)wprinter); return TCL_ERROR; }

    HDC hdc = CreateDCW(NULL, wprinter, NULL, dm);
    ckfree((char *)dm);
    if (!hdc) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "cannot open printer device context (Win32 error %lu)",
            (unsigned long)GetLastError()));
        ckfree((char *)wprinter);
        return TCL_ERROR;
    }
    ckfree((char *)wprinter);

    int paper_w = GetDeviceCaps(hdc, PHYSICALWIDTH);
    int paper_h = GetDeviceCaps(hdc, PHYSICALHEIGHT);
    int off_x   = GetDeviceCaps(hdc, PHYSICALOFFSETX);
    int off_y   = GetDeviceCaps(hdc, PHYSICALOFFSETY);
    int area_w  = GetDeviceCaps(hdc, HORZRES);
    int area_h  = GetDeviceCaps(hdc, VERTRES);
    int dpi_x   = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpi_y   = GetDeviceCaps(hdc, LOGPIXELSY);
    if (dpi_x <= 0) dpi_x = 300;
    if (dpi_y <= 0) dpi_y = 300;

    /* Global setting, not a per-call parameter. Reset to 0 if the same
     * process also renders to screen afterwards. */
    FPDF_SetPrintMode(mode);

    /* Content box. Margins are measured from the PAPER EDGE -- that is
     * how users state them -- while the DC origin sits at the printable
     * area, hence the shift by off_x/off_y. The box is then clipped to
     * the printable area: a margin smaller than the hardware margin
     * cannot be honoured, which is the normal case for -margin 0. */
    int ml = (int)(mm_l / 25.4 * dpi_x + 0.5);
    int mr = (int)(mm_r / 25.4 * dpi_x + 0.5);
    int mt = (int)(mm_t / 25.4 * dpi_y + 0.5);
    int mb = (int)(mm_b / 25.4 * dpi_y + 0.5);

    int box_l = -off_x + ml;
    int box_t = -off_y + mt;
    int box_r = -off_x + paper_w - mr;
    int box_b = -off_y + paper_h - mb;

    if (box_l < 0)      box_l = 0;
    if (box_t < 0)      box_t = 0;
    if (box_r > area_w) box_r = area_w;
    if (box_b > area_h) box_b = area_h;

    int box_w = box_r - box_l;
    int box_h = box_b - box_t;

    if (box_w <= 0 || box_h <= 0) {
        DeleteDC(hdc);
        PDFIUM_ERROR(interp, "margins leave no printable area");
    }

    int cols, rows;
    PdfiumNupGrid(nup, box_w, box_h, &cols, &rows);
    int per_sheet = cols * rows;

    /* With more than one page per sheet each cell gets a small gutter,
     * otherwise the pages touch and the boundary is invisible. */
    int gutter = (per_sheet > 1) ? (int)(2.0 / 25.4 * dpi_x + 0.5) : 0;
    int cell_w = box_w / cols;
    int cell_h = box_h / rows;

    DOCINFOW di;
    memset(&di, 0, sizeof(di));
    di.cbSize = sizeof(di);
    WCHAR *wdoc = PdfiumUtf8ToWide(docname);
    di.lpszDocName = wdoc;

    if (StartDocW(hdc, &di) <= 0) {
        ckfree((char *)wdoc);
        DeleteDC(hdc);
        PDFIUM_ERROR(interp, "StartDoc failed");
    }

    int printed = 0;
    int failed  = 0;

    for (int c = 0; c < loop_copies && !failed; c++) {
        for (int p = from; p <= to && !failed; p += per_sheet) {

            if (StartPage(hdc) <= 0) { failed = 1; break; }

            for (int k = 0; k < per_sheet && (p + k) <= to; k++) {

                FPDF_PAGE page = FPDF_LoadPage(doc, p + k);
                if (!page) { failed = 1; break; }

                double nat_w = FPDF_GetPageWidth(page)  / 72.0 * dpi_x;
                double nat_h = FPDF_GetPageHeight(page) / 72.0 * dpi_y;

                int cw = cell_w - gutter;
                int ch = cell_h - gutter;
                if (cw < 1) cw = 1;
                if (ch < 1) ch = 1;

                double s;
                if (scale_pct > 0.0) {
                    /* Fixed scaling is exact, even if it overflows the
                     * cell: someone asking for 200 % wants 200 %. */
                    s = scale_pct / 100.0;
                } else if (fit) {
                    s = PDFIUM_MIN(cw / nat_w, ch / nat_h);
                } else {
                    s = 1.0;
                }

                int w = (int)(nat_w * s + 0.5);
                int h = (int)(nat_h * s + 0.5);

                int ci, ri;
                if (nuporder_cols) { ci = k / rows; ri = k % rows; }
                else               { ci = k % cols; ri = k / cols; }

                int cell_x = box_l + ci * cell_w;
                int cell_y = box_t + ri * cell_h;

                int x, y;
                if (fit || scale_pct > 0.0) {
                    x = cell_x + (cw - w) / 2;
                    y = cell_y + (ch - h) / 2;
                } else {
                    /* 1:1 at the cell corner -- predictable position,
                     * which matters for labels */
                    x = cell_x;
                    y = cell_y;
                }

                FPDF_RenderPage(hdc, page, x, y, w, h, rotate,
                                FPDF_ANNOT | FPDF_PRINTING);
                FPDF_ClosePage(page);
                printed++;
            }

            if (EndPage(hdc) <= 0) failed = 1;
        }
    }

    if (failed) {
        AbortDoc(hdc);
        DeleteDC(hdc);
        ckfree((char *)wdoc);
        PDFIUM_ERROR(interp, "printing aborted");
    }

    EndDoc(hdc);
    DeleteDC(hdc);
    ckfree((char *)wdoc);

    Tcl_SetObjResult(interp, Tcl_NewIntObj(printed));
    return TCL_OK;
}

#endif /* _WIN32 */

/* -------------------------------------------------------------------- */
/* pdfium::canprint  --  available on every platform, so callers can     */
/* branch instead of catching errors                                     */
/* -------------------------------------------------------------------- */
static int
PdfiumCanPrintCmd(ClientData cd, Tcl_Interp *interp,
                  int objc, Tcl_Obj *const objv[])
{
    (void)cd;
    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
#ifdef _WIN32
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
#else
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
#endif
    return TCL_OK;
}


PDFIUMTCL_EXPORT int
Pdfiumtcl_Init(Tcl_Interp *interp)
{
    /* Stubs initialisieren -- Tcl 9 braucht "9.0", Tcl 8 "8.5" */
#if TCL_MAJOR_VERSION >= 9
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
#else
    if (Tcl_InitStubs(interp, "8.5", 0) == NULL) return TCL_ERROR;
#endif
    /* Tk-Stubs NICHT hier -- nur pdfium::render braucht Tk (lazy in EnsureTk). */

    FPDF_InitLibrary();

    Tcl_Eval(interp, "namespace eval ::pdfium {}");

    Tcl_CreateObjCommand(interp, "::pdfium::open",
                         PdfiumOpenCmd,       NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::close",
                         PdfiumCloseCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::pagecount",
                         PdfiumPageCountCmd,  NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::render",
                         PdfiumRenderCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::gettext",
                         PdfiumGetTextCmd,    NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::pagesize",
                         PdfiumPageSizeCmd,   NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::meta",
                         PdfiumMetaCmd,       NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::rotation",
                         PdfiumRotationCmd,   NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::search",
                         PdfiumSearchCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::images",
                         PdfiumImagesCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::pageobjects",
                         PdfiumPageObjectsCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::charboxes",
                         PdfiumCharBoxesCmd,   NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::flatten",
                         PdfiumFlattenCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::catalog",
                         PdfiumCatalogCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::editbegin",
                         PdfiumEditBeginCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::editclick",
                         PdfiumEditClickCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::editchar",
                         PdfiumEditCharCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::editkey",
                         PdfiumEditKeyCmd,       NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::editrender",
                         PdfiumEditRenderCmd,    NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::edittext",
                         PdfiumEditTextCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::editstate",
                         PdfiumEditStateCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::editend",
                         PdfiumEditEndCmd,       NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::formfill",
                         PdfiumFormFillCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::addannot",
                         PdfiumAddAnnotCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::delannot",
                         PdfiumDelAnnotCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::signatures",
                         PdfiumSignaturesCmd,    NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::attachments",
                         PdfiumAttachmentsCmd,   NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::attachment",
                         PdfiumAttachmentCmd,    NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::addattachment",
                         PdfiumAddAttachmentCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::delattachment",
                         PdfiumDelAttachmentCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::links",
                         PdfiumLinksCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::structure",
                         PdfiumStructureCmd,  NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::bookmarks",
                         PdfiumBookmarksCmd,  NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::formcheck",
                         PdfiumFormCheckCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::formfields",
                         PdfiumFormFieldsCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::annot_list",
                         PdfiumAnnotListCmd,  NULL, NULL);

    /* --- write / edit (0.4) --- */
    Tcl_CreateObjCommand(interp, "::pdfium::newdoc",
                         PdfiumNewDocCmd,          NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::newpage",
                         PdfiumNewPageCmd,         NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::closepage",
                         PdfiumClosePageCmd,       NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::generatecontent",
                         PdfiumGenerateContentCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::importpages",
                         PdfiumImportPagesCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::setcropbox",
                         PdfiumSetCropBoxCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::setmediabox",
                         PdfiumSetMediaBoxCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::addimagejpeg",
                         PdfiumAddImageJpegCmd,    NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::addimagebitmap",
                         PdfiumAddImageBitmapCmd,  NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::deletepage",
                         PdfiumDeletePageCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::setrotation",
                         PdfiumSetRotationCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::mctext",
                         PdfiumMcTextCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::save",
                         PdfiumSaveCmd,            NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::savewithversion",
                         PdfiumSaveWithVersionCmd, NULL, NULL);

    /* --- Drucken ------------------------------------------------------ */
    Tcl_CreateObjCommand(interp, "::pdfium::canprint",
                         PdfiumCanPrintCmd,        NULL, NULL);
#ifdef _WIN32
    Tcl_CreateObjCommand(interp, "::pdfium::printers",
                         PdfiumPrintersCmd,        NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::defaultprinter",
                         PdfiumDefaultPrinterCmd,  NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::papers",
                         PdfiumPapersCmd,          NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::printercaps",
                         PdfiumPrinterCapsCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::print",
                         PdfiumPrintCmd,           NULL, NULL);
#endif

    Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION);
    return TCL_OK;
}
