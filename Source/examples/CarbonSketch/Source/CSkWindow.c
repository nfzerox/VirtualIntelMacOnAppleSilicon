/*
    File:       CSkWindow.c
        
    Contains:	Implementation of CSkWindow creation and event handling.
				
    Disclaimer:	IMPORTANT:  This Apple software is supplied to you by Apple Computer, Inc.
                ("Apple") in consideration of your agreement to the following terms, and your
                use, installation, modification or redistribution of this Apple software
                constitutes acceptance of these terms.  If you do not agree with these terms,
                please do not use, install, modify or redistribute this Apple software.

                In consideration of your agreement to abide by the following terms, and subject
                to these terms, Apple grants you a personal, non-exclusive license, under Apple’s
                copyrights in this original Apple software (the "Apple Software"), to use,
                reproduce, modify and redistribute the Apple Software, with or without
                modifications, in source and/or binary forms; provided that if you redistribute
                the Apple Software in its entirety and without modifications, you must retain
                this notice and the following text and disclaimers in all such redistributions of
                the Apple Software.  Neither the name, trademarks, service marks or logos of
                Apple Computer, Inc. may be used to endorse or promote products derived from the
                Apple Software without specific prior written permission from Apple.  Except as
                expressly stated in this notice, no other rights or licenses, express or implied,
                are granted by Apple herein, including but not limited to any patent rights that
                may be infringed by your derivative works or by other works in which the Apple
                Software may be incorporated.

                The Apple Software is provided by Apple on an "AS IS" basis.  APPLE MAKES NO
                WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED
                WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
                PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE OR IN
                COMBINATION WITH YOUR PRODUCTS.

                IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR
                CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
                GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
                ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION
                OF THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT
                (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN
                ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    Copyright © 2004 Apple Computer, Inc., All Rights Reserved
*/

#include "CSkWindow.h"
#include "CSkDocStorage.h"
#include "CSkConstants.h"
#include "CSkUtils.h"
#include "CSkObjects.h"
#include "CSkToolPalette.h"
#include "CSkPrinting.h"
#include "NavServicesHandling.h"
#include <math.h>
#include <stdio.h>

// The starter artwork uses ordinary document objects, not a background image:
// every circle and line can be selected, moved, recolored, copied, or deleted.
static CSkObjectPtr AddStarterShape(DocStorage* document, int type, CGRect bounds,
                                  CGrgba fill, CGrgba stroke, float lineWidth)
{
    CSkObjectPtr object = CreateDrawObj(document->toolPalette, type);
    if (object == NULL)
        return NULL;

    DrawObjList selection = { object, object };
    if (type != kLineShape)
        CSkShapeSetBounds(GetCSkObjectShape(object), bounds);
    SetDrawObjSelectState(object, true);
    SetFillColorOfSelecteds(&selection, &fill);
    SetStrokeColorOfSelecteds(&selection, &stroke);
    SetLineWidthOfSelecteds(&selection, lineWidth);
    SetLineStyleOfSelecteds(&selection, kStyleSolid);
    SetDrawObjSelectState(object, false);
    AddDrawObjToList(&document->objList, object);
    return object;
}

static void CreateStarterDrawing(DocStorage* document)
{
    CFAbsoluteTime started = CFAbsoluteTimeGetCurrent();
    fprintf(stderr, "\nCarbonSketch | %u-bit Intel | Carbon + Quartz 2D\n",
            (unsigned)(8 * sizeof(void*)));
    fprintf(stderr, "[Canvas] %d x %d points, resolution-independent vector artwork\n",
            document->docSize.h, document->docSize.v);
    fprintf(stderr, "[Palette] Lagoon / Azure / Violet / Coral / Tangerine / Gold\n");
    fprintf(stderr, "[Geometry] Arranging 24 translucent petal circles around an orbit\n");
    const CGrgba clear = { 0, 0, 0, 0 };
    const CGrgba paper = { 0.98, 0.97, 0.93, 1 };
    const CGrgba ink = { 0.10, 0.17, 0.27, 1 };
    const CGrgba colors[] = {
        { 0.05, 0.60, 0.64, 1 }, { 0.16, 0.43, 0.84, 1 },
        { 0.52, 0.35, 0.75, 1 }, { 0.91, 0.34, 0.40, 1 },
        { 0.96, 0.57, 0.23, 1 }, { 0.85, 0.68, 0.20, 1 }
    };
    const CGPoint center = { 288, 470 };

    AddStarterShape(document, kRectShape, CGRectMake(24, 236, 528, 456),
                    paper, clear, 0);

    // A six-color rosette: overlapping translucent circles demonstrate Quartz
    // compositing while remaining real, editable CarbonSketch vector shapes.
    for (int index = 0; index < 24; ++index)
    {
        double angle = index * (2.0 * M_PI / 24.0);
        CGrgba stroke = colors[index / 4];
        CGrgba fill = stroke;
        fill.a = 0.055;
        stroke.a = 0.78;
        AddStarterShape(document, kOvalShape,
            CGRectMake(center.x + 82 * cos(angle) - 90,
                       center.y + 82 * sin(angle) - 90, 180, 180),
            fill, stroke, 1.6);
    }

    // Fine spokes connect two offset orbits, making an intricate woven center.
    fprintf(stderr, "[Geometry] Weaving 48 spokes between two offset circular paths\n");
    for (int index = 0; index < 48; ++index)
    {
        double angle = index * (2.0 * M_PI / 48.0);
        CGrgba color = colors[index / 8];
        color.a = 0.50;
        CSkObjectPtr line = AddStarterShape(document, kLineShape, CGRectZero,
                                            clear, color, 0.8);
        if (line != NULL)
            CSkShapeSetLine(GetCSkObjectShape(line),
                CGPointMake(center.x + 160 * cos(angle), center.y + 160 * sin(angle)),
                CGPointMake(center.x + 54 * cos(angle + 1.3), center.y + 54 * sin(angle + 1.3)));
    }

    AddStarterShape(document, kOvalShape, CGRectMake(265, 447, 46, 46),
                    ink, clear, 0);
    AddStarterShape(document, kOvalShape, CGRectMake(279, 461, 18, 18),
                    paper, clear, 0);

    // A small palette key finishes the composition below the rosette.
    for (int index = 0; index < 6; ++index)
        AddStarterShape(document, kOvalShape, CGRectMake(177 + 38 * index, 262, 20, 20),
                        colors[index], clear, 0);

    fprintf(stderr, "[Artwork] Starter geometry generated in %.2f ms\n",
            1000.0 * (CFAbsoluteTimeGetCurrent() - started));
    fprintf(stderr, "[Explore] Every circle and line is editable. Select, move, recolor!\n");
}


/* ------------------- Coordinate Juggling ---------------------

Let's say we want to present the drawing area (a "document page") on some background
(as in AppleWorks, Word etc.). The window has size wWidth x wHeight, and scrolls around on 
the background, provided 2 x hMargin + docWidth is bigger than wWidth, or wHeight is bigger
than 2 * vMargin + docHeight. If it scrolls around, scrollPosition (relative to the 
background origin, which is at (-kTopMargin, -kLeftMargin) relativ to the document topLeft)
describes the topLeft of the window. The horizontal scrolling range is then 
2 x hMargin + docWidth - wWidth, the vertical range is 2 x vMargin + docHeight - wHeight.
However, we also want the support a scaling factor for the document content. So, all dimensions
above except wWidth and wHeight need to be scaled by the current scaling factor!

Our drawing objects are described in documentpage-relativ coordinates, with the origin of
the CGContext at the bottom-left of the page. 
To draw an object into the windowContext (which has the origin reset via SyncCGContextOriginWithPort, 
included in QDBeginCGContext), we need to do the following:
- CGContextTranslateCTM(scaledCGDocOrigin)  // bottom-left of scaled document, relativ to bottom-left of window
- CGContextScaleCTM(curScaling)
where scaledCGDocOrigin is computed as follows:
  x = docTopLeft.h * scale - scrollPosition.h
  y = (docTopLeft.v + docSize) * scale - (scrollPosition.v + wHeight)

To convert mouse-tracking (hit-testing) window-topleft-relative QD coordinates into cgDocument coordinates, 
we need to
- add scrollPosition (now the QD coordinates are relative to our global scroll area)
- subtract the (scaled) docOrigin point (still in QD coordintes, now docOrigin-relative). 
- scale by 1/(scaling factor)
- convert to cg coordinates: (v, h) -> (h, docHeight - v), where docHeight is unscaled.

-------------------------------------------------------------- */


//----------------------------------------------------------------------------------------------------------------
// Adjust horizontal and vertical scroll bar maximums; set control minimum and maximum of the "Scale" popup.
// Needs to get called at window creation, window resizing and scale change.

static void SetupControls(WindowRef window, DocStorage* docStP)
{
    ControlID   cntlID  = { kControlSignatureMainWindow, kScrollVerticalID };
    ControlRef  control;
    SInt16		cntlMax;
	
    GetControlByID(window, &cntlID, &control);
    cntlMax = docStP->scale * (docStP->docSize.v + 2 * docStP->docTopLeft.v) - docStP->windowRect.bottom;
    if (cntlMax < 0)
        cntlMax = 0;
    SetControlMaximum(control, cntlMax);
	
    cntlID.id = kScrollHorizontalID;
    GetControlByID(window, &cntlID, &control);
    cntlMax = docStP->scale * (docStP->docSize.h + 2 * docStP->docTopLeft.h) - docStP->windowRect.right;
    if (cntlMax < 0)	// is this built into SetControlMaximum when 0 = cntlMin ?
        cntlMax = 0;
    SetControlMaximum(control, cntlMax);
	
	cntlID.id = kScalePopupID;
    GetControlByID(window, &cntlID, &control);
	SetControlMinimum(control, 0);
	SetControlMaximum(control, kMaxScaleMenuItem);
}

//----------------------------------------------------------------------------------------------------------------
// Given a QDPoint in local window coordinates, return a CGPoint in document (drawObject) coordinates.
static CGPoint QDCoordsToCSkDocCGCoords(Point inPt, DocStorage* docStP)
{
    float   scale       = docStP->scale;
    Point   docTopLeft  = docStP->docTopLeft;
    float   x, y;
    
    AddPt(docStP->scrollPosition, &inPt);       // now on global scaled document background
    x = (float)inPt.h / scale;
    y = (float)inPt.v / scale;                  // now unscaled
    x -= docTopLeft.h;
    y -= docTopLeft.v;                          // document-topLeft relative
    return CGPointMake(x, docStP->docSize.v - y);  // document-bottomLeft relative
}

//----------------------------------------------------------------------------------------------------------------
// If either the window size, or the scroll position, or the scale changes, we need to recompute the displayCTM.
// The docTopLeft would only change in the case of a multi-page document.
static void RecalculateDisplayCTM(DocStorage* docStP)
{
    int     wHeight = docStP->windowRect.bottom - docStP->windowRect.top;
    CGPoint scaledCGDocOrigin;  // relative to the origin of the window context
    
    scaledCGDocOrigin.x = docStP->docTopLeft.h * docStP->scale - docStP->scrollPosition.h;
    scaledCGDocOrigin.y = (docStP->scrollPosition.v + wHeight) - (docStP->docTopLeft.v + docStP->docSize.v) * docStP->scale;
    
    // Create the CGAffineTransform docStP->displayCTM, which includes a translation by scaledCGDocOrigin 
	// and a scaling in x and y by docStP->scale in x and y. Each time we draw our content into a "fresh" CGContext,
	// we need to concatenate its CTM with this "displayCTM".
    docStP->displayCTM = CGAffineTransformMake(docStP->scale, 0, 0, docStP->scale, scaledCGDocOrigin.x, scaledCGDocOrigin.y);
}

//------------------------------------------------------------------------------------------------------------------
// For convenience, we create this "single pixel CGBitmapContext" for hit-testing right away at creation of a new
// document. Given the purpose of CarbonSketch, there is not much use of it without doing hit-testing, sooner or later.
// Note that we use the genericColorSpace for consistency, even though it doesn't make much technical sense, here.
static CGContextRef CreateSinglePixelBitmapContext()
{
    const int		width       = 1;
    const int		height      = 1;
    const int		size        = 4 * width * height;
    void*           data        = calloc(size, 1);
    CGColorSpaceRef genericColorSpace  = GetGenericRGBColorSpace();
    CGContextRef    bmCtx       = CGBitmapContextCreate(data, width, height, 8, 4 * width, genericColorSpace, kCGImageAlphaPremultipliedFirst);

	// The colorspace story here is mainly for consistency - genericColorSpace throughout!
	
    CGContextSetFillColorSpace(bmCtx, genericColorSpace); 
    CGContextSetStrokeColorSpace(bmCtx, genericColorSpace); 

	// After that, all colors passed to CGContextSetFillColor/CGContextSetStrokeColor are arrays
	// float[4] = { r, g, b, a }. In this app, we use the CGrgba type instead, for clarity.

    return bmCtx;
}

//--------------------------------------------------------------------------------------
// Allocation and initialization of document storage.
// Note that we rely on NewPtrClear setting everything else to NULL.

static DocStorage* CreateDocumentStorage(WindowRef window, WindowRef toolPalette)
{
    // default document position in window
    const int kLeftMargin   = 18;
    const int kTopMargin    = 18;
    const int kDocWidth     = 576;
    const int kDocHeight    = 720;
    const int kGridWidth    = 18;
    const Point nullPt      = { 0, 0 };
    
    DocStorage*  docStP = (DocStorage*) NewPtrClear( sizeof(DocStorage) );
    
    GetWindowPortBounds(window, &docStP->windowRect);
    docStP->ownerWindow         = window;
    docStP->bmCtx               = CreateSinglePixelBitmapContext();
    docStP->docTopLeft.v        = kTopMargin;
    docStP->docTopLeft.h        = kLeftMargin;
    docStP->docSize.v           = kDocHeight;
    docStP->docSize.h           = kDocWidth;
    docStP->gridWidth           = kGridWidth;
    docStP->scale               = 1.0;
    docStP->scrollPosition      = nullPt;
    docStP->toolPalette         = toolPalette;
    docStP->dupOffset           = CGPointMake(9.0, 9.0);
    docStP->pageFormat          = kPMNoPageFormat;
    docStP->printSettings       = kPMNoPrintSettings;
    RecalculateDisplayCTM(docStP);
    return docStP;
}

//--------------------------------------------------------------------------------------
// Make all the necessary "..Release" calls, and deallocate any nested storage
void ReleaseDocumentStorage(DocStorage* docStP)
{
    ReleaseDrawObjList(&docStP->objList);
    
    if (docStP->bmCtx != NULL)
        CGContextRelease(docStP->bmCtx);
     // Don't dispose of toolPalette!
	 if (docStP->pdfData != NULL)
		CFRelease(docStP->pdfData);
    if (docStP->pageFormat != kPMNoPageFormat)
        (void)PMRelease(docStP->pageFormat);
    if (docStP->printSettings != kPMNoPrintSettings)
        (void)PMRelease(docStP->printSettings);
    if (docStP->flattenedPageFormat != NULL)
        DisposeHandle(docStP->flattenedPageFormat);
}

//--------------------------------------------------------------------------------------
// If the srcRect doesn't fit into dstRect, scale the CTM such that it fits,
// and translate it such that the srcRect topLeft gets mapped into the topleft of the dstRect.
// We can assume that srcRect and dstRect have their origin at (0, 0).
// For clarity, only pass in the srcRect.size and the dstRect.size.
// This is only used to draw an external PDF into the document page.
static void SetupTransform(CGContextRef ctx, CGSize srcSize, CGSize dstSize)
{
	float scaleX = dstSize.width / srcSize.width;
	float scaleY = dstSize.height / srcSize.height;
	float scale  = (scaleX < scaleY ? scaleX : scaleY);

	// Do nothing if srcRect fits entirely into dstRect
	if (scale < 1.0)
	{
		CGContextScaleCTM(ctx, scale, scale); 
	}
	// Always put the drawing into the destination's topLeft. Assume srcRect and dstRect have origin (0, 0)
	CGContextTranslateCTM(ctx, dstSize.width - scale * srcSize.width, 
							dstSize.height - scale * srcSize.height);
}


//--------------------------------------------------------------------------------------
// Draw page 1 of pdfData into the dstRect, scaling it (if necessary) to fit.
static void DrawPDFData(CGContextRef ctx, CFDataRef pdfData, CGRect dstRect)
{
	CGDataProviderRef	provider;
	CGPDFDocumentRef	document;
	CGPDFPageRef		page;
	CGRect				pageSize;
	
	provider = CGDataProviderCreateWithData(NULL, CFDataGetBytePtr(pdfData), CFDataGetLength(pdfData), NULL);
	document = CGPDFDocumentCreateWithProvider(provider);
	
	page = CGPDFDocumentGetPage(document, 1);			// always page 1 only
	pageSize = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);

    CGContextSaveGState(ctx);							// save GState because we are going to change the CTM
	SetupTransform(ctx, pageSize.size, dstRect.size);	// Scale into dstRect, if the page is too big
	CGContextDrawPDFPage(ctx, page);					// Just what it says it does ...
	CGContextRestoreGState(ctx);						// restore to whatever the previous CTM was

	CFRelease(document);	// we created it
	CFRelease(provider);	// we created it
}

//--------------------------------------------------------------------------------------
// Clip the drawing to the WindowPortRect minus the area covered by our controls.
// Create a path that contains all the rectangles, and use CGContextEOClip (even-odd)
// rather than CGContextClip (winding number), so the contained rectangles get
// substracted from the big cgWinRect rectangle for the clip path.
// We are working in default CGContext user coordinates; so we need the window height
// to convert the local QD coordinates of the ControlBounds rectangles to CG coordinates.

static void ClipToContentArea(CGContextRef ctx, WindowRef window, CGRect windowPortRect, int wHeight)
{
    ControlID cntlID = { kControlSignatureMainWindow, kScrollHorizontalID };
	ControlRef	control;
	Rect		r;
	
	CGContextBeginPath(ctx);				// clear the context's current path
	CGContextAddRect(ctx, windowPortRect);	// add rectangular windowPortRect path
	
    // Subtract areas of controls. Because we will use the "even-odd" rule for the clip,
	// it's enough to just add the rectangles to be subtracted. Otherwise, we would have
	// to add the contour of the rectangles one side after the other in counter-clockwise order.
	
	// cntlID.id = kScrollHorizontalID;	-- set above
    GetControlByID(window, &cntlID, &control);
	GetControlBounds(control, &r);
	CGContextAddRect(ctx, CGRectMake(r.left, wHeight - r.bottom, r.right - r.left, r.bottom - r.top));
	
	cntlID.id = kScrollVerticalID;
    GetControlByID(window, &cntlID, &control);
	GetControlBounds(control, &r);
	CGContextAddRect(ctx, CGRectMake(r.left, wHeight - r.bottom, r.right - r.left, r.bottom - r.top));

    cntlID.id = kScalePopupID;
    GetControlByID(window, &cntlID, &control);
	GetControlBounds(control, &r);
	CGContextAddRect(ctx, CGRectMake(r.left, wHeight - r.bottom, r.right - r.left, r.bottom - r.top));

	CGContextEOClip(ctx);	// Applies the "even-odd" rule for using the above path (a set of rectangles) as clip
}

//--------------------------------------------------------------------------------------
static void DrawDocumentBackgroundGrid(CGContextRef ctx, Point docSize, float gridWidth)
// Draw document background grid
{
    const CGrgba gridColor = { 0.8, 0.9, 0.8, 1.0 };	// used as float[4] in CGContextSetStrokeColor
	float t;
	
	CGContextSetStrokeColor(ctx, (float*)&gridColor);
	CGContextSetLineWidth(ctx, 0.5);
	
	t = 0.5;
	while (t < docSize.h)
	{
		CGContextMoveToPoint(ctx, t, 0.5);
		CGContextAddLineToPoint(ctx, t, docSize.v);
		t += gridWidth;
	}
	t = 0.5;
	while (t < docSize.v)
	{
		CGContextMoveToPoint(ctx, 0.5, t);
		CGContextAddLineToPoint(ctx, docSize.h, t);
		t += gridWidth;
	}
	CGContextStrokePath(ctx);
}

//--------------------------------------------------------------------------------------
static void DrawWindowContent(WindowRef window)
{
    DocStorage*		docStP      		= (DocStorage*)GetWRefCon( window );
    const CGrgba 	white 				= { 1.0, 1.0, 1.0, 1.0 };	// used as float[4] in CGContextSetFillColor
    const CGrgba 	docBackgroundColor 	= { 0.5, 0.5, 0.5, 1.0 };
    CGColorSpaceRef genericColorSpace 	= GetGenericRGBColorSpace();
	int				wHeight				= docStP->windowRect.bottom - docStP->windowRect.top;
    CGRect			cgWinRect			= CGRectMake(0, 0, docStP->windowRect.right - docStP->windowRect.left, wHeight);
    CGContextRef	ctx;
    
    QDBeginCGContext(GetWindowPort(window), &ctx);
	
	// This CGContextSaveGState/CGContextRestoreGState bracket here is pointless,
	// because we have this ctx only inside of QDBeginCGContext/QDEndCGContext, anyway.
	// But if we decide in the future to hang on to this ctx (in DocStorage), rather
	// than calling QDBeginCGContext/QDEndCGContext all the time, then we will need it.
	
	CGContextSaveGState(ctx);	// save GState before we clip
	
	// Clip all Quartz drawing to the window content area (e.g. don't overwrite scrollbars).
	// Pass in the cgWinRect in the default CGContext user coordinates, and pass in
	// the window height to convert local controlBounds rectangles to CG coordintes.
	ClipToContentArea(ctx, window, cgWinRect, wHeight);
	
    // ensure that we are drawing in the correct color space
    CGContextSetFillColorSpace(ctx, genericColorSpace); 
    CGContextSetStrokeColorSpace(ctx, genericColorSpace); 

    CGContextSetFillColor(ctx, (float*)&docBackgroundColor);
    CGContextFillRect(ctx, cgWinRect);                  // paint background around document

    CGContextConcatCTM(ctx, docStP->displayCTM);		// concatenate with our own transformation matrix
														// (see RecalculateDisplayCTM() )
    // also clip to document page, now
    CGContextClipToRect(ctx, CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v));

    // From now on, all drawing is to be done in "document page coordinates"
    
    CGContextSetFillColor(ctx, (float*)&white);
    CGContextFillRect(ctx, CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v ));  // paint white background

	// If we have a background pdf, draw it
	if (docStP->pdfData != NULL)
	{
		DrawPDFData(ctx, docStP->pdfData, CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v));
	}
	else
	{
		DrawDocumentBackgroundGrid(ctx, docStP->docSize, docStP->gridWidth);
	}
	
    RenderDrawObjList(ctx, &docStP->objList, true);
	
	CGContextRestoreGState(ctx);	// Reset the GState to what it was on entry
	
	// The rule is: Always call CGContextSynchronize (or CGContextFlush) before QDEndCGContext.
	CGContextSynchronize(ctx);	
																		
    QDEndCGContext(GetWindowPort(window), &ctx);
}   // DrawWindowContent


//--------------------------------------------------------------------------------------
// Only go into "dragSelection" mode if points a and b are at least "so many" pixels apart.
// (This is questionable ...)
static Boolean BigEnoughForDragSelection(Point a, Point b)
{
    int dv = b.v - a.v;
	int dh = b.h - a.h;
	
	if (dv < 0)	dv = -dv;
	if (dh < 0) dh = -dh;
	
	return (dv > 1) && (dh > 1);
}

//--------------------------------------------------------------------------------------
// We support "shiftKeyDown" to constrain shape creation to
// a) only horizontal, vertical or diagonal lines
// b) only square rectangles and ovals.

static void AdjustEndPoint(Point* ioPt, Point beginPt, int selectedShape, UInt32 modifiers)
{
    Boolean shiftKeyDown = ((modifiers & shiftKey) != 0);

    if ( shiftKeyDown )
    {
        int dh = ioPt->h - beginPt.h;
        int dv = ioPt->v - beginPt.v;
		int sign = (dh * dv >= 0 ? 1 : -1);
	
        if (selectedShape == kLineShape)  // allow horizontal, vertical and diagonal only
        {
            int absDH = (dh > 0 ? dh : -dh);
            int absDV = (dv > 0 ? dv : -dv);
            if (absDH < absDV/4)
            {
                ioPt->h = beginPt.h;
            }
            else if (absDV < absDH/4)
            {
                ioPt->v = beginPt.v;
            }
            else    // diagonal
            {
                ioPt->v = beginPt.v + sign * dh;
                ioPt->h = beginPt.h + dh;
            }
        }
        else    // make it square
        {
            ioPt->v = beginPt.v + dh;
            ioPt->h = beginPt.h + dh;
        }
    }
}


//-----------------------------------------------------------------------------------
// DoMouseArrowTracking (for resizing or moving selected objects) and
// DoMouseToolTracking (for creating new objects) are rather messy. Will try to
// find a better approach later (when somebody gives me good suggestions ...).

static void DoMouseArrowTracking( WindowRef window, CGContextRef overlayContext,
				     Point beginPt, UInt32 evtModifiers )
{
    const float     dashLengths[2]  = { 3, 3 };     // for drawing "marching ants"
    static float    phase           = 1.0;          // (make them march)

    enum {
        dragSelect  = 1,
        dragMove    = 2,
        dragResize  = 3
    };
    
    int                 dragState       = dragSelect;
    OSStatus            err;
    MouseTrackingResult	trackingResult;
    Point               endPt;
    CGPoint             cgStartPt;
    CGPoint             windowContextPt;
    DocStorage*         docStP          = (DocStorage*)GetWRefCon( window );
    CGRect              docRect         = CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v);
    CSkObjectPtr		tempObj         = NULL;
    CSkObjectPtr		hitObj          = NULL;
    Boolean             beenDragging    = false;
    Boolean             isStillDown     = true;
    Boolean             shiftKeyDown    = ((evtModifiers & shiftKey) != 0);
    UInt32              grabNum;
    
    cgStartPt = QDCoordsToCSkDocCGCoords(beginPt, docStP);
    
    // First, check if we are down on a selected object. If so, switch to dragMove or dragResize.
    // If not, stay in noDrag, and go to dragSelect if dragging continues.

    windowContextPt = CGPointMake( beginPt.h, (docStP->windowRect.bottom - docStP->windowRect.top) - beginPt.v );
    hitObj = DrawObjListHitTesting(&docStP->objList, docStP->bmCtx, docStP->displayCTM, windowContextPt, cgStartPt, &grabNum);
    if ( (hitObj != NULL) && IsDrawObjSelected(hitObj) )
    {
        dragState = (grabNum > 0 ? dragResize : dragMove);
        if (dragState == dragResize)
        {
            tempObj = CopyDrawObject(hitObj);
            MakeDrawObjTransparent(tempObj, 0.4);
            SetContextStateForDrawObject(overlayContext, tempObj);
        }
    }
    
    while ( isStillDown ) 
    {
        UInt32      modifiers;
		CGPoint     cgEndPt;
        
        err = TrackMouseLocationWithOptions( GetWindowPort(window), 0, 0, &endPt, &modifiers, &trackingResult );
        
        windowContextPt = CGPointMake( endPt.h, (docStP->windowRect.bottom - docStP->windowRect.top) - endPt.v );
        cgEndPt = QDCoordsToCSkDocCGCoords(endPt, docStP);

        switch ( trackingResult )
        {
            case kMouseTrackingMouseDragged:                
                beenDragging = true;
                CGContextClearRect( overlayContext, docRect );    // "Erase" the overlay window (doc coordinates!)
                // Either draw selection rectangle with marching ants, or move the selected objects, or resize the selected object
                
                if (dragState == dragSelect)
                {
                    if ( BigEnoughForDragSelection(beginPt, endPt) )
                    {
                        CGRect selectionRect = CGRectMake(cgStartPt.x, cgStartPt.y, cgEndPt.x - cgStartPt.x, cgEndPt.y - cgStartPt.y);
                        CGContextSetLineDash(overlayContext, phase, dashLengths, 2);
                        CGContextStrokeRect(overlayContext, selectionRect);
                        phase += 1.0;   // will use a timer to keep the ants marching, later ...
			
						if (!shiftKeyDown)
							CSkObjListSetSelectState(&docStP->objList, false);
						CSkObjListSelectWithinRect(&docStP->objList, selectionRect);
                    }
					else
					{
						hitObj = DrawObjListHitTesting(&docStP->objList, docStP->bmCtx, docStP->displayCTM, windowContextPt, cgEndPt, NULL);
						
						if (hitObj != NULL)
						{
							Boolean wasSelected = IsDrawObjSelected(hitObj);
							
							if (shiftKeyDown)   // extend selection, or toggle selected state
							{
								SetDrawObjSelectState(hitObj, !wasSelected);
							}
							else if (!wasSelected)
							{
								CSkObjListSetSelectState(&docStP->objList, false);
								SetDrawObjSelectState(hitObj, true);
							}
						}
						else if (!shiftKeyDown)   // just deselect everything
						{
							CSkObjListSetSelectState(&docStP->objList, false);
						}
					}
                    DrawWindowContent(window);  // to reflect selection state changes
                }
                else if (dragState == dragMove)
                {
                    // redraw selected objects at offset cgEndPt - cgStartPt
                    RenderSelectedDrawObjs(overlayContext, &docStP->objList, cgEndPt.x - cgStartPt.x, cgEndPt.y - cgStartPt.y, 0.5);
                }
                else // if (dragState == dragResize)
                {
                    CSkShapeResize(GetCSkObjectShape(tempObj), &grabNum, cgEndPt);
                    RenderCSkObject(overlayContext, tempObj, true);
                }
                CGContextSynchronize(overlayContext);      //  Make sure we get our drawing to the screen
                break;

            case kMouseTrackingMouseUp:
                if (!beenDragging)          // there was no dragging, just a single click
                {
                    hitObj = DrawObjListHitTesting(&docStP->objList, docStP->bmCtx, docStP->displayCTM, windowContextPt, cgEndPt, NULL);
                            
                    if (shiftKeyDown == false)
                        CSkObjListSetSelectState(&docStP->objList,false);
                    
                    if (hitObj != NULL)
                    {
                        if (shiftKeyDown)
                            SetDrawObjSelectState(hitObj, !IsDrawObjSelected(hitObj));
                        else    
                            SetDrawObjSelectState(hitObj, true);
                    }
                }
                else
                {
                    if (dragState == dragMove)
                    {
                        MoveSelectedDrawObjs(&docStP->objList, cgEndPt.x - cgStartPt.x, cgEndPt.y - cgStartPt.y);
                    }
                    else if (dragState == dragResize)   // Move new shape geometry from tempObj to hitObj
		    		{
						memcpy( GetCSkObjectShape(hitObj), GetCSkObjectShape(tempObj), CSkShapeSize());
                    }
                }
                // fall through to isStillDown = false
                
            case kMouseTrackingUserCancelled:
                isStillDown = false;
                break;
        }
    }
    
    if ( (tempObj != NULL) )
    {
        ReleaseDrawObj(tempObj);
    }
    
    CGContextClearRect( overlayContext, docRect );
    
    // Make sure we always redraw the new state of the new object list
    DrawWindowContent(window); 

    SetThemeCursor( kThemeArrowCursor );
}   // DoMouseArrowTracking


//-----------------------------------------------------------------------------------
static void DoMouseToolTracking( WindowRef window, CGContextRef overlayContext,
				    int shapeSelect, Point beginPt )
{
    const CGrgba        trStrokeColor   = { 0.5, 0.5, 0.5, 0.4 };   // gray with alpha = 0.4
    const CGrgba        trFillColor     = { 0.9, 0.9, 0.9, 0.3 };   // ltgray with alpha = 0.3
    OSStatus            err;
    MouseTrackingResult	trackingResult;
    Point               endPt;
    CGPoint             cgStartPt;
    CGPoint             cgEndPt;
    CGRect              objBounds       = CGRectMake(0, 0, 0, 0);
    DocStorage*         docStP          = (DocStorage*)GetWRefCon( window );
    CGRect              docRect         = CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v);
    CSkObjectPtr		tempObj         = CreateDrawObj(docStP->toolPalette, shapeSelect);
    Boolean             beenDragging    = false;
    Boolean             isStillDown     = true;
    
    CSkToolPaletteSetContextState(overlayContext, docStP->toolPalette);
    CGContextSetStrokeColor( overlayContext, (float*)&trStrokeColor);
    CGContextSetFillColor( overlayContext, (float*)&trFillColor);
    SetThemeCursor( kThemeCrossCursor );
         
    cgStartPt = QDCoordsToCSkDocCGCoords(beginPt, docStP);
    
    while ( isStillDown ) 
    {
        UInt32      modifiers;
        
        err = TrackMouseLocationWithOptions( GetWindowPort(window), 0, 0, &endPt, &modifiers, &trackingResult );
        if (err != noErr)   // when would this occur?
            break;
        
	AdjustEndPoint(&endPt, beginPt, shapeSelect, modifiers);
		
        cgEndPt = QDCoordsToCSkDocCGCoords(endPt, docStP);

        switch ( trackingResult )
        {
            case kMouseTrackingMouseDragged:
                beenDragging = true;
                CGContextClearRect( overlayContext, docRect );    // "Erase" the overlay window (doc coordinates!)
                if (shapeSelect == kLineShape)
                {
                    CSkShapeSetLine(GetCSkObjectShape(tempObj), cgStartPt, cgEndPt);
                }
                else
                {
                    objBounds.origin = cgStartPt;   // need to reassign each time, because CGRectStandardize may change it
                    objBounds.size = CGSizeMake(cgEndPt.x - cgStartPt.x, cgEndPt.y - cgStartPt.y);
                    objBounds = CGRectStandardize(objBounds);   // make size components positive
                    CSkShapeSetBounds(GetCSkObjectShape(tempObj), objBounds);
                }
                RenderCSkObject(overlayContext, tempObj, false);
                CGContextSynchronize(overlayContext);       //  Make sure we get our drawing to the screen
                break;

            case kMouseTrackingMouseUp:
            case kMouseTrackingUserCancelled:
                isStillDown = false;
                break;
        }
    }
    
    CGContextClearRect( overlayContext, docRect );
    
    if ( beenDragging )  // we have created a new object
    {
        SetDrawObjAttributesFromToolPalette(tempObj, docStP->toolPalette);
        CSkObjListSetSelectState(&docStP->objList, false);
        SetDrawObjSelectState(tempObj, true);
        AddDrawObjToList(&docStP->objList, tempObj);
        tempObj = NULL;     // don't release it!
    }
    
    if ( tempObj != NULL )
    {
        ReleaseDrawObj(tempObj);
    }
    
    // Make sure we always redraw the new state of the new object list
    DrawWindowContent(window); 

    SetThemeCursor( kThemeArrowCursor );
    
    if (beenDragging && (shapeSelect >= kLineShape))  // set ToolPalette selection back to "Arrow"
    {
        ControlID   cntlID = { kControlSignaturePalette, kArrowTool };
        ControlRef  control;
        GetControlByID(docStP->toolPalette, &cntlID, &control);
        SendControlEventHit(control);
    }
}   // DoMouseToolTracking

//-----------------------------------------------------------------------------------------------------------------------
// We pass this ControlActionProc to HandleControlClick so we get live scrolling.
static void ControlActionProc( ControlRef control, ControlPartCode partCode )
{
    WindowRef       window      = GetControlOwner(control);
    DocStorage*  docStP         = (DocStorage*)GetWRefCon(window);
    SInt16          value       = GetControlValue(control);
    SInt16          minVal      = GetControlMinimum(control);
    SInt16          maxVal      = GetControlMaximum(control);
    int             pageIncr;
    int             buttonIncr;
    ControlID       cntlID;
    Boolean         horizontal;
    
    GetControlID(control, &cntlID);
    horizontal = (cntlID.id == kScrollHorizontalID);
    
    pageIncr = (horizontal ? 3 * (docStP->windowRect.right - docStP->windowRect.left) / 4 : 3 * (docStP->windowRect.bottom - docStP->windowRect.top) / 4);
    buttonIncr = (pageIncr > 10 ? pageIncr / 10 : 1);
        
    switch (partCode)
    {
        case kControlUpButtonPart:      value -= buttonIncr;    break;
        case kControlDownButtonPart:	value += buttonIncr;    break;
        case kControlPageUpPart:        value -= pageIncr;      break;
        case kControlPageDownPart:      value += pageIncr;      break;
    }
    
    if (value < minVal)
        value = minVal;
    else if (value > maxVal)
        value = maxVal;
    SetControlValue(control, value);
    
    if (horizontal)
        docStP->scrollPosition.h = value;
    else
        docStP->scrollPosition.v = value;
        
    RecalculateDisplayCTM(docStP);
    DrawWindowContent(window);                  // for live scrolling
}

//---------------------------------------------------------------------------------------------------------------------
// Create an overlay window for mouse tracking. Its "overlayWindowCtx" should behave the same as
// the window context in terms of drawing our objects (except for stroke and fill colors).
// Note that we do not attempt to keep the overlay window around for repeated use; instead, we
// create and destroy it each time. This allows to avoid WindowGroups, which could be used
// to tie the overlay window to its "parent".

static OSStatus CreateOverlayWindow( Rect* inBounds, WindowRef* outOverlayWindow )
{
    UInt32 flags = kWindowHideOnSuspendAttribute | kWindowIgnoreClicksAttribute;
    OSStatus err = CreateNewWindow( kOverlayWindowClass, flags, inBounds, outOverlayWindow );
    require_noerr(err, CreateNewWindowFAILED);
    
//  SetPortWindowPort( *outOverlayWindow );
    ShowWindow( *outOverlayWindow );
    
CreateNewWindowFAILED:
    return err;
}


//-----------------------------------------------------------------------------------------------------------------------
static void DoContentClick(WindowRef window, Point where, UInt32 modifiers)
{
    DocStorage*     docStP      = (DocStorage*)GetWRefCon( window );
    ControlPartCode partCode;
    ControlRef      control     = FindControlUnderMouse ( where, window, &partCode );

    if (control != NULL)
    {
        HandleControlClick ( control, where, 0, ControlActionProc );
    }
    else
    {
		// Create an overlayWindow for mousetracking feedback
		WindowRef   overlayWindow;
		Rect	    wRect = docStP->windowRect;
		CGRect		cgWinRect = CGRectMake(0, 0, wRect.right - wRect.left, wRect.bottom - wRect.top);
		OSStatus    err;
		
		QDLocalToGlobalRect( GetWindowPort(window), &wRect );
		err = CreateOverlayWindow(&wRect, &overlayWindow);
		if (err == noErr)
		{
			CGContextRef overlayContext;
			CGColorSpaceRef genericColorSpace = GetGenericRGBColorSpace();
			int	    selectedTool = CSkToolPaletteGetTool(docStP->toolPalette);
	
			QDBeginCGContext(GetWindowPort(overlayWindow), &overlayContext);
			
			// This is overkill (again) - we shouldn't have to care about calibrated
			// colorspaces in the overlayWindow. But what the heck ...
			CGContextSetFillColorSpace(overlayContext, genericColorSpace); 
			CGContextSetStrokeColorSpace(overlayContext, genericColorSpace); 
			
			// We do want to clip drawing in the overlay context, just as the
			// drawing into the window needs to be clipped (look there for more comments).
			// Because we are clipping, we should save and restore the overlayContext's GState.
			// However, because we create and destroy the overlayContext in each ContentClick,
			// this is pointless, here.
			ClipToContentArea(overlayContext, window, cgWinRect, wRect.bottom - wRect.top);
			
			CGContextConcatCTM(overlayContext, docStP->displayCTM);
			// From now on, all drawing is done in "document page coordinates"
			
			// also clip to document page, now
			CGRect cgDocRect = CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v);
			CGContextClipToRect(overlayContext, cgDocRect);
			
			// The docStP->bmCtx stays around, so we need to save and restore its GState
			// around the following clip!
			CGContextSaveGState(docStP->bmCtx);
			CGContextClipToRect(docStP->bmCtx,  cgDocRect);

			if (selectedTool == kArrowTool)
			{
				DoMouseArrowTracking(window, overlayContext, where, modifiers);
				
				// if we end up with a new selection, adjust the toolpalette popup check marks
				CSkToolPaletteSyncPopups(FirstSelectedObject(&docStP->objList));
			}
			else
			{
				DoMouseToolTracking(window, overlayContext, MapToolToShape(selectedTool), where);
			}
			
			CGContextRestoreGState(docStP->bmCtx);

			// The rule says to call CGContextSynchronize before QDEndCGContext; but here it
			// really is pointless because we are closing the overlayWindow altogether.
			QDEndCGContext(GetWindowPort(overlayWindow), &overlayContext);
			
			// Always get rid of the overlayWindow. This way, we don't have to play around with window groups,
			// and still avoid problems when switching between several windows or between applications.
			DisposeWindow(overlayWindow);
		}
    }
}	// DoContentClick

//-----------------------------------------------------------------------------------------------------------------------
static void DoResize(WindowRef window, Rect* newBounds)
{
    const int       kScrollbarWidth = 15;
    DocStorage*		docStP          = (DocStorage*)GetWRefCon(window);
    ControlID       cntlID          = { kControlSignatureMainWindow, kScalePopupID };
    ControlRef      control;
    Rect            bounds          = *newBounds;
    
    OffsetRect(&bounds, -bounds.left, -bounds.top);
    docStP->windowRect = bounds;
    
    // Resize and reposition controls
    GetControlByID(window, &cntlID, &control);
    GetControlBounds(control, &bounds);
    
    OffsetRect(&bounds, 0, docStP->windowRect.bottom - bounds.bottom);
    SetControlBounds(control, &bounds);
    
    cntlID.signature = kControlSignatureMainWindow;
    cntlID.id = kScrollHorizontalID;
    GetControlByID(window, &cntlID, &control);
    bounds.top = docStP->windowRect.bottom - kScrollbarWidth;
    bounds.left = bounds.right;                         // scrollbar left = popup button right
    bounds.bottom = docStP->windowRect.bottom;
    bounds.right = docStP->windowRect.right - kScrollbarWidth;
    SetControlBounds(control, &bounds);
    
    cntlID.id = kScrollVerticalID;
    GetControlByID(window, &cntlID, &control);
    bounds.top = 0;
    bounds.left = docStP->windowRect.right - kScrollbarWidth;
    bounds.bottom = docStP->windowRect.bottom - kScrollbarWidth;
    bounds.right = docStP->windowRect.right;
    SetControlBounds(control, &bounds);

    RecalculateDisplayCTM(docStP);
    SetupControls(window, docStP);
    InvalWindowRect(window, &docStP->windowRect);
}

//--------------------------------------------------------------------------------------------------
/////////////////////////////// Support for Copy/Paste of PDF Data /////////////////////////////////
//--------------------------------------------------------------------------------------------------

// To create PDF data for the Pasteboard, we need to set up a CFDataConsumer that collects data in a CFMutableDataRef.
// Here are the two required callbacks:

static size_t MyCFDataPutBytes(void* info, const void* buffer, size_t count)
{
	CFDataAppendBytes((CFMutableDataRef)info, buffer, count);
	return count;
}

static void MyCFDataRelease(void* info)
{
	CFRelease((CFMutableDataRef)info);
}

//--------------------------------------------------------------------------------------------------
// We reuse this routine in NavServicesHandling.c, from "MakePDFDocument":

void DrawIntoPDFPage(CGContextRef pdfContext, CGRect pageBounds, const DocStorage* docStP, UInt32 pageNumber)
{
#pragma unused(pageNumber)
	CGColorSpaceRef genericColorSpace = GetGenericRGBColorSpace();

	CGContextBeginPage(pdfContext, &pageBounds);
	
	// ensure that we are drawing in the correct color space, a calibrated color space
	CGContextSetFillColorSpace(pdfContext, genericColorSpace); 
	CGContextSetStrokeColorSpace(pdfContext, genericColorSpace); 
	
	if (docStP->pdfData != NULL)
	{
		DrawPDFData(pdfContext, docStP->pdfData, pageBounds);
	}
    RenderDrawObjList(pdfContext, &docStP->objList, false);
	CGContextEndPage(pdfContext);
}

//--------------------------------------------------------------------------------------------------
static OSStatus 
AddWindowContentToPasteboardAsPDF( PasteboardRef pasteboard, const DocStorage* docStP)
{
	OSStatus				err		= noErr;
	CGRect					docRect = CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v);
	CFDataRef				pdfData = CFDataCreateMutable( kCFAllocatorDefault, 0);
	CGContextRef			pdfContext;
	CGDataConsumerRef		consumer;
	CGDataConsumerCallbacks cfDataCallbacks = { MyCFDataPutBytes, MyCFDataRelease };
	
	// We need to clear the pasteboard of it's current contents so that this application can
	// own it and add it's own data.
	err = PasteboardClear( pasteboard );
	require_noerr( err, PasteboardClear_FAILED );

	consumer = CGDataConsumerCreate((void*)pdfData, &cfDataCallbacks);
	
	// For now (and for demo purposes), just put the whole window drawing as pdf data
	// on the paste board, regardless of what is selected in the window.
	pdfContext = CGPDFContextCreate(consumer, &docRect, NULL);
	require(pdfContext != NULL, CGPDFContextCreate_FAILED);
	
	DrawIntoPDFPage(pdfContext, docRect, docStP, 1);
	CGContextRelease(pdfContext);   // this finalizes the pdfData
	
	err = PasteboardPutItemFlavor( pasteboard, (PasteboardItemID)1,
						CFSTR("com.adobe.pdf"), pdfData, 0 );
	require_noerr( err, PasteboardPutItemFlavor_FAILED );
	
CGPDFContextCreate_FAILED:
PasteboardPutItemFlavor_FAILED:
	CGDataConsumerRelease(consumer);	// this also releases the pdfData, via MyCFDataRelease

PasteboardClear_FAILED:
	return err;
}

//--------------------------------------------------------------------------------------------------
// Check whether the pasteboard contains pdf data. 
// If so, return the CFDataRef in the pdfData parameter, if it's not NULL.
static Boolean PasteboardContainsPDF(PasteboardRef inPasteboard, CFDataRef* pdfData)
{
	Boolean				gotPDF		= false;
	OSStatus			err			= noErr;
	PasteboardSyncFlags	syncFlags;
	ItemCount			itemCount;
	UInt32				itemIndex;
	
	if (inPasteboard == NULL)	// only if GetPasteboard() failed to allocate the Pasteboard!
	{
		return false;
	}
	
	syncFlags = PasteboardSynchronize(inPasteboard);

	if (syncFlags & kPasteboardModified)
	{
		fprintf(stderr, "Pasteboard modified\n");
	}
	
	// Count the number of items on the pasteboard so we can iterate through them.
	err = PasteboardGetItemCount(inPasteboard, &itemCount);
	require_noerr(err, PasteboardGetItemCount_FAILED);
	
	for (itemIndex = 1; itemIndex <= itemCount; ++itemIndex)
	{
		PasteboardItemID	itemID;
		CFArrayRef			flavorTypeArray;
		CFIndex				flavorCount;
		CFIndex				flavorIndex;
		
		// Every item is identified by a unique value.
		err = PasteboardGetItemIdentifier( inPasteboard, itemIndex, &itemID );
		require_noerr( err, PasteboardGetItemIdentifier_FAILED );
		
		// The flavorTypeArray is a CFType and we'll need to call CFRelease on it later.
		err = PasteboardCopyItemFlavors( inPasteboard, itemID, &flavorTypeArray );
		require_noerr( err, PasteboardCopyItemFlavors_FAILED );
		
		flavorCount = CFArrayGetCount( flavorTypeArray );
		
		for (flavorIndex = 0; flavorIndex < flavorCount; ++flavorIndex)
		{
			CFStringRef				flavorType;
			CFComparisonResult		comparisonResult;
			
			// grab the flavorType so we can extract it's flags and data
			flavorType = (CFStringRef)CFArrayGetValueAtIndex( flavorTypeArray, flavorIndex );

			// Don't care about flavorFlags, for now
			{
//			PasteboardFlavorFlags	flavorFlags;
//			err = PasteboardGetItemFlavorFlags( inPasteboard, itemID, flavorType, &flavorFlags );
//			require_noerr( err, PasteboardGetItemFlavorFlags_FAILED );
			}

			// Look at the flavorTypeStr for debugging:
			{
//			char flavorTypeStr[128];
//			CFStringGetCString( flavorType, flavorTypeStr, 128, kCFStringEncodingMacRoman );
//			fprintf(stderr, "%s\n", flavorTypeStr);
			}
			
			// If it's a pdf, get it and return true
			comparisonResult = CFStringCompare(flavorType, CFSTR("com.adobe.pdf"), 0);
			if (comparisonResult == kCFCompareEqualTo)
			{
				if (pdfData != NULL)
				{
					err = PasteboardCopyItemFlavorData( inPasteboard, itemID, flavorType, pdfData );
					require_noerr( err, PasteboardCopyItemFlavorData_FAILED );
				}
				gotPDF = true;
				break;
			}
						
PasteboardCopyItemFlavorData_FAILED:
//PasteboardGetItemFlavorFlags_FAILED:
			;
		}
		
		CFRelease(flavorTypeArray);

PasteboardCopyItemFlavors_FAILED:
PasteboardGetItemIdentifier_FAILED:
		;
	}
	
PasteboardGetItemCount_FAILED:	
	return gotPDF;
}

//--------------------------------------------------------------------------------------------------
// For demo purposes, just keep the pasted-in pdf data separately from the CSkObject list.
// A new "Paste" replaces any previous pdf data.
static void AttachPDFToWindow(CFDataRef pdfData, DocStorage* docStP)
{
	if (docStP->pdfData != NULL)
	{
		CFRelease(docStP->pdfData);
		docStP->pdfData = NULL;
	}
	docStP->pdfData = CFRetain(pdfData);
}

//--------------------------------------------------------------------------------------------------
// Make sure "Copy" and "Paste" are correctly enabled/disabled.
static void DoCommandUpdateStatus(EventRef inEvent, WindowRef window, DocStorage* docStP)
{
#pragma unused(inEvent, window, docStP)		// for now
	MenuRef			menu;
	MenuItemIndex   unused;
	
	// For now, always enable "Copy" to put a (potentially empty) pdf on the pasteboard
	GetIndMenuItemWithCommandID(NULL, kHICommandCopy, 1, &menu, &unused);
	EnableMenuCommand(menu, kHICommandCopy);
	
	// For now, enable "Paste" only if we have a pdf on the pasteboard
	GetIndMenuItemWithCommandID(NULL, kHICommandPaste, 1, &menu, &unused);
	if ( PasteboardContainsPDF(GetPasteboard(), NULL) )
		EnableMenuCommand(menu, kHICommandPaste);
	else
		DisableMenuCommand(menu, kHICommandPaste);
}

//--------------------------------------------------------------------------------------------------
// Handle a relatively long list of window HICommands:

static OSStatus DoCommandProcess(EventRef inEvent, WindowRef window, DocStorage* docStP)
{
    OSStatus            err			= eventNotHandledErr;
    CGrgba              color;
    HICommandExtended   command;
    short				itemNo;
    Boolean             scaleChanged = false;
    
    GetEventParameter( inEvent, kEventParamDirectObject, typeHICommand, NULL, sizeof(HICommand), NULL, &command );

//  { char* c = &command.commandID;
//  fprintf(stderr, "kEventClassCommand, commandID = %c%c%c%c\n", c[0], c[1], c[2], c[3]); }

    switch (command.commandID)
    {
		case kHICommandCopy:
		// for now, only demonstrate how to put the current document content as 'pdf' on the clip board
		{
			err = AddWindowContentToPasteboardAsPDF( GetPasteboard(), docStP);
		}
		break;

		case kHICommandPaste:
		// for now, only demonstrate how to paste in a 'pdf' from the clip board
		{
			CFDataRef pdfData;
			if ( PasteboardContainsPDF(GetPasteboard(), &pdfData) )
			{
				AttachPDFToWindow(pdfData, docStP); // this will retain the pdfData in docStP
				CFRelease(pdfData);
			}
			else
			{
				fprintf(stderr, "kHICommandPaste: Paste menu item should have been disabled!\n");
			}
			err = noErr;
		}
		break;

        case kHICommandClose:
            // For now, do nothing here; kEventWindowClose will deal with window closing.
	    	// Later, we'll ask whether a "dirty" window needs to be saved.
        break;
		
        case kHICommandPageSetup:
        case kHICommandPrint:
        {
            ProcessPrintCommand(docStP, command.commandID);
	    	err = noErr;
        }
        break;
        
        case kCmdWritePDF:
			(void)SaveAsPDFDocument(window, docStP);
	    	err = noErr;
		break;
			
        case kHICommandClear:       
			RemoveSelectedDrawObjs(&docStP->objList); 
			err = noErr;
		break;
        
		case kHICommandSelectAll:
			CSkObjListSetSelectState(&docStP->objList, true);
			err = noErr;
		break;
        
        case kCmdDuplicate:
			DuplicateSelectedDrawObjs(&docStP->objList, docStP->dupOffset);
			err = noErr;
		break;
        
        case kCmdMoveForward:
			MoveObjectForward(&docStP->objList);
			err = noErr;
		break;

        case kCmdMoveToFront:
			MoveObjectToFront(&docStP->objList);
			err = noErr;
		break;
        
        case kCmdMoveBackward:
			MoveObjectBackward(&docStP->objList);
			err = noErr;
		break;
        
        case kCmdMoveToBack:
			MoveObjectToBack(&docStP->objList);
			err = noErr;
		break;
        
        case kCmdScale50:
			docStP->scale = 0.5;   scaleChanged = true;
			itemNo = 1;
			err = noErr;
		break;
	
        case kCmdScale100:
			docStP->scale = 1.0;   scaleChanged = true;
			itemNo = 2;
			err = noErr;
		break;
        
        case kCmdScale200:
			docStP->scale = 2.0;   scaleChanged = true;
			itemNo = 3;
			err = noErr;
	    break;

		// Line Width
		case kCmdWidthNone:      
			SetLineWidthOfSelecteds(&docStP->objList, 0.0);
			err = noErr;
			break;
		case kCmdWidthOne:       
			SetLineWidthOfSelecteds(&docStP->objList, 1.0);
			err = noErr;
			break;
		case kCmdWidthTwo:       
			SetLineWidthOfSelecteds(&docStP->objList, 2.0);
			err = noErr;
			break;
		case kCmdWidthFour:      
			SetLineWidthOfSelecteds(&docStP->objList, 4.0);
			err = noErr;
			break;
		case kCmdWidthEight:     
			SetLineWidthOfSelecteds(&docStP->objList, 8.0);
			err = noErr;
			break;
		case kCmdWidthSixteen:   
			SetLineWidthOfSelecteds(&docStP->objList, 16.0);
			err = noErr;
			break;
		case kCmdWidthThinner:   
			SetLineWidthOfSelecteds(&docStP->objList, kMakeItThinner);
			err = noErr;
			break;
		case kCmdWidthThicker:
			SetLineWidthOfSelecteds(&docStP->objList, kMakeItThicker);
			err = noErr;
			break;
		case kCmdLineWidthChanged:  
			SetLineWidthOfSelecteds(&docStP->objList, CSkToolPaletteGetLineWidth(docStP->toolPalette));
			err = noErr;
			break;
			
		// Line Cap
		case kCmdCapButt:        
				SetLineCapOfSelecteds(&docStP->objList, kCGLineCapButt);
			err = noErr;
			break;
		case kCmdCapRound:       
				SetLineCapOfSelecteds(&docStP->objList, kCGLineCapRound);
			err = noErr;
			break;
		case kCmdCapSquare:      
				SetLineCapOfSelecteds(&docStP->objList, kCGLineCapSquare);
			err = noErr;
			break;
			case kCmdLineCapChanged:
				SetLineCapOfSelecteds(&docStP->objList, CSkToolPaletteGetLineCap(docStP->toolPalette));
			err = noErr;
			break;
			
		// Line Join
		case kCmdJoinMiter:
				SetLineJoinOfSelecteds(&docStP->objList, kCGLineJoinMiter);
			err = noErr;
			break;
		case kCmdJoinRound:
				SetLineJoinOfSelecteds(&docStP->objList, kCGLineJoinRound);
			err = noErr;
			break;
		case kCmdJoinBevel:
				SetLineJoinOfSelecteds(&docStP->objList, kCGLineJoinBevel);
			err = noErr;
			break;
			case kCmdLineJoinChanged:
				SetLineJoinOfSelecteds(&docStP->objList, CSkToolPaletteGetLineJoin(docStP->toolPalette));
			err = noErr;
			break;
			
		// Line Style
		case kCmdStyleSolid:
				SetLineStyleOfSelecteds(&docStP->objList, kStyleSolid);
			err = noErr;
			break;
		case kCmdStyleDashed:
				SetLineStyleOfSelecteds(&docStP->objList, kStyleDashed);
			err = noErr;
			break;
			case kCmdLineStyleChanged:
				SetLineStyleOfSelecteds(&docStP->objList, CSkToolPaletteGetLineStyle(docStP->toolPalette));
			err = noErr;
			break;
        
        case kCmdStrokeColorChanged:
            SetStrokeColorOfSelecteds(&docStP->objList, CSkToolPaletteGetStrokeColor(docStP->toolPalette, &color));
			err = noErr;
	    break;
        
        case kCmdFillColorChanged:
            SetFillColorOfSelecteds(&docStP->objList, CSkToolPaletteGetFillColor(docStP->toolPalette, &color));
			err = noErr;
	    break;
        
        case kCmdStrokeAlphaChanged:
        {
            ControlID   cntlID  = { kControlSignaturePalette, kStrokeAlphaSlider };
            ControlRef  control;
            CGrgba      color;
            float       alpha;
            
            GetControlByID(docStP->toolPalette, &cntlID, &control);
            alpha = (float)GetControlValue(control) / 100.0;
            CSkToolPaletteGetStrokeColor(docStP->toolPalette, &color);
            color.a = alpha;
            CSkToolPaletteSetStrokeColor(docStP->toolPalette, &color);
            SetStrokeAlphaOfSelecteds(&docStP->objList, alpha);
			err = noErr;
        }
        break;
        
        case kCmdFillAlphaChanged:
        {
            ControlID   cntlID  = { kControlSignaturePalette, kFillAlphaSlider };
            ControlRef  control;
            CGrgba      color;
            float       alpha;
            
            GetControlByID(docStP->toolPalette, &cntlID, &control);
            alpha = (float)GetControlValue(control) / 100.0;
            CSkToolPaletteGetFillColor(docStP->toolPalette, &color);
            color.a = alpha;
            CSkToolPaletteSetFillColor(docStP->toolPalette, &color);
            SetFillAlphaOfSelecteds(&docStP->objList, alpha);
			err = noErr;
        }
        break;
    }   // switch
    
    if (scaleChanged)
    {
        ControlID   cntlID  = { kControlSignatureMainWindow, kScalePopupID };
        ControlRef  control;
        Str255      text;
		
        GetControlByID(window, &cntlID, &control);
        GetMenuItemText(command.source.menu.menuRef, command.source.menu.menuItemIndex, text);
        SetControlTitle(control, text);
		SetControlData(control, kControlEntireControl, kControlBevelButtonMenuValueTag, sizeof(itemNo), &itemNo);
        SetupControls(window, docStP);                  // do the scroll thumbs still need to be drawn?
        RecalculateDisplayCTM(docStP);
    }
    
    DrawWindowContent(window);                          // redraw unconditionally, for now
    
    return err;
}   // DoCommandProcess


//-----------------------------------------------------------------------------------------------------------------------
static pascal OSStatus CSkWindowEventHandler( EventHandlerCallRef inCallRef, EventRef inEvent, void* inUserData )
{
    #pragma unused ( inCallRef )
    OSStatus	err		= eventNotHandledErr;
    UInt32	eventKind   = GetEventKind( inEvent );
    UInt32	eventClass  = GetEventClass( inEvent );

    WindowRef   window  = (WindowRef)inUserData;
    DocStorage*  docStP = (DocStorage*)GetWRefCon( window );
    
    switch ( eventClass )
    {
    case kEventClassKeyboard:
        if (eventKind == kEventRawKeyDown )
        {
            char ch;
            
            err = GetEventParameter( inEvent, 
                                    kEventParamKeyMacCharCodes, 
                                    typeChar,
                                    NULL,
                                    sizeof( char ), 
                                    NULL, 
                                    &ch );
            if (ch == kBackspaceCharCode)
            {
                HICommand hiCmd;
                memset(&hiCmd, 0, sizeof(hiCmd));
                hiCmd.commandID = kHICommandClear;
                SendWindowCommandEvent(window, &hiCmd);
            }
        }
        
    case kEventClassWindow:
        if ( eventKind == kEventWindowDrawContent )
        {
            DrawWindowContent(window);
			DrawControls(window);
			err = noErr;
        }
        else if ( eventKind == kEventWindowClose )	// Window is about to close
		{
			// check if dirty, and if so, whether to save the document
		}
        else if ( eventKind == kEventWindowClosed )	// Dispose extra window storage here
        {
            ReleaseDocumentStorage( docStP );
            // Should check whether this is the last window, and if so, close the toolPalette, too
        }
        else if ( eventKind == kEventWindowClickContentRgn )
        {
            Point   where;
            UInt32  modifiers;
            
            GetEventParameter(inEvent, kEventParamMouseLocation, typeQDPoint, NULL, sizeof(Point), NULL, &where);
            GetEventParameter(inEvent, kEventParamKeyModifiers, typeUInt32, NULL, sizeof(UInt32), NULL, &modifiers);
            QDGlobalToLocalPoint(GetWindowPort(window), &where);
            DoContentClick(window, where, modifiers);
            //err = noErr;
        }
        else if ( eventKind == kEventWindowBoundsChanged )
        {
            Rect r;

            (void) GetEventParameter( inEvent, kEventParamCurrentBounds, typeQDRectangle, NULL, sizeof(Rect), NULL, &r );

            // We get here also when the window gets moved and doesn't need to be redrawn.
			#define RECTWIDTH(r)	    ((r).right - (r).left)
			#define RECTHEIGHT(r)       ((r).bottom - (r).top)

			if ( (RECTWIDTH(docStP->windowRect) != RECTWIDTH(r)) || (RECTHEIGHT(docStP->windowRect) != RECTHEIGHT(r)) )
			{
				DoResize(window, &r);
			}
        }
        break;
            
    case kEventClassCommand:
        if ( eventKind == kEventCommandUpdateStatus )
		{
			DoCommandUpdateStatus(inEvent, window, docStP);
			err = noErr;
		}
        else if ( eventKind == kEventCommandProcess )
        {
            err = DoCommandProcess(inEvent, window, docStP);
        }
        break;
    }

    return err;
}


//-----------------------------------------------------------------------------------------------------------------------
OSStatus NewCSkWindow(IBNibRef theNibRef, WindowRef toolPalette)
{
    const EventTypeSpec	windowEvents[]	=   {   { kEventClassCommand,   kEventCommandProcess },
												{ kEventClassCommand,   kEventCommandUpdateStatus },
                                                { kEventClassKeyboard, 	kEventRawKeyDown },
                                                { kEventClassWindow,    kEventWindowClickContentRgn },
                                                { kEventClassWindow, 	kEventWindowDrawContent },
                                                { kEventClassWindow,    kEventWindowBoundsChanged },
                                                { kEventClassWindow,    kEventWindowClose },
                                                { kEventClassWindow,    kEventWindowClosed }
                                            };
											
    static EventHandlerUPP  gCSkWindowEventProc = NULL;
    WindowRef				window;
    OSStatus				err		= CreateWindowFromNib( theNibRef, CFSTR("MainWindow"), &window );

    if (err == noErr)
    {
        // Fit the starter composition without requiring an initial resize.
        SizeWindow(window, 630, 570, false);
        DocStorage*  docStP = CreateDocumentStorage(window, toolPalette);
        SetWRefCon( window, (long) docStP );    // Should use SetWindowProperty()
        CreateStarterDrawing(docStP);
        
		if (gCSkWindowEventProc == NULL)
		{
			gCSkWindowEventProc = NewEventHandlerUPP(CSkWindowEventHandler);
			if (gCSkWindowEventProc == NULL)
				return memFullErr;		// We are screwed. Really.
		}

        err = InstallWindowEventHandler( window, gCSkWindowEventProc, GetEventTypeCount(windowEvents), windowEvents, window, NULL );

        Rect bounds;
        GetWindowPortBounds(window, &bounds);
        DoResize(window, &bounds);

        ShowWindow( window );
        fprintf(stderr, "[Ready] Canvas open. Try the shape tools or export your drawing as PDF.\n");
    }
	return err;
}
