/*
    File:       main.c
    
    Contains:	Initialization code and RunApplicationEventLoop.

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

#include "CSkWindow.h"
#include "CSkToolPalette.h"
#include "CSkConstants.h"

// Keep our nibRef around as global (CreateNibReference is expensive)
IBNibRef    gOurNibRef;

//-----------------------------------------------------------------------------------------------------------------------
static	pascal	OSStatus AppEventHandlerProc( EventHandlerCallRef inCallRef, EventRef inEvent, void* inUserData )
{
    #pragma unused(inCallRef, inUserData)
    OSStatus    err         = eventNotHandledErr;
    UInt32      eventClass  = GetEventClass(inEvent);
    UInt32      eventKind   = GetEventKind(inEvent);
    
    if ( (eventClass == kEventClassCommand) && (eventKind == kEventCommandProcess) )
    {
        HICommand command;

        GetEventParameter( inEvent, kEventParamDirectObject, typeHICommand, NULL, sizeof(HICommand), NULL, &command );
        
        switch ( command.commandID)
        {
            case kHICommandNew:
                NewCSkWindow(gOurNibRef, CSkToolPalette(gOurNibRef));
				err = noErr;
				break;
            
            case kHICommandAbout:
            {
    //          DisplayAboutBox();  -- the built-in one is good enough, for now
            }
            break;
            
            case kHICommandOpen:
            {
    //          OpenFiles();	-- real soon now ...
            }
            break;
        }
    }
    return err;
}

// --------------------------------------------------------------------------------------------------------------
static pascal OSErr	DoOpenApp(const AppleEvent* inputEvent, AppleEvent* outputEvent, SInt32 handlerRefCon)
{
#pragma unused (inputEvent, outputEvent, handlerRefCon)
	return NewCSkWindow(gOurNibRef, CSkToolPalette(gOurNibRef));
} // DoOpenApp


//---------------------------------------------------------------------------------------------
int main()
{
    static const EventTypeSpec  sApplicationEvents[] = {{ kEventClassCommand, kEventCommandProcess }};
    OSErr                       err;
    
    err = CreateNibReference(CFSTR("CarbonSketch"), &gOurNibRef );
    require_noerr( err, CantGetNibRef );

    err = SetMenuBarFromNib( gOurNibRef, CFSTR("MenuBar") );
    require_noerr( err, SetMenuBarFromNib_FAILED );

	AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, DoOpenApp, 0, false);
//	AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, DoOpenDocument, 0, false);
//	AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, DoPrintDocument, 0, false);
//	AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, DoQuitApp, 0, false);

    InstallApplicationEventHandler( NewEventHandlerUPP(AppEventHandlerProc), 
                                    GetEventTypeCount(sApplicationEvents), 
                                    sApplicationEvents, 0, NULL );

    RunApplicationEventLoop();

SetMenuBarFromNib_FAILED:
    DisposeNibReference(gOurNibRef);

CantGetNibRef:
    return err;
}

