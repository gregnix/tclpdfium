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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Tcl_Size: ab Tcl 9 definiert, fuer Tcl 8 als int */
#ifndef TCL_SIZE_MAX
    typedef int Tcl_Size;
#endif

/* Windows DLL-Export — noetig fuer MinGW ohne --export-all-symbols */
#ifdef _WIN32
#  define PDFIUMTCL_EXPORT __declspec(dllexport)
#else
#  define PDFIUMTCL_EXPORT
#endif

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
                         "doc-handle pagenum ?-dpi n? ?-width px? ?-imagename name?");
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
    char imgname[64];
    snprintf(imgname, sizeof(imgname), "pdfimg%d", pagenum);

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
        }
    }

    FPDF_DOCUMENT doc  = (FPDF_DOCUMENT)(intptr_t)ptr;
    FPDF_PAGE     page = FPDF_LoadPage(doc, pagenum);
    if (!page) PDFIUM_ERROR(interp, "cannot load page");

    /* Seitengröße in Punkten */
    double w_pt = FPDF_GetPageWidth(page);
    double h_pt = FPDF_GetPageHeight(page);

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
    FPDF_RenderPageBitmap(bmp, page, 0, 0, w_px, h_px,
                          0 /*rotation*/, FPDF_ANNOT);

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

/* ------------------------------------------------------------------ */
/* pdfium::search doc-handle pagenum searchtext ?-case 0|1?           */
/* Gibt Liste von {startpos count} zurück (Zeichenpositionen).        */
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
    if (objc >= 6) {
        const char *opt = Tcl_GetString(objv[4]);
        if (strcmp(opt, "-case") == 0)
            Tcl_GetIntFromObj(interp, objv[5], &casesensitive);
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
/* pdfium::formfields doc-handle pagenum                               */
/* Gibt Liste von Dicts zurück:                                        */
/*   {type name value}                                                 */
/* ------------------------------------------------------------------ */
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

    int n = FPDFPage_GetAnnotCount(page);
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);

    for (int i = 0; i < n; i++) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;

        FPDF_ANNOTATION_SUBTYPE subtype = FPDFAnnot_GetSubtype(annot);
        if (subtype != FPDF_ANNOT_WIDGET) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        /* Feldname */
        unsigned long nlen = FPDFAnnot_GetStringValue(annot, "T", NULL, 0);
        unsigned short *nbuf = (unsigned short *)ckalloc(nlen + 2);
        FPDFAnnot_GetStringValue(annot, "T", nbuf, nlen);
        int nnchars = (int)((nlen / 2) - 1);
        if (nnchars < 0) nnchars = 0;
        Tcl_Obj *name = _AnnotUtf16ToObj(interp, (unsigned short *)nbuf, (unsigned long)nlen);
        ckfree((char *)nbuf);

        /* Feldwert */
        unsigned long vlen = FPDFAnnot_GetStringValue(annot, "V", NULL, 0);
        Tcl_Obj *value;
        if (vlen > 0) {
            unsigned short *vbuf = (unsigned short *)ckalloc(vlen + 2);
            FPDFAnnot_GetStringValue(annot, "V", vbuf, vlen);
            int vnchars = (int)((vlen / 2) - 1);
            if (vnchars < 0) vnchars = 0;
            value = _AnnotUtf16ToObj(interp, (unsigned short *)vbuf, (unsigned long)vlen);
            ckfree((char *)vbuf);
        } else {
            value = Tcl_NewStringObj("", 0);
        }

        /* Feldtyp aus FT-Eintrag des Annotation-Dicts */
        const char *typstr = "widget";
        unsigned long ftlen = FPDFAnnot_GetStringValue(annot, "FT", NULL, 0);
        if (ftlen > 0) {
            unsigned short *ftbuf =
                (unsigned short *)ckalloc(ftlen + 2);
            FPDFAnnot_GetStringValue(annot, "FT", ftbuf, ftlen);
            char ft[16] = {0};
            for (int k = 0; k < 15 && ftbuf[k]; k++)
                ft[k] = (char)(ftbuf[k] & 0xFF);
            ckfree((char *)ftbuf);
            if      (strcmp(ft, "Tx")  == 0) typstr = "text";
            else if (strcmp(ft, "Btn") == 0) typstr = "button";
            else if (strcmp(ft, "Ch")  == 0) typstr = "choice";
            else if (strcmp(ft, "Sig") == 0) typstr = "signature";
        }

        Tcl_Obj *entry = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, entry,
                                 Tcl_NewStringObj(typstr, -1));
        Tcl_ListObjAppendElement(interp, entry, name);
        Tcl_ListObjAppendElement(interp, entry, value);
        Tcl_ListObjAppendElement(interp, result, entry);

        FPDFPage_CloseAnnot(annot);
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
    Tcl_CreateObjCommand(interp, "::pdfium::links",
                         PdfiumLinksCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::bookmarks",
                         PdfiumBookmarksCmd,  NULL, NULL);
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
    Tcl_CreateObjCommand(interp, "::pdfium::save",
                         PdfiumSaveCmd,            NULL, NULL);
    Tcl_CreateObjCommand(interp, "::pdfium::savewithversion",
                         PdfiumSaveWithVersionCmd, NULL, NULL);

    Tcl_PkgProvide(interp, "pdfiumtcl", "0.5.2");
    return TCL_OK;
}
