/*
 * WmIrix.c: IRIX 4Dwm-style window frames, drawn when irixStyle is set.
 *
 * The look, as measured on IRIX 6.5's 4Dwm:
 *
 *  - a one pixel black outline round the whole window;
 *  - the border a raised ring: on its outer edge a light line and a
 *    light/base checkerboard top and left, a dark line and a dark/base
 *    checkerboard bottom and right; on its inner edge the same, the other
 *    way round; base colour between;
 *  - a black line round the title bar and the client, and between them;
 *  - each title bar part (window menu button, title, minimize, maximize)
 *    raised the same way, sunken while pressed;
 *  - the glyphs drawn as black outlines with a black drop shadow: a long
 *    bar, a small box and a tall box;
 *  - short notches across the border where the resize corners begin.
 *
 * Copyright (c) 2026 atomchild411.
 * This file is part of emwm and is distributed under the terms of the GNU
 * Lesser General Public License, version 2.1 or later (see COPYING).
 */

#include "WmGlobal.h"
#include "WmCDInfo.h"
#include "WmCDecor.h"
#include "WmIrix.h"

/* Light, base, dark and black, for the frame being drawn. */
typedef struct {
    unsigned long light, base, dark, black;
} IrixColors;

static GC irixGC;
static Pixmap irixChecker;
static Display *irixDisplay;

/* One GC and a 2x2 checkerboard stipple, made on first use. */
static GC
IrixGetGC (ClientData *pcd)
{
    static unsigned char checker_bits[] = { 0x01, 0x02 };

    if (irixGC == NULL || irixDisplay != DISPLAY) {
	irixDisplay = DISPLAY;
	irixGC = XCreateGC (DISPLAY, pcd->pSD->rootWindow, 0, NULL);
	irixChecker = XCreateBitmapFromData (DISPLAY, pcd->pSD->rootWindow,
					     (char *) checker_bits, 2, 2);
	XSetStipple (DISPLAY, irixGC, irixChecker);
	XSetTSOrigin (DISPLAY, irixGC, 0, 0);
    }
    return irixGC;
}

static void
IrixGetColors (ClientData *pcd, Boolean title, IrixColors *c)
{
    AppearanceData *ap = (title && DECOUPLE_TITLE_APPEARANCE(pcd)) ?
	&CLIENT_TITLE_APPEARANCE(pcd) : &CLIENT_APPEARANCE(pcd);

    if (pcd == wmGD.keyboardFocus) {
	c->light = ap->activeTopShadowColor;
	c->base = ap->activeBackground;
	c->dark = ap->activeBottomShadowColor;
    }
    else {
	c->light = ap->topShadowColor;
	c->base = ap->background;
	c->dark = ap->bottomShadowColor;
    }
    c->black = BlackPixel (DISPLAY, pcd->pSD->screen);
}

static void
Fill (Window win, GC gc, unsigned long pixel, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
	return;
    XSetFillStyle (DISPLAY, gc, FillSolid);
    XSetForeground (DISPLAY, gc, pixel);
    XFillRectangle (DISPLAY, win, gc, x, y, (unsigned) w, (unsigned) h);
}

/* Half the pixels a, half b, in a checkerboard. */
static void
Dither (Window win, GC gc, unsigned long a, unsigned long b,
	int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
	return;
    Fill (win, gc, a, x, y, w, h);
    XSetForeground (DISPLAY, gc, b);
    XSetFillStyle (DISPLAY, gc, FillStippled);
    XFillRectangle (DISPLAY, win, gc, x, y, (unsigned) w, (unsigned) h);
    XSetFillStyle (DISPLAY, gc, FillSolid);
}

/*
 * The two pixel edge of a bevel round x, y, w, h: on the top and left a
 * solid line in tl and a tl/base checkerboard inside it, on the bottom and
 * right the same in br. Corners go to the bottom-right colour, as on IRIX.
 */
static void
Edge (Window win, GC gc, IrixColors *c, unsigned long tl, unsigned long br,
      int x, int y, int w, int h)
{
    if (w < 4 || h < 4)
	return;
    Fill (win, gc, tl, x, y, w - 1, 1);			/* top */
    Fill (win, gc, tl, x, y, 1, h - 1);			/* left */
    Dither (win, gc, c->base, tl, x + 1, y + 1, w - 3, 1);
    Dither (win, gc, c->base, tl, x + 1, y + 1, 1, h - 3);
    Fill (win, gc, br, x, y + h - 1, w, 1);		/* bottom */
    Fill (win, gc, br, x + w - 1, y, 1, h);		/* right */
    Dither (win, gc, c->base, br, x + 1, y + h - 2, w - 2, 1);
    Dither (win, gc, c->base, br, x + w - 2, y + 1, 1, h - 2);
}

/*
 * The edge round a hole in the border (where the title bar and client
 * sit): solid lines on the perimeter of x, y, w, h, top and left in tl,
 * bottom and right in br, with checkerboards just outside them.
 */
static void
InnerEdge (Window win, GC gc, IrixColors *c, unsigned long tl,
	   unsigned long br, int x, int y, int w, int h)
{
    if (w < 2 || h < 2)
	return;
    Dither (win, gc, c->base, tl, x - 1, y - 1, w + 1, 1);
    Dither (win, gc, c->base, tl, x - 1, y - 1, 1, h + 1);
    Dither (win, gc, c->base, br, x, y + h, w + 1, 1);
    Dither (win, gc, c->base, br, x + w, y, 1, h + 1);
    Fill (win, gc, tl, x, y, w - 1, 1);
    Fill (win, gc, tl, x, y, 1, h - 1);
    Fill (win, gc, br, x, y + h - 1, w, 1);
    Fill (win, gc, br, x + w - 1, y, 1, h);
}

/* A box raised (or sunken) out of the base colour. */
static void
Raised (Window win, GC gc, IrixColors *c, Boolean sunken,
	int x, int y, int w, int h)
{
    Fill (win, gc, c->base, x, y, w, h);
    if (sunken)
	Edge (win, gc, c, c->dark, c->light, x, y, w, h);
    else
	Edge (win, gc, c, c->light, c->dark, x, y, w, h);
}

/* A glyph: a black outline, base inside, and a black drop shadow. */
static void
Glyph (Window win, GC gc, IrixColors *c, int x, int y, int w, int h)
{
    if (w < 3 || h < 3)
	return;
    XSetFillStyle (DISPLAY, gc, FillSolid);
    XSetForeground (DISPLAY, gc, c->black);
    XDrawRectangle (DISPLAY, win, gc, x + 1, y + 1,
		    (unsigned) w - 1, (unsigned) h - 1);	/* shadow */
    XDrawRectangle (DISPLAY, win, gc, x, y,
		    (unsigned) w - 1, (unsigned) h - 1);	/* outline */
    Fill (win, gc, c->base, x + 1, y + 1, w - 2, h - 2);
}

/*
 * One title bar part, at x, y, w, h in win: its bevel and its glyph.
 * Pressed parts are sunken and their glyphs move down and right.
 */
static void
DrawGadget (ClientData *pcd, Window win, GC gc, IrixColors *c, int gadget,
	    Boolean pressed, int x, int y, int w, int h)
{
    int gw, gh, s = pressed ? 1 : 0;

    Raised (win, gc, c, pressed, x, y, w, h);
    switch (gadget) {
	case FRAME_SYSTEM:		/* a long, flat bar */
	    gw = w - 8;
	    gh = h >= 16 ? 5 : 4;
	    Glyph (win, gc, c, x + 4 + s, y + (h - gh) / 2 + s, gw, gh);
	    break;
	case FRAME_MINIMIZE:		/* a small box */
	    gw = gh = 4;
	    Glyph (win, gc, c, x + (w - gw) / 2 + s, y + (h - gh) / 2 + s,
		   gw, gh);
	    break;
	case FRAME_MAXIMIZE:		/* a tall box */
	    gh = h - 8;
	    gw = (gh * 5) / 6;
	    Glyph (win, gc, c, x + (w - gw) / 2 + s, y + 4 + s, gw, gh);
	    break;
	default:
	    break;
    }
}

/* The title bar part's box, in the coordinates of win. */
static Boolean
GadgetBox (ClientData *pcd, int gadget, int *x, int *y,
	   unsigned int *w, unsigned int *h)
{
    if (!GetFramePartInfo (pcd, gadget, x, y, w, h))
	return False;
    if (DECOUPLE_TITLE_APPEARANCE(pcd)) {
	*x -= (int) pcd->frameInfo.upperBorderWidth;
	*y -= (int) pcd->frameInfo.upperBorderWidth;
    }
    /* The bottom row is the black line between title and client. */
    if (*h > 1)
	*h -= 1;
    return True;
}

static void
DrawTitleBar (ClientData *pcd, GC gc)
{
    static int gadgets[] = {
	FRAME_SYSTEM, FRAME_TITLE, FRAME_MINIMIZE, FRAME_MAXIMIZE
    };
    static unsigned long pressedFlags[] = {
	SYSTEM_DEPRESSED, TITLE_DEPRESSED, MINIMIZE_DEPRESSED,
	MAXIMIZE_DEPRESSED
    };
    Window win = DECOUPLE_TITLE_APPEARANCE(pcd) ?
	pcd->clientTitleWin : pcd->clientFrameWin;
    IrixColors c;
    int i, x, y;
    unsigned int w, h;

    IrixGetColors (pcd, True, &c);
    if (GetFramePartInfo (pcd, FRAME_TITLEBAR, &x, &y, &w, &h)) {
	if (DECOUPLE_TITLE_APPEARANCE(pcd))
	    x = y = 0;
	Fill (win, gc, c.black, x, y + (int) h - 1, (int) w, 1);
    }
    for (i = 0; i < 4; i++) {
	if (GadgetBox (pcd, gadgets[i], &x, &y, &w, &h))
	    DrawGadget (pcd, win, gc, &c, gadgets[i],
			(pcd->decorFlags & pressedFlags[i]) != 0,
			x, y, (int) w, (int) h);
    }
}

/* Short notches across a border band where the resize corners begin. */
static void
DrawNotches (ClientData *pcd, Window win, GC gc, IrixColors *c,
	     int W, int H, int b)
{
    int cw = (int) pcd->frameInfo.cornerWidth;
    int ch = (int) pcd->frameInfo.cornerHeight;
    int t = (int) pcd->frameInfo.upperBorderWidth;

    if (cw <= b + 2 || ch <= b + 2 || W < 2 * cw || H < 2 * ch)
	return;
    /* across the top and bottom bands */
    Fill (win, gc, c->dark, cw - 1, 1, 1, t - 2);
    Fill (win, gc, c->light, cw, 1, 1, t - 2);
    Fill (win, gc, c->dark, W - cw - 1, 1, 1, t - 2);
    Fill (win, gc, c->light, W - cw, 1, 1, t - 2);
    Fill (win, gc, c->dark, cw - 1, H - b + 1, 1, b - 2);
    Fill (win, gc, c->light, cw, H - b + 1, 1, b - 2);
    Fill (win, gc, c->dark, W - cw - 1, H - b + 1, 1, b - 2);
    Fill (win, gc, c->light, W - cw, H - b + 1, 1, b - 2);
    /* across the left and right bands */
    Fill (win, gc, c->dark, 1, ch - 1, b - 2, 1);
    Fill (win, gc, c->light, 1, ch, b - 2, 1);
    Fill (win, gc, c->dark, 1, H - ch - 1, b - 2, 1);
    Fill (win, gc, c->light, 1, H - ch, b - 2, 1);
    Fill (win, gc, c->dark, W - b + 1, ch - 1, b - 2, 1);
    Fill (win, gc, c->light, W - b + 1, ch, b - 2, 1);
    Fill (win, gc, c->dark, W - b + 1, H - ch - 1, b - 2, 1);
    Fill (win, gc, c->light, W - b + 1, H - ch, b - 2, 1);
}

void
IrixDrawFrame (ClientData *pcd)
{
    GC gc = IrixGetGC (pcd);
    Window win = pcd->clientFrameWin;
    IrixColors c;
    int W = (int) pcd->frameInfo.width, H = (int) pcd->frameInfo.height;
    int t = (int) pcd->frameInfo.upperBorderWidth;
    int b = (int) pcd->frameInfo.lowerBorderWidth;

    IrixGetColors (pcd, False, &c);
    if ((pcd->decor & (MWM_DECOR_RESIZEH | MWM_DECOR_BORDER)) && b >= 4) {
	/* the ring: base, then the black outline */
	Fill (win, gc, c.base, 0, 0, W, t);
	Fill (win, gc, c.base, 0, 0, b, H);
	Fill (win, gc, c.base, W - b, 0, b, H);
	Fill (win, gc, c.base, 0, H - b, W, b);
	XSetForeground (DISPLAY, gc, c.black);
	XDrawRectangle (DISPLAY, win, gc, 0, 0,
			(unsigned) W - 1, (unsigned) H - 1);
	/* its outer edge raised, its inner edge sunken */
	Edge (win, gc, &c, c.light, c.dark, 1, 1, W - 2, H - 2);
	InnerEdge (win, gc, &c, c.dark, c.light, b - 2, t - 2,
		   W - 2 * b + 4, H - t - b + 4);
	/* the black line round the title bar and the client */
	XSetForeground (DISPLAY, gc, c.black);
	XDrawRectangle (DISPLAY, win, gc, b - 1, t - 1,
			(unsigned) (W - 2 * b + 1), (unsigned) (H - t - b + 1));
	if (pcd->decor & MWM_DECOR_RESIZEH)
	    DrawNotches (pcd, win, gc, &c, W, H, b);
    }
    if (pcd->decor & MWM_DECOR_TITLE)
	DrawTitleBar (pcd, gc);
}

Boolean
IrixDepressGadget (ClientData *pcd, int gadget, Boolean depressed)
{
    GC gc = IrixGetGC (pcd);
    Window win = DECOUPLE_TITLE_APPEARANCE(pcd) ?
	pcd->clientTitleWin : pcd->clientFrameWin;
    IrixColors c;
    int x, y;
    unsigned int w, h;

    switch (gadget) {
	case FRAME_TITLE:
	case FRAME_SYSTEM:
	case FRAME_MINIMIZE:
	case FRAME_MAXIMIZE:
	    break;
	default:
	    return False;
    }
    if (!GadgetBox (pcd, gadget, &x, &y, &w, &h))
	return False;
    IrixGetColors (pcd, True, &c);
    DrawGadget (pcd, win, gc, &c, gadget, depressed, x, y, (int) w, (int) h);
    if (gadget == FRAME_TITLE)
	DrawWindowTitle (pcd, False);
    return True;
}
