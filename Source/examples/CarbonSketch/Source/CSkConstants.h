/*
    File:       CSkConstants.h
        
    Contains:	Application-wide constant definitions.

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


#ifndef __CSKCONSTANTS__
#define __CSKCONSTANTS__

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

// Geometry (shape) selectors - the items 1 through 4
enum {
	kUndefined		= 0,
	kLineShape		= 1,
	kRectShape		= 2,
	kOvalShape		= 3,
	kRRectShape		= 4
};
// maybe we want to add later polygons, regular closed polygons, quadratics, cubics, or even general paths?


// Window controls

enum {
    kControlSignatureMainWindow = 'CSkM',
    kScrollHorizontalID		= 1,
    kScrollVerticalID		= 2,
    kScalePopupID		= 3
};

//---------------------------------------------------------------------------------------------
//	Floating palette buttons - cf. CSkUtils, MapToolToShape()
//---------------------------------------------------------------------------------------------

enum {
    kControlSignaturePalette	= 'Tool',
    kArrowTool			= 1,
    kLineTool			= 2,
    kRectTool			= 3,
    kOvalTool			= 4,
    kRRectTool			= 5,
    kLineWidthPopup		= 6,
    kLineCapPopup		= 7,
    kLineJoinPopup		= 8,
    kLineStylePopup		= 9,
    kStrokeColorBtn		= 10,
    kFillColorBtn		= 11,
    kStrokeAlphaSlider		= 12,
    kFillAlphaSlider		= 13
};

// Line Width menu items
enum {
    kCmdWidthNone		= 'WdNn',
    kCmdWidthOne		= 'Wd01',
    kCmdWidthTwo		= 'Wd02',
    kCmdWidthFour		= 'Wd04',
    kCmdWidthEight		= 'Wd08',
    kCmdWidthSixteen		= 'Wd16',
    kCmdWidthThinner		= 'Wd< ',
    kCmdWidthThicker		= 'Wd> ',
    kMakeItThinner		= -2,
    kMakeItThicker		= -1,
    kLastLineWidthItem		= 8,
    kCmdStrokeColor		= 'StrC',
    kCmdFillColor		= 'FilC'
};

// Line Cap commandIDs
enum {
    kCmdCapButt			= 'Cap1',
    kCmdCapRound		= 'Cap2',
    kCmdCapSquare		= 'Cap3'
};


// Line Join menu items
enum {
    kCmdJoinMiter		= 'Jon1',
    kCmdJoinRound		= 'Jon2',
    kCmdJoinBevel		= 'Jon3'
};

// Line Style menu items
enum
{
    kStyleSolid			= 1,
    kStyleDashed		= 2,
    kCmdStyleSolid		= 'Stl1',
    kCmdStyleDashed		= 'Stl2'
};

// Scalefactor menu item commands
enum
{
    kCmdScale50			= 'S050',
    kCmdScale100		= 'S100',
    kCmdScale200		= 'S200',
    kMaxScaleMenuItem		= 3
};

// Arrange menu item commands
enum 
{
    kCmdMoveForward	    = 'Fwrd',
    kCmdMoveToFront	    = 'Frnt',
    kCmdMoveBackward	    = 'Bwrd',
    kCmdMoveToBack	    = 'Back'
};

// Other menu or window commands
enum
{   
    kCmdWritePDF            = 'WPDF',
    kCmdDuplicate           = 'Dupl',
    kCmdLineWidthChanged    = 'LwCh',
    kCmdLineCapChanged      = 'LcCh',
    kCmdLineJoinChanged     = 'LjCh',
    kCmdLineStyleChanged    = 'LsCh',
    kCmdStrokeColorChanged  = 'SCCh',
    kCmdFillColorChanged    = 'FCCh',
    kCmdStrokeAlphaChanged  = 'Ssld',
    kCmdFillAlphaChanged    = 'Fsld'
};


#endif
