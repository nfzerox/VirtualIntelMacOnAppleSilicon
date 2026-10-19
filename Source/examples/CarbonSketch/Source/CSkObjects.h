/*
    File:       CSkObjects.h
        
    Contains:	Externally visible interface to CSkObject implementation.
                
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


#ifndef __CSKOBJECTS__
#define __CSKOBJECTS__

#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#include "CSkUtils.h"
#include "CSkShapes.h"

typedef struct CSkObject CSkObject, *CSkObjectPtr;  // struct CSkObject defined in QuartzDrawObjects.c

// The name changed from "DrawObj" to "CSkObject", during development.
// Currently, CSkObjects are limited to lines, rectangles, ovals and roundRectangles
// (see enumeration of shape selectors in CSkConstants.h); but obviously,
// we'll want to extand that in the future.
// CSkObjects are stored in a double-linked list, and drawn from back to front.


struct DrawObjList
{
    CSkObjectPtr  firstItem;
    CSkObjectPtr  lastItem;
};
typedef struct DrawObjList  DrawObjList, *DrawObjListPtr;


CSkObjectPtr    CreateDrawObj(WindowRef toolPalette, int shapeType);
CSkObjectPtr    CopyDrawObject(const CSkObject* obj);
void		ReleaseDrawObj(CSkObjectPtr drawObj);
void		ReleaseDrawObjList(DrawObjListPtr objList);
void		SetLineWidthOfSelecteds(DrawObjListPtr objListP, float lineWidth);
void		SetLineCapOfSelecteds(DrawObjListPtr objListP, CGLineCap lineCap);
void		SetLineJoinOfSelecteds(DrawObjListPtr objListP, CGLineJoin lineJoin);
void		SetLineStyleOfSelecteds(DrawObjListPtr objListP, int lineStyle);
void		SetStrokeColorOfSelecteds(DrawObjListPtr objListP, CGrgba* color);
void		SetStrokeAlphaOfSelecteds(DrawObjListPtr objListP, float alpha);
void		SetFillColorOfSelecteds(DrawObjListPtr objListP, CGrgba* color);
void		SetFillAlphaOfSelecteds(DrawObjListPtr objListP, float alpha);
void		SetDrawObjAttributesFromToolPalette(CSkObjectPtr obj, WindowRef toolPalette);
void		SetSelectedDrawObjAttributesFromToolPalette(DrawObjListPtr objListP, WindowRef toolPalette);

void		GetLineAttributes(const CSkObject* obj, float* width, CGLineCap* cap, CGLineJoin* join, int* style);

int			GetDrawObjShapeType( const CSkObject* drawObj );
CSkShape*   GetCSkObjectShape( const CSkObject* drawObj );
float		GetFillAlpha( const CSkObject* drawObj );
float		GetStrokeAlpha( const CSkObject* drawObj );
Boolean		IsDrawObjSelected( const CSkObject* drawObj );
void		SetDrawObjSelectState(CSkObjectPtr drawObj, Boolean selected);
void		CSkObjListSetSelectState(DrawObjListPtr objList, Boolean state);
CSkObjectPtr    FirstSelectedObject(const DrawObjList* drawObjListP);
void		CSkObjListSelectWithinRect(DrawObjList* objList, CGRect selectionRect);
void		SetContextStateForDrawObject(CGContextRef ctx, const CSkObject* obj);
void		RenderCSkObject ( CGContextRef ctx, const CSkObject* obj, Boolean drawSelection);
void		RenderDrawObjList( CGContextRef ctx, const DrawObjList* objListP, Boolean drawSelection);
void		RenderSelectedDrawObjs(CGContextRef ctx, const DrawObjList* objListP, float offsetX, float offsetY, float alpha);
void		MakeDrawObjTransparent(CSkObject* obj, float alpha);
void		MoveSelectedDrawObjs(DrawObjList* objListP, float offsetX, float offsetY);
CSkObjectPtr    DrawObjListHitTesting ( DrawObjListPtr objList, 
					CGContextRef bmCtx,
					CGAffineTransform m, 
					CGPoint windowCtxPt, 
					CGPoint docPt, 
					UInt32* outFlags);

void		AddDrawObjToList(DrawObjListPtr objList, CSkObjectPtr obj);
void		RemoveSelectedDrawObjs(DrawObjListPtr objList);
void		DuplicateSelectedDrawObjs(DrawObjListPtr objList, CGPoint offset);
void		MoveObjectForward(DrawObjListPtr objList);
void		MoveObjectToFront(DrawObjListPtr objList);
void		MoveObjectBackward(DrawObjListPtr objList);
void		MoveObjectToBack(DrawObjListPtr objList);

#endif
