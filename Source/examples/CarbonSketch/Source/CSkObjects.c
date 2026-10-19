/*
    File:       CSkObjects.c
        
    Contains:	Definition of (opaque) CSkObject structure, and various related routines.
                Refers to CSkShape for geometric description, and contains additional attributes.
				CSkObjets form a doubly-linked list to deal with z-ordering.
				
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


#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>

#include "CSkObjects.h"
// also includes "CSkShapes.h"
#include "CSkConstants.h"
#include "CSkToolPalette.h"

// CSkObjects (or "DrawObjects" as they were called in the first stages of development) are stored 
// in a double-linked list. They contain a CSkShapePtr to the geometry definition, and
// specifications needed for drawing. 
struct CSkObject
{
	CSkShapePtr		shape;
    float           lineWidth;
    CGLineCap       lineCap;
    CGLineJoin      lineJoin;
    int             lineStyle;
    CGrgba          strokeColor;
    CGrgba          fillColor;
    Boolean         selected;
    CSkObjectPtr      nextObj;
    CSkObjectPtr      prevObj;
};


//---------------------------------------------------------------------------------------------
// Return the specified line attributes lineWidth, lineCap, lineJoin, lineStyle.
// No NULL-checks in the out-parameters because 
// a) the function is only used with parameters &lineWidth, &lineCap, &lineJoin, &lineStyle
// b) I want to keep the code short
void GetLineAttributes(const CSkObject* obj, float* width, CGLineCap* cap, CGLineJoin* join, int* style)
{
	if (obj != NULL)
	{
		*width  = obj->lineWidth;
		*cap	= obj->lineCap;
		*join   = obj->lineJoin;
		*style  = obj->lineStyle;
	}
	else
	{
		fprintf(stderr, "GetLineAttributes: NULL obj parameter\n");
	}
}

//---------------------------------------------------------------------------------------------
// Allocate new drawObject with attributes from the current settings in the CSkToolPalette.
// Bounds are empty.
CSkObjectPtr CreateDrawObj(WindowRef toolPalette, int shapeType)
{
    CSkObjectPtr obj = (CSkObjectPtr)NewPtrClear(sizeof(CSkObject));
    if (obj != NULL)
    {
		SetDrawObjAttributesFromToolPalette(obj, toolPalette);
		obj->shape = CSkShapeCreate(shapeType);
    }
    return obj;
}

//---------------------------------------------------------------------------------------------
// After mouse-tracking a selected CSkObject, we need an independent (unlinked) copy of it 
// to draw during dragging.
CSkObjectPtr CopyDrawObject(const CSkObject* obj)
{
    CSkObjectPtr newObj = (CSkObjectPtr)NewPtrClear(sizeof(CSkObject));
    if (newObj)
    {
		memcpy(newObj, obj, sizeof(CSkObject));
        newObj->shape = CSkShapeCreate(kUndefined);
		memcpy(newObj->shape, obj->shape, CSkShapeSize());
        newObj->nextObj = NULL;
        newObj->prevObj = NULL;
    }
    return newObj;
}

//---------------------------------------------------------------------------------------------
void ReleaseDrawObj(CSkObjectPtr obj)
{
    CSkShapeRelease(obj->shape);
	DisposePtr((Ptr)obj);
}

void ReleaseDrawObjList(DrawObjListPtr objList)
{    
    CSkObjectPtr obj = objList->firstItem;
    
    while ( obj != NULL )
    {
        CSkObjectPtr deleteThis = obj;
        obj = obj->nextObj;
        ReleaseDrawObj(deleteThis);
    }
}


//---------------------------------------------------------------------------------------------
// Some obvious accessors
int GetDrawObjShapeType( const CSkObject* drawObj )
{
    return CSkShapeGetType(drawObj->shape);
}

CSkShape* GetCSkObjectShape( const CSkObject* drawObj )
{
	return drawObj->shape;
}

float GetFillAlpha( const CSkObject* drawObj )
{
	return drawObj->fillColor.a;
}

float GetStrokeAlpha( const CSkObject* drawObj )
{
	return drawObj->strokeColor.a;
}

void SetDrawObjSelectState( CSkObjectPtr drawObj, Boolean selected )
{
    drawObj->selected = selected;
}

Boolean IsDrawObjSelected( const CSkObject* drawObj )
{
    return drawObj->selected;
}


//----------------------------------------------------------------------------
void SetDrawObjAttributesFromToolPalette(CSkObjectPtr obj, WindowRef toolPalette)
{
    obj->lineWidth  = CSkToolPaletteGetLineWidth(toolPalette);
    obj->lineCap    = CSkToolPaletteGetLineCap(toolPalette);
    obj->lineJoin   = CSkToolPaletteGetLineJoin(toolPalette);
    obj->lineStyle  = CSkToolPaletteGetLineStyle(toolPalette);
    CSkToolPaletteGetStrokeColor(toolPalette, &obj->strokeColor);
    CSkToolPaletteGetFillColor(toolPalette, &obj->fillColor);
}

void SetSelectedDrawObjAttributesFromToolPalette(DrawObjListPtr objListP, WindowRef toolPalette)
{
    CSkObjectPtr obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            SetDrawObjAttributesFromToolPalette(obj, toolPalette);
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetLineWidthOfSelecteds(DrawObjListPtr objListP, float lineWidth)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
        {
			if (lineWidth == kMakeItThinner)
			{
				if (obj->lineWidth >= 2.0)
					obj->lineWidth -= 1.0;
			}
			else if (lineWidth == kMakeItThicker)
				obj->lineWidth += 1.0;
			else
				obj->lineWidth = lineWidth;
		}
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetLineCapOfSelecteds(DrawObjListPtr objListP, CGLineCap lineCap)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            obj->lineCap = lineCap;
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetLineJoinOfSelecteds(DrawObjListPtr objListP, CGLineJoin lineJoin)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            obj->lineJoin = lineJoin;
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetLineStyleOfSelecteds(DrawObjListPtr objListP, int lineStyle)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            obj->lineStyle = lineStyle;
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetStrokeColorOfSelecteds(DrawObjListPtr objListP, CGrgba* color)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            obj->strokeColor = *color;
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetStrokeAlphaOfSelecteds(DrawObjListPtr objListP, float alpha)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            obj->strokeColor.a = alpha;
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetFillColorOfSelecteds(DrawObjListPtr objListP, CGrgba* color)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            obj->fillColor = *color;
        obj = obj->nextObj;
    }
}

//----------------------------------------------------------------------------
void SetFillAlphaOfSelecteds(DrawObjListPtr objListP, float alpha)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            obj->fillColor.a = alpha;
        obj = obj->nextObj;
    }
}

//--------------------------------------------------------
CSkObjectPtr FirstSelectedObject(const DrawObjList* objListP)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
            return obj;
        obj = obj->nextObj;
    }
    return NULL;
}

//------------------------------------------------------------------------
// Needed for dragselection of several objects
void CSkObjListSelectWithinRect(DrawObjList* objListP, CGRect selectionRect)
{
    CSkObjectPtr  obj = objListP->firstItem;
    while (obj != NULL)
    {
        CGRect bounds = CSkShapeGetBounds(obj->shape);
        if ( CGRectContainsRect(selectionRect, bounds) )
			obj->selected = true;
        obj = obj->nextObj;
    }
}


//-------------------------------------------
///////////// Drawing Routines //////////////
//
// We don't distinguish between "filled" and "not filled"; the result
// "stroke only" is achieved by setting the fillColor alpha to fully
// transparent. Similarly, if we want "filled only", we have to set
// the alpha of the strokeColor to 0.
// Consequently, we always pass kCGPathFillStroke to CGContextDrawPath.

//-------------------------------------------------------------------------
static void DrawRect(CGContextRef ctx, CGRect cgRect)
{
    CGContextBeginPath(ctx);			// reset current path to empty
	CGContextAddRect(ctx, cgRect);		// current path now represents cgRect
	CGContextDrawPath(ctx, kCGPathFillStroke);
}

//-------------------------------------------------------------------------
static void DrawOval(CGContextRef ctx, CGRect cgRect)
{
    const float TWOPI   = 6.283185307;
    float   halfWidth   = 0.5 * CGRectGetWidth(cgRect);
    float   halfHeight  = 0.5 * CGRectGetHeight(cgRect);
    float   centerX, centerY, radius;
    float   scaleX, scaleY;

    if (halfWidth < halfHeight)
    {
        radius = halfWidth;
        scaleX = 1.0;
        scaleY = halfHeight / halfWidth;
        centerX = cgRect.origin.x + halfWidth;
        centerY = (cgRect.origin.y + halfHeight) / scaleY;
    }
    else
    {
        radius = halfHeight;
        scaleX = halfWidth / halfHeight;
        scaleY = 1.0;
        centerX = (cgRect.origin.x + halfWidth) / scaleX;
        centerY = cgRect.origin.y + halfHeight;
    }

    CGContextSaveGState(ctx);				// because we temporarily change the CTM
    CGContextScaleCTM(ctx, scaleX, scaleY);  // so the full-circle arc will appear as oval
    CGContextBeginPath(ctx);
    CGContextAddArc(ctx, centerX, centerY, radius, 0.0, TWOPI, 0);
    CGContextClosePath(ctx);
    CGContextRestoreGState(ctx);	// back to the previous CTM

	CGContextDrawPath(ctx, kCGPathFillStroke);
}

//--------------------------------------------------------------------------------------
static void DrawRRect(CGContextRef ctx, CGRect cgRect, float rX, float rY)
{
	CGContextBeginPath(ctx);

    if ((rX > 0) && (rY > 0))
    {
        float width     = CGRectGetWidth(cgRect);
        float height    = CGRectGetHeight(cgRect);
        float fw        = width / rX;
        float fh        = height / rY;

        if (fw < 2)
        {
            rX = width / 2;
            fw = 2;
        }
        
        if (fh < 2)
        {
            rY = height / 2;
            fh = 2;
        }

        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, CGRectGetMinX(cgRect), CGRectGetMinY(cgRect));
        CGContextScaleCTM(ctx, rX, rY);
        CGContextMoveToPoint(ctx, fw, fh/2);
        CGContextAddArcToPoint(ctx, fw, fh, fw/2, fh, 1);
        CGContextAddArcToPoint(ctx, 0, fh, 0, fh/2, 1);
        CGContextAddArcToPoint(ctx, 0, 0, fw/2, 0, 1);
        CGContextAddArcToPoint(ctx, fw, 0, fw, fh/2, 1);
        CGContextRestoreGState(ctx);     
    }
    else
    {
        CGContextAddRect(ctx, cgRect);
    }           
    
    CGContextClosePath(ctx);
	CGContextDrawPath(ctx, kCGPathFillStroke);
}

//--------------------------------------------------------------------------------------
static void DrawCGLine(CGContextRef ctx, CGPoint a, CGPoint b)
{
    CGContextMoveToPoint( ctx, a.x, a.y );
    CGContextAddLineToPoint( ctx, b.x, b.y );
    CGContextStrokePath( ctx );
}

//--------------------------------------------------------------------------------------
// Keep this separate from RenderCSkObject; this way, RenderCSkObject can
// be reused from within the mousetracking loops when drawing into an overlay window.
void SetContextStateForDrawObject(CGContextRef ctx, const CSkObject* obj)
{
    CGContextSetLineWidth(ctx, obj->lineWidth);
    CGContextSetLineCap(ctx, obj->lineCap);
    CGContextSetLineJoin(ctx, obj->lineJoin);
    if (obj->lineStyle == kStyleDashed)
    {
        float dashLengths[2] = { obj->lineWidth + 4, obj->lineWidth + 4 };
        CGContextSetLineDash(ctx, 1.0, dashLengths, 2);
    }
    
    CGContextSetStrokeColor( ctx, (float *)&(obj->strokeColor));
    CGContextSetFillColor( ctx, (float *)&(obj->fillColor));
}


//--------------------------------------------------------------------------------------
// RenderCSkObject is being called from RenderDrawObjList, and also during MouseTracking
// (see CSkWindow.c). 
void RenderCSkObject(CGContextRef ctx, const CSkObject* obj, Boolean drawSelection)
{
    int		shapeType   = CSkShapeGetType(obj->shape);
    CGRect  shapeBounds = CSkShapeGetBounds(obj->shape);
	CGPoint a, b;
	
	if ( shapeType == kLineShape )
	{
		CSkShapeGetLine(obj->shape, &a, &b);
		DrawCGLine(ctx, a, b);
	}
	else switch (shapeType)
	{
		case kRectShape:
			DrawRect(ctx, shapeBounds);
			break;
			
		case kOvalShape:
			DrawOval(ctx, shapeBounds);
			break;
		
		case kRRectShape:
			{
				float rX, rY;
				CSkShapeGetRRectRadii(obj->shape, &rX, &rY);
				DrawRRect(ctx, shapeBounds, rX, rY);
			}
			break;
	}
	
    if (drawSelection && obj->selected)  // draw little "grabber" squares
    {
		CGRect  grabRect;
		UInt32  grabNum = 0;
		
		CGContextSaveGState(ctx);	// because we are changing colors and line width
		
		CGContextSetRGBFillColor(ctx, 0.9, 0.9, 0.9, 0.7);  // ltGray, a little transparent
		CGContextSetRGBStrokeColor(ctx, 0, 0, 0, 1.0);      // black
		CGContextSetLineWidth(ctx, 1.0);

		while (NextGrabberRect(obj->shape, &grabNum, &grabRect))
		{
			DrawRect(ctx, grabRect);
			// Alternatively:
			// CGContextStrokeRect(ctx, grabRect);
			// CGContextFillRect(ctx, grabRect);
		}
		
		CGContextRestoreGState(ctx);
    }
}	// RenderCSkObject

//---------------------------------------------------------------------------------------------
// Draw the CSkObjects in the linked list from back to front. For each object, the GState needs
// to be saved and restored.
void  RenderDrawObjList(CGContextRef ctx, const DrawObjList* objListP, Boolean drawSelection)
{
    CSkObjectPtr obj = objListP->lastItem;    // draw from back to front
    while (obj != NULL)
    {
		CGContextSaveGState(ctx);		// because SetContextStateForDrawObject is doing what it says it will
		SetContextStateForDrawObject(ctx, obj);
        RenderCSkObject(ctx, obj, drawSelection);
		CGContextRestoreGState(ctx);	// undo the changes for the specific obj drawing
        obj = obj->prevObj;
    }
}


//---------------------------------------------------------------------------------------------
// Multiply in a transparency factor. Used for tracking feedback when resizing an object.
void MakeDrawObjTransparent(CSkObject* obj, float alpha)
{
    obj->strokeColor.a *= alpha;
    obj->fillColor.a *= alpha;
}

//---------------------------------------------------------------------------------------------
// The following is used during moving selected objects around (ctx is overlayWindowContext).
// Draw the selected objects only, and with an additional alpha multiplied in for more transparency.
void  RenderSelectedDrawObjs(CGContextRef ctx, const DrawObjList* objListP, float offsetX, float offsetY, float alpha)
{
    CSkObjectPtr obj = objListP->lastItem;    // draw from back to front
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, offsetX, offsetY);
    while (obj != NULL)
    {
        if (obj->selected)
        {
            CGrgba saveStrokeColor  = obj->strokeColor;
            CGrgba saveFillColor    = obj->fillColor;
            
			MakeDrawObjTransparent(obj, alpha);
			SetContextStateForDrawObject(ctx, obj);
            RenderCSkObject(ctx, obj, true);
			
            obj->strokeColor = saveStrokeColor;
            obj->fillColor = saveFillColor;
        }
        obj = obj->prevObj;
    }
    CGContextRestoreGState(ctx);   
}

//---------------------------------------------------------------------------------------------
// Called at the end of mouse tracking when selected objects have been moved
void  MoveSelectedDrawObjs(DrawObjList* objListP, float offsetX, float offsetY)
{
    CSkObjectPtr obj = objListP->firstItem;
    while (obj != NULL)
    {
        if (obj->selected)
        {
			CSkShapeOffset(obj->shape, offsetX, offsetY);
        }
        obj = obj->nextObj;
    }
}


//---------------------------------------------------------------------------------------------
// Select all, or deselect all
void CSkObjListSetSelectState(DrawObjListPtr objList, Boolean state)
{
    CSkObjectPtr obj = objList->firstItem;
    
    while (obj != NULL)
    {		
        obj->selected = state;
        obj = obj->nextObj;	
    }
}


//---------------------------------------------------------------------------------------------
// Called when reording objects in the list
static CSkObjectPtr GetFirstSelectedDrawObj(const DrawObjList* objList)
{
    CSkObjectPtr  obj = objList->firstItem;
    CSkObjectPtr  selectedObject = NULL;
    
    while (obj != NULL)
    {	
        if (obj->selected == true)
        {
            selectedObject = obj;
            break;
        }
        obj = obj->nextObj;
    }
    return selectedObject;
}


//----------------------------------------------------------------------------
// Some obvious linked list management routines

void AddDrawObjToList(DrawObjListPtr objList, CSkObjectPtr obj)
{
    CSkObjectPtr	firstObj = objList->firstItem;
    
    obj->prevObj = NULL;     // always put it in front of the list
    obj->nextObj = firstObj;
    objList->firstItem = obj;

    if (firstObj == NULL)
    {
        objList->lastItem   = obj;
    }
    else
    {
        firstObj->prevObj = obj;
    }
}

//----------------------------------------------------------------------
static void RemoveDrawObjFromList(DrawObjListPtr objList, const CSkObject* obj)
{
    CSkObjectPtr prevObj = obj->prevObj;
    CSkObjectPtr nextObj = obj->nextObj;
    
    if (prevObj != NULL)
        prevObj->nextObj = nextObj;
            
    if (nextObj != NULL)
        nextObj->prevObj = prevObj;
            
    if (objList->firstItem == obj)
        objList->firstItem = obj->nextObj;
            
    if (objList->lastItem == obj)
        objList->lastItem = obj->prevObj;
}

//----------------------------------------------------------------------
void RemoveSelectedDrawObjs(DrawObjListPtr objList)
{
    CSkObjectPtr	obj = objList->firstItem;
    while (obj != NULL)
    {
        CSkObjectPtr nextObj = obj->nextObj;
        if (obj->selected)
        {
            RemoveDrawObjFromList(objList, obj);
        }
        obj = nextObj;
    }
}

//----------------------------------------------------------------------
static void InsertDrawObjBefore(DrawObjListPtr objList, CSkObjectPtr obj, CSkObjectPtr beforeObj)
{
    CSkObjectPtr prevObj = beforeObj->prevObj;
    
    if (prevObj != NULL)
        prevObj->nextObj = obj;
    
    obj->prevObj = prevObj;
    obj->nextObj = beforeObj;
    beforeObj->prevObj = obj;
    
    if (objList->firstItem == beforeObj)
            objList->firstItem = obj;
}


//----------------------------------------------------------------------
static void InsertDrawObjAfter(DrawObjListPtr objList, CSkObjectPtr obj, CSkObjectPtr afterObj)
{
    CSkObjectPtr nextObj = afterObj->nextObj;
    
    if (nextObj != NULL)
        nextObj->prevObj = obj;
    
    obj->nextObj = nextObj;
    obj->prevObj = afterObj;
    afterObj->nextObj = obj;
    
    if (objList->lastItem == afterObj)
        objList->lastItem = obj;
}

//----------------------------------------------------------------------
static void DuplicateDrawObj(DrawObjListPtr objList, CSkObjectPtr obj, CGPoint offset)
{
    CSkObjectPtr tempObj = CopyDrawObject(obj);
    // Always deselect the original and select the new
    obj->selected = false;
    tempObj->selected = true;
//  tempObj->bounds = CGRectOffset(tempObj->bounds, offset.x, offset.y);    -- this normalizes the rect, which is not what we want for lines
    CSkShapeOffset(tempObj->shape, offset.x, offset.y);
    InsertDrawObjBefore(objList, tempObj, obj);
}

//----------------------------------------------------------------------
void DuplicateSelectedDrawObjs(DrawObjListPtr objList, CGPoint offset)
{
    CSkObjectPtr	obj = objList->firstItem;
    while (obj != NULL)
    {
        CSkObjectPtr nextObj = obj->nextObj;
        if (obj->selected)
        {
            DuplicateDrawObj(objList, obj, offset);
        }
        obj = nextObj;
    }
}


//----------------------------------------------------------------------
void MoveObjectForward(DrawObjListPtr objList)
{
    CSkObjectPtr obj = GetFirstSelectedDrawObj(objList);
    if (obj != NULL)
    {
        // remember where we were
        CSkObjectPtr prevObj = obj->prevObj;
            
        // only remove and insert if there is a previous item
        if (prevObj != NULL)
        {
            RemoveDrawObjFromList(objList, obj);    // pull obj out of list
            InsertDrawObjBefore(objList, obj, prevObj);
        }
    }
}

//----------------------------------------------------------------------
void MoveObjectBackward(DrawObjListPtr objList)
{
    CSkObjectPtr obj = GetFirstSelectedDrawObj(objList);
    if (obj != NULL)
    {
        // remember where we were
        CSkObjectPtr nextObj = obj->nextObj;
        
        // only remove and insert if there is a next
        if (nextObj != NULL)
        {
            RemoveDrawObjFromList(objList, obj);    // pull obj out of list
            InsertDrawObjAfter(objList, obj, nextObj);
        }
    }
}

//----------------------------------------------------------------------
void MoveObjectToFront(DrawObjListPtr objList)
{
    CSkObjectPtr obj = GetFirstSelectedDrawObj(objList);
    if ((obj != NULL) && (obj != objList->firstItem))
    {
        RemoveDrawObjFromList(objList, obj);     // pull obj out of list
        AddDrawObjToList(objList, obj);        // add it in front
    }
}


//----------------------------------------------------------------------
void MoveObjectToBack(DrawObjListPtr objList)
{
    CSkObjectPtr obj = GetFirstSelectedDrawObj(objList);
    if ((obj != NULL) && (obj != objList->lastItem))
    {
        RemoveDrawObjFromList(objList, obj);         // pull obj out of list
        obj->prevObj = objList->lastItem;   // hook it in at the end
        obj->prevObj->nextObj = obj;
        objList->lastItem = obj;
        obj->nextObj = NULL;
    }
}


//--------------------------------------------------------------------------------------
// Return the first (front-to-back) object hit by docPt, or NULL.
// If hit, *outFlags contains which "grabber" handle has been hit, if any (numbered 1..nGrabbers).
// Note the use of a 1x1-pixel CGBitmapContext bmCtx for hit-testing. We keep it around and pass it in;
// but you could also create it on the spot.
CSkObjectPtr DrawObjListHitTesting(DrawObjListPtr objList, CGContextRef bmCtx, CGAffineTransform m, CGPoint windowCtxPt, CGPoint docPt, UInt32* grabNum)
{
    UInt32*		   baseAddr = (UInt32*)CGBitmapContextGetData(bmCtx);   // Assume 4 bytes per pixel!
    CSkObjectPtr	obj		= objList->firstItem;                       // traverse front to back
	Boolean			hit		= false;
	
    CGContextSaveGState(bmCtx);											// because we are temporarily changing the CTM
    CGContextTranslateCTM( bmCtx, -windowCtxPt.x, -windowCtxPt.y );     // move 1x1 bitmap context to "windowCtxPt"
    CGContextConcatCTM(bmCtx, m);                                       // apply document transform for drawing
    
    while ((obj != NULL) && !hit)
    {
		int		shapeType   = CSkShapeGetType(obj->shape);
        float   d			= 0.5 * obj->lineWidth;
        CGRect  cgR			= CSkShapeGetBounds(obj->shape);
		
		cgR = CGRectInset(cgR, -d, -d);
        
        if (CGRectContainsPoint(cgR, docPt))
        {
			cgR = CGRectInset(cgR, d, d);   // restore original shape bounds
			
            // If the obj is selected, check for a hit in the grabbers first since they overlap the obj
            if (obj->selected && (grabNum != NULL))
            {
                *grabNum = FindGrabberHit(obj->shape, docPt);
                if (*grabNum > 0)
                {
					fprintf(stderr, "0x%x: grabber %d hit\n", (uint)obj, (int)*grabNum);
					hit = true;
					break;                      // done - return obj
                }
             }

            if (shapeType == kRectShape)
            {
//				fprintf(stderr, "0x%x: Rectangle hit\n", (uint)obj);
				hit = true;
                break;                          // done - return obj
            }
            
            // Horizontal or vertical line? Hit if CGRectContainsPoint
            if  (   (shapeType == kLineShape)
                &&  ((cgR.size.height == 0) || (cgR.size.width == 0))
                )
            {
//				fprintf(stderr, "0x%x: Horz/Vert Line hit\n", (uint)obj);
				hit = true;
                break;                          // done - return obj
            }

            // If none of the above, draw the object into the bitmapContext, and check whether this changed the point
			cgR = CGRectInset(cgR, -d, -d);		// outset once more
			*baseAddr = 0;						// clear the pixel in bmCtx
            SetContextStateForDrawObject(bmCtx, obj);
            RenderCSkObject(bmCtx, obj, true);
            
            if (*baseAddr != 0)     // got it!
            {
				hit = true;
//				fprintf(stderr, "0x%x: BitmapContext hit\n", (uint)obj);
                break;                          // done - return obj
            }
			else
			{
//				fprintf(stderr, "no BitmapContext hit for (%f, %f) in (%f, %f, %f, %f)\n", 
//								docPt.x, docPt.y, 
//								cgR.origin.x, cgR.origin.y, cgR.size.width, cgR.size.height);
			}
        }
		
		if (!hit)
			obj = obj->nextObj;
    }
    
    // obj == NULL if there was no hit
    
    CGContextRestoreGState(bmCtx);
    return  obj;
}
