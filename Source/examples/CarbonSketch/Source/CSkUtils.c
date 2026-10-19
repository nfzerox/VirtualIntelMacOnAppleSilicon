/*
    File:       CSkUtils.h
    
    Contains:	Some utility routines for CarbonSketch

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


#include "CSkUtils.h"

//--------------------------------------------------
void SendWindowCloseEvent( WindowRef window )
{
    EventRef event;
    
    CreateEvent( NULL, kEventClassWindow, kEventWindowClose, GetCurrentEventTime(), kEventAttributeUserEvent, &event );
    SetEventParameter( event, kEventParamDirectObject, typeWindowRef, sizeof(window), &window );
    SendEventToWindow( event, window );
    ReleaseEvent( event );
}

//--------------------------------------------------
void SendWindowCommandEvent( WindowRef window, HICommand* command )
{
    EventRef event;
    
    CreateEvent( NULL, kEventClassCommand, kEventCommandProcess, GetCurrentEventTime(), kEventAttributeUserEvent, &event );
    SetEventParameter( event, kEventParamDirectObject, typeHICommand, sizeof(HICommand), command );
    SendEventToWindow( event, window );
    ReleaseEvent( event );
}

//--------------------------------------------------
void SendControlEventHit(ControlRef control)
{
    EventRef event;
    
    CreateEvent( NULL, kEventClassControl, kEventControlHit, GetCurrentEventTime(), kEventAttributeUserEvent, &event );
    SetEventParameter( event, kEventParamDirectObject, typeControlRef, sizeof(control), control );
    SendEventToEventTarget( event, GetControlEventTarget(control) );
    ReleaseEvent( event );
}

//---------------------------------------------------------------------------------
void ConvertRGBColorToCGrgba(const RGBColor* inRGB, float alpha, CGrgba* outCGrgba)
{
    outCGrgba->r = (float)inRGB->red   / 65535.0;
    outCGrgba->g = (float)inRGB->green / 65535.0;
    outCGrgba->b = (float)inRGB->blue  / 65535.0;
    outCGrgba->a = alpha;
}

static void ConvertCGrgbaToRGB(const CGrgba* inCGrgba, RGBColor* outRGB)
{
    outRGB->red     = 65535 * inCGrgba->r;
    outRGB->green   = 65535 * inCGrgba->g;
    outRGB->blue    = 65535 * inCGrgba->b;
}

#define USECALCOLOR 1
//-------------------------------------------
OSStatus PickSomeColor(CGrgba* theColor)
{
    RGBColor            rgb;
    NColorPickerInfo    info;
    OSStatus            err;
#if USECALCOLOR
    static CMProfileRef genericProfileRef = NULL;
#endif    
    
    memset(&info, 0, sizeof(NColorPickerInfo));
    ConvertCGrgbaToRGB(theColor, &rgb);

    info.placeWhere         = kCenterOnMainScreen;
    info.flags              = kColorPickerDialogIsMoveable | kColorPickerDialogIsModal;
    info.theColor.color.rgb.red     = rgb.red;
    info.theColor.color.rgb.green   = rgb.green;
    info.theColor.color.rgb.blue    = rgb.blue;

#if USECALCOLOR
    if (genericProfileRef == NULL)
    {
		/*  We only call OpenGenericProfile once. It returns a reference that
			we "own". Since we hang on to that reference for the life of this application we
			don't close it after we are done in this function; we keep it in a static variable
			so we don't have to get a reference at any later time.
		*/
		genericProfileRef = OpenGenericProfile();
    }
	
    info.dstProfile = genericProfileRef;
#endif

    err = NPickColor(&info);
    require_noerr(err, NPickColorFAILED);
    
    if ((err == noErr) && info.newColorChosen)
    {
        rgb.red     = info.theColor.color.rgb.red;
        rgb.green   = info.theColor.color.rgb.green;
        rgb.blue    = info.theColor.color.rgb.blue;
        ConvertRGBColorToCGrgba(&rgb, theColor->a, theColor);
    }
            
NPickColorFAILED:
    return err;
}

//---------------------------------------------------------------------------------------------
void  CGRectToQDRect(const int windowHeight, const CGRect cgRect, Rect* outQDRect)
{
    outQDRect->left = cgRect.origin.x;
    outQDRect->top  = windowHeight - (cgRect.origin.y + cgRect.size.height);
    outQDRect->right = cgRect.origin.x + cgRect.size.width;
    outQDRect->bottom  = windowHeight - cgRect.origin.y;
}


//---------------------------------------------------------------------------------------------
void QDPortToCGCoordinates(Point* inPtArray, Point* outPtArray, int numPoints, Rect* portBounds)
{
    int i;
    int height = portBounds->bottom - portBounds->top;
    
    for (i = 0; i < numPoints; ++i)
    {
        outPtArray[i].v = height - inPtArray[i].v;
        outPtArray[i].h = inPtArray[i].h;
    }
}

//---------------------------------------------------------------------------------------------
// Map kLineTool, kRectTool, kOvalTool, kRRectTool to kLineShape, kRectShape, kOvalShape, kRRectShape
int MapToolToShape(int toolID)
{
	return toolID - 1;
}

#define	kGenericRGBProfilePathStr       "/System/Library/ColorSync/Profiles/Generic RGB Profile.icc"
/*
    This function locates, opens, and returns the profile reference for the calibrated 
    Generic RGB color space. It is up to the caller to call CMCloseProfile when done
    with the profile reference this function returns.
*/
CMProfileRef OpenGenericProfile(void)
{
    static CMProfileRef cachedRGBProfileRef = NULL;
    
    // we only create the profile reference once
    if (cachedRGBProfileRef == NULL)
    {
		OSStatus 			err;
		CMProfileLocation 	loc;
	
		loc.locType = cmPathBasedProfile;
		strcpy(loc.u.pathLoc.path, kGenericRGBProfilePathStr);
	
		err = CMOpenProfile(&cachedRGBProfileRef, &loc);
		
		if (err != noErr)
		{
			cachedRGBProfileRef = NULL;
			// log a message to the console
			fprintf(stderr, "couldn't open generic profile due to error %d\n", (int)err);
		}
    }

    if (cachedRGBProfileRef)
    {
		// clone the profile reference so that the caller has their own reference, not our cached one
		CMCloneProfileRef(cachedRGBProfileRef);   
    }

    return cachedRGBProfileRef;
}

/*
    Return the generic RGB color space. This is a 'get' function and the caller should
    not release the returned value unless the caller retains it first. Usually callers
    of this routine will immediately use the returned colorspace with CoreGraphics
    so they typically do not need to retain it themselves.
    
    This function creates the generic RGB color space once and hangs onto it so it can
    return it whenever this function is called.
*/
CGColorSpaceRef GetGenericRGBColorSpace(void)
{
    static CGColorSpaceRef genericRGBColorSpace = NULL;

	if (genericRGBColorSpace == NULL)
	{
		CMProfileRef genericRGBProfile = OpenGenericProfile();
	
		if (genericRGBProfile)
		{
			genericRGBColorSpace = CGColorSpaceCreateWithPlatformColorSpace(genericRGBProfile);
			if (genericRGBColorSpace == NULL)
				fprintf(stderr, "couldn't create the generic RGB color space\n");
			
			// we opened the profile so it is up to us to close it
			CMCloseProfile(genericRGBProfile); 
		}
	}
    return genericRGBColorSpace;
}

//-------------------------------------------------------------
PasteboardRef GetPasteboard(void)
{
	static PasteboardRef sPasteboard = NULL;
	
	if (sPasteboard == NULL)
	{
		PasteboardCreate( kPasteboardClipboard, &sPasteboard );
		if (sPasteboard == NULL)
		{
			fprintf(stderr, "PasteboardCreate failed - I wonder why?!\n");
		}
	}
	return sPasteboard;
}
