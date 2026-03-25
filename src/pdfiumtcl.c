/*
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
 */

#include <tcl.h>
#include <tk.h>
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_doc.h>
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Hilfsmakro: Fehler setzen und TCL_ERROR zurückgeben                 */
/* ------------------------------------------------------------------ */
#define PDFIUM_ERROR(interp, msg) \
    do { Tcl_SetResult(interp, (char*)(msg), TCL_STATIC); return TCL_ERROR; } while(0)

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
static int
PdfiumRenderCmd(ClientData cd, Tcl_Interp *interp,
                int objc, Tcl_Obj *const objv[])
{
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

    /* PDFium-Bitmap anlegen (BGRA) */
    FPDF_BITMAP bmp = FPDFBitmap_Create(w_px, h_px, 0 /*kBGR*/);
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

    /* BGRA → RGBA umwandeln (Tk erwartet RGBA) */
    unsigned char *rgba = (unsigned char *)ckalloc(w_px * h_px * 4);
    unsigned char *src  = (unsigned char *)buf;
    for (int y = 0; y < h_px; y++) {
        unsigned char *row = src + y * stride;
        unsigned char *dst = rgba + y * w_px * 4;
        for (int x = 0; x < w_px; x++) {
            dst[0] = row[2]; /* R */
            dst[1] = row[1]; /* G */
            dst[2] = row[0]; /* B */
            dst[3] = row[3]; /* A */
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
    Tcl_Obj *result = Tcl_NewUnicodeObj((Tcl_UniChar *)buf16, nchars);
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

    Tcl_Obj *result = Tcl_NewUnicodeObj((Tcl_UniChar *)buf, nchars);
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

    /* Suchbegriff als UTF-16LE */
    const char *term_utf8 = Tcl_GetString(objv[3]);
    Tcl_Obj *termObj = Tcl_NewStringObj(term_utf8, -1);
    int termlen;
    Tcl_UniChar *termUni = Tcl_GetUnicodeFromObj(termObj, &termlen);

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
    Tcl_DecrRefCount(termObj);

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
            Tcl_Obj *url = Tcl_NewUnicodeObj((Tcl_UniChar *)buf, nchars);
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
        /* Titel */
        unsigned long len = FPDFBookmark_GetTitle(bm, NULL, 0);
        unsigned short *buf = (unsigned short *)ckalloc(len + 2);
        FPDFBookmark_GetTitle(bm, buf, len);
        int nchars = (int)((len / 2) - 1);
        if (nchars < 0) nchars = 0;
        Tcl_Obj *title = Tcl_NewUnicodeObj((Tcl_UniChar *)buf, nchars);
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
        Tcl_Obj *name = Tcl_NewUnicodeObj((Tcl_UniChar *)nbuf, nnchars);
        ckfree((char *)nbuf);

        /* Feldwert */
        unsigned long vlen = FPDFAnnot_GetStringValue(annot, "V", NULL, 0);
        Tcl_Obj *value;
        if (vlen > 0) {
            unsigned short *vbuf = (unsigned short *)ckalloc(vlen + 2);
            FPDFAnnot_GetStringValue(annot, "V", vbuf, vlen);
            int vnchars = (int)((vlen / 2) - 1);
            if (vnchars < 0) vnchars = 0;
            value = Tcl_NewUnicodeObj((Tcl_UniChar *)vbuf, vnchars);
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
/* Pdfiumtcl_Init  --  wird von "load" aufgerufen                      */
/* ------------------------------------------------------------------ */
int
Pdfiumtcl_Init(Tcl_Interp *interp)
{
    if (Tcl_InitStubs(interp, "8.5", 0) == NULL) return TCL_ERROR;
    if (Tk_InitStubs(interp, "8.5", 0)  == NULL) return TCL_ERROR;

    FPDF_InitLibrary();

    Tcl_Eval(interp, "namespace eval pdfium {}");

    Tcl_CreateObjCommand(interp, "pdfium::open",
                         PdfiumOpenCmd,       NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::close",
                         PdfiumCloseCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::pagecount",
                         PdfiumPageCountCmd,  NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::render",
                         PdfiumRenderCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::gettext",
                         PdfiumGetTextCmd,    NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::pagesize",
                         PdfiumPageSizeCmd,   NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::meta",
                         PdfiumMetaCmd,       NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::rotation",
                         PdfiumRotationCmd,   NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::search",
                         PdfiumSearchCmd,     NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::links",
                         PdfiumLinksCmd,      NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::bookmarks",
                         PdfiumBookmarksCmd,  NULL, NULL);
    Tcl_CreateObjCommand(interp, "pdfium::formfields",
                         PdfiumFormFieldsCmd, NULL, NULL);

    Tcl_PkgProvide(interp, "pdfiumtcl", "0.3");
    return TCL_OK;
}
