/*
    File:       CSkShapes.c
        
    Contains:	Definition of (opaque) CSkShape structure, and various related routines.
                The idea is to facilitate future expansion by additional shapes, without
                going object-oriented with polymorphism (which it should still suggest and
                facilitate as well).
                
    Disclaimer:	IMPORTANT:  This Apple software is supplied to you by Apple Computer, Inc.
                ("Apple") in consideration of your agreement to the following terms, and your
                use, installation, modification or redistribution of this Apple software
                constitutes acceptance of these terms.  If you do not agree with these terms,
                please do not use, install, modify or redistribute this Apple software.

                In consideration of your agreement to abide by the following terms, and subject
                to these terms, Apple grants you a personal, non-exclusive license, under AppleÕs
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


#include "CSkShapes.h"
#include "CSkConstants.h"

// The information about a CSkObject's geometric shape has been factored out into this separate file,
// for good coding practice, and to make room for future extensions.
// As it stands, CSkShapes are fixed-size allocations; but this might change in the future.

struct CSkRRect 
{
    CGRect  bounds;
    float   rX;
    float   rY;
};
typedef struct CSkRRect CSkRRect;

struct CSkShape 
{
    int     shapeType;
    union   {
        CGPoint twoPts[2];  // for lines
        CGRect  bounds;		// for rects, ovals (and RRects)
        CSkRRect rrect;
        // ....
    } u;
};


ByteCount CSkShapeSize(void)
{
    return sizeof(CSkShape);
}

//-------------------------------------------------------- The expected accessors


int CSkShapeGetType(const CSkShape* sh)
{
    return sh->shapeType;
}


void CSkShapeGetLine(const CSkShape* sh, CGPoint* a, CGPoint* b)
{
    if (sh->shapeType == kLineShape)
    {
        *a = sh->u.twoPts[0];
        *b = sh->u.twoPts[1];
    }
    else
    {
        fprintf(stderr, "GetLinePoints: sh->shapeType is %d, should be %d\n", sh->shapeType, kLineShape);
    }
}


CGRect CSkShapeGetBounds(const CSkShape* sh)
{
    CGRect bounds;
    
    switch (sh->shapeType)
    {
        case kLineShape: 
            bounds.origin = sh->u.twoPts[0];
            bounds.size = CGSizeMake(sh->u.twoPts[1].x - sh->u.twoPts[0].x, sh->u.twoPts[1].y - sh->u.twoPts[0].y);
            bounds = CGRectStandardize(bounds);
            break;
                
        case kRectShape:
        case kOvalShape:
            bounds = sh->u.bounds;
            break;
                
        case kRRectShape:
            bounds = sh->u.rrect.bounds;
            break;
    }
    return bounds;
}

void CSkShapeGetRRectRadii(const CSkShape* sh, float* rX, float* rY)
{
    if (sh->shapeType == kRRectShape)
    {
        *rX = sh->u.rrect.rX;
        *rY = sh->u.rrect.rY;
    }
    else
    {
        fprintf(stderr, "CSkShapeGetRRectRadii: shapeType is %d, should be %d\n", sh->shapeType, kRRectShape);
    }
}

void CSkShapeSetBounds(CSkShape* sh, CGRect rect)
{
    if ((sh->shapeType >= kRectShape) && (sh->shapeType <= kRRectShape))
    {
        if (sh->shapeType != kRRectShape)
            sh->u.bounds = rect;
        else
            sh->u.rrect.bounds = rect;
    }
    else
    {
        fprintf(stderr, "CSkShapeSetBounds: shapeType %d not allowed\n", sh->shapeType);
    }
}

void CSkShapeSetLine(CSkShape* sh, CGPoint a, CGPoint b)
{
    sh->shapeType = kLineShape;
    sh->u.twoPts[0] = a;
    sh->u.twoPts[1] = b;
}

static void CSkShapeSetRect(CSkShape* sh, CGRect rect)
{
    sh->shapeType = kRectShape;
    sh->u.bounds = rect;
}

static void CSkShapeSetOval(CSkShape* sh, CGRect rect)
{
    sh->shapeType = kOvalShape;
    sh->u.bounds = rect;
}

static void CSkShapeSetRRect(CSkShape* sh, CGRect rect, float rX, float rY)
{
    sh->shapeType = kRRectShape;
    sh->u.rrect.bounds = rect;
    sh->u.rrect.rX = rX;
    sh->u.rrect.rY = rY;
}

//-------------------------------------------------------- 
// Allocate and initialize
CSkShapePtr CSkShapeCreate(int shapeType)
{
    const float cR = 16.0;      // default rounding radius for RRects
    CSkShapePtr sh = (CSkShapePtr)malloc(sizeof(CSkShape));
    switch (shapeType)
    {
        case kLineShape:    CSkShapeSetLine(sh, CGPointZero, CGPointZero);  break;
        case kRectShape:    CSkShapeSetRect(sh, CGRectZero);                break;
        case kOvalShape:    CSkShapeSetOval(sh, CGRectZero);                break;
        case kRRectShape:   CSkShapeSetRRect(sh, CGRectZero, cR, cR);       break;
    }
    return sh;
}

// Deallocate
void CSkShapeRelease(CSkShape* sh)
{
    free(sh);
}


//--------------------------------------------------------------
// (Cannot think of any non-redundant comment ...)
void CSkShapeOffset(CSkShape* sh, float offsetX, float offsetY)
{
    switch (sh->shapeType)
    {
        case kLineShape:
        {
            CGPoint a, b;
            CSkShapeGetLine(sh, &a, &b);
            a.x += offsetX;
            a.y += offsetY;
            b.x += offsetX;
            b.y += offsetY;
            CSkShapeSetLine(sh, a, b);
        }
        break;
        
        case kRectShape:
        case kOvalShape:
        case kRRectShape:
        {
            CGRect bounds = CSkShapeGetBounds(sh);
            bounds = CGRectOffset(bounds, offsetX, offsetY);
            CSkShapeSetBounds(sh, bounds);
        }
        break;
    }
}


//--------------------------------------------------------------------------------------
// Here come the "grabbers":
// little squares to indicate selection and to grab for resizing.
// Lines are a special case (only start and end-point); all other shapes
// currently get 8 grabbers along the bounding box (four at corners,
// four at mid-sides).
// Once we add arcs or even general paths to the repertory, this grabbing
// business will get much more complicated!

enum {
    grGrabNone      = 0,
    grBottomLeft    = 1,
    grBottomMiddle  = 2,
    grBottomRight   = 3,
    grLeftMiddle    = 4,
    grRightMiddle   = 5,
    grTopLeft       = 6,
    grTopMiddle     = 7,
    grTopRight      = 8,
};

// Iterates through the obj's little grabber rectangles. Pass in 0 for previousGrabNum at 
// the beginning; it gets incremented on return. Keep going until return value is false.
// Called from "FindGrabberHit", below, and also from RenderCSkObject when they need
// to be drawn.
Boolean NextGrabberRect(const CSkShape* sh, UInt32* previousGrabNum, CGRect* grabRect)
{
    const float kGrabberSize    = 6.0;
    const float kHalfGrabSize   = 3.0;
    CGRect  grabR = CGRectMake( -kHalfGrabSize, -kHalfGrabSize, kGrabberSize, kGrabberSize);
    UInt32  grNum = *previousGrabNum + 1;
    
    if (sh->shapeType == kLineShape)
    {
        if (grNum > 2)
        {
            *previousGrabNum = 0;
            return false;
        }
        *grabRect = CGRectOffset( grabR, sh->u.twoPts[grNum - 1].x, sh->u.twoPts[grNum - 1].y );
    }
    else
    {
        const int   xD[] = { -1, 0, 1, 2, 0, 2, 0, 1, 2 };  // index 0 unused; grabberpoints 1...8
        const int   yD[] = { -1, 0, 0, 0, 1, 1, 2, 2, 2 };
        CGRect  bounds;
        float   halfWidth, halfHeight;
        
        if (grNum > 8)
        {
            *previousGrabNum = 0;
            return false;
        }
        bounds      = CSkShapeGetBounds(sh);
        halfWidth   = 0.5 * CGRectGetWidth(bounds);
        halfHeight  = 0.5 * CGRectGetHeight(bounds);
        *grabRect   = CGRectOffset(grabR, bounds.origin.x + xD[grNum] * halfWidth,
                                          bounds.origin.y + yD[grNum] * halfHeight );
    }
    
    *previousGrabNum = grNum;
    return true;
}

//--------------------------------------------------------------------------------------
UInt32 FindGrabberHit(const CSkShape* shape, CGPoint pt)
{
    CGRect  grabRect;
    UInt32  grabNum     = 0;
    
    while (NextGrabberRect(shape, &grabNum, &grabRect))
    {
        if (CGRectContainsPoint(grabRect, pt))
            break;
    }
    return grabNum;
}

//--------------------------------------------------------------------------------------
// If the movement of the "dragger" causes size.width or size.height to become negative,
// the dragger changes its number! Don't know if there is a less messy way to deal with it.

void CSkShapeResize(CSkShape* sh, UInt32* dragger, CGPoint newPt)
{	
    if (sh->shapeType == kLineShape)
    {
        CGPoint a, b;
        CSkShapeGetLine(sh, &a, &b);
        if (*dragger == 1)   // we only have dragger indices 1 and 2
            a = newPt;
        else
            b = newPt;
        CSkShapeSetLine(sh, a, b);
    }
    else 
    {
        CGRect  bounds = CSkShapeGetBounds(sh);
        float   dx, dy;
		
        switch (*dragger)
        {
            case grBottomLeft:
                dx = newPt.x - bounds.origin.x;
                dy = newPt.y - bounds.origin.y;
                bounds.origin.x += dx;
                bounds.origin.y += dy;
                bounds.size.width -= dx;
                bounds.size.height -= dy;
				if (bounds.size.width < 0)
					*dragger = grBottomRight;
				if (bounds.size.height < 0)
					*dragger = grTopLeft;
                break;
                
            case grBottomMiddle:
                dy = newPt.y - bounds.origin.y;
                bounds.origin.y += dy;
                bounds.size.height -= dy;
				if (bounds.size.height < 0)
					*dragger = grTopMiddle;
                break;
				
            case grBottomRight:
                dx = newPt.x - (bounds.origin.x + bounds.size.width);
                dy = newPt.y - bounds.origin.y;
                bounds.size.width += dx;
                bounds.origin.y += dy;
                bounds.size.height -= dy;
				if (bounds.size.width < 0)
					*dragger = grBottomLeft;
				if (bounds.size.height < 0)
					*dragger = grTopRight;
				break;
				
            case grLeftMiddle:
                dx = newPt.x - bounds.origin.x;
                bounds.origin.x += dx;
                bounds.size.width -= dx;
				if (bounds.size.width < 0)
					*dragger = grRightMiddle;
                break;
				
            case grRightMiddle:
                dx = newPt.x - (bounds.origin.x + bounds.size.width);
                bounds.size.width += dx;
				if (bounds.size.width < 0)
					*dragger = grLeftMiddle;
                break;
				
            case grTopLeft:
                dx = newPt.x - bounds.origin.x;
                dy = newPt.y - (bounds.origin.y + bounds.size.height);
                bounds.origin.x += dx;
                bounds.size.width -= dx;
                bounds.size.height += dy;
				if (bounds.size.width < 0)
					*dragger = grTopRight;
				if (bounds.size.height < 0)
					*dragger = grBottomLeft;
				break;
				
            case grTopMiddle:
                dy = newPt.y - (bounds.origin.y + bounds.size.height);
                bounds.size.height += dy;
				if (bounds.size.height < 0)
					*dragger = grBottomMiddle;
                break;
				
            case grTopRight:
                dx = newPt.x - (bounds.origin.x + bounds.size.width);
                dy = newPt.y - (bounds.origin.y + bounds.size.height);
                bounds.size.width += dx;
                bounds.size.height += dy;
				if (bounds.size.width < 0)
					*dragger = grTopLeft;
				if (bounds.size.height < 0)
					*dragger = grBottomRight;
				break;
        }
        CSkShapeSetBounds(sh, CGRectStandardize(bounds));
    }
}	// CSkShapeResize


