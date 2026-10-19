/*
    File:       CSkPrinting.c
    
    Contains:	Implementation of printing support for CarbonSketch

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


#include "CSkPrinting.h"
#include "CSkConstants.h"
#include "CSkWindow.h"
// also includes "CSkDocStorage.h"
// also includes "CSkObjects.h"

#include "NavServicesHandling.h"

//-----------------------------------------------------------------------------------------------------------------------
static OSStatus MyCreatePageFormat(PMPrintSession printSession, PMPageFormat *pageFormat)
{
        OSStatus status = PMCreatePageFormat(pageFormat);
            
        //  Note that PMPageFormat is not session-specific, but calling
        //  PMSessionDefaultPageFormat assigns values specific to the printer
        //  associated with the current printing session.
        if ((status == noErr) && (*pageFormat != kPMNoPageFormat))
            status = PMSessionDefaultPageFormat(printSession, *pageFormat);
	    
	return status;
}

//-----------------------------------------------------------------------------------------------------------------------
// (Borrowed from /Developer/Examples/Printing/App/)
static OSStatus DoPageSetupDialog(PMPrintSession printSession, PMPageFormat* pageFormat, Handle* flattendedPageFormat)
{
    OSStatus	status = noErr;
    
    if (*pageFormat == kPMNoPageFormat)    // Set up a valid PageFormat object
    {
		MyCreatePageFormat(printSession, pageFormat);
    }
    else
    {
        status = PMSessionValidatePageFormat(printSession, *pageFormat, kPMDontWantBoolean);
    }

    if (status == noErr)            //	Display the Page Setup dialog
    {
        Boolean accepted;
        status = PMSessionPageSetupDialog(printSession, *pageFormat, &accepted);
        if (status == noErr && !accepted)
            status = kPMCancel;		// user clicked Cancel button
    }	
                            
    //	If the user did not cancel, flatten and save the PageFormat object with our document
    if ((status == noErr) && (flattendedPageFormat != NULL))
    {
//        status = FlattenAndSavePageFormat(*pageFormat);
        status = PMFlattenPageFormat(*pageFormat, flattendedPageFormat);
    }
    
    return status;
} // DoPageSetupDialog

//-----------------------------------------------------------------------------------------------------------------------
static OSStatus	DetermineNumberOfPagesInDoc(PMPageFormat pageFormat, UInt32* numPages)
{
    PMRect	pageRect;
    OSStatus	status  = PMGetAdjustedPageRect(pageFormat, &pageRect);
    check(status == noErr);

    *numPages = 1;  // will do better some time in the future ...

    return status;
    
} // DetermineNumberOfPagesinDoc

//-----------------------------------------------------------------------------------------------------------------------
static OSStatus DoPrintLoop(DocStoragePtr docStP, PMPrintSession printSession, PMPageFormat pageFormat, PMPrintSettings printSettings)
{
    OSStatus        status = noErr, tempErr;
    CGContextRef    printingCtx;
    UInt32          realNumberOfPagesinDoc,
                    pageNumber,
                    firstPage,
                    lastPage;
    CFStringRef     jobName;    // use window title

    status = CopyWindowTitleAsCFString(docStP->ownerWindow, &jobName);

    status = PMSetJobNameCFString(printSettings, jobName);
    CFRelease (jobName);

    //	Get the user's Print dialog selection for first and last pages to print.
    if (status == noErr)
    {
        status = PMGetFirstPage(printSettings, &firstPage);
        if (status == noErr)
            status = PMGetLastPage(printSettings, &lastPage);
    }

    //	Check that the selected page range does not exceed the actual number of pages in the document.
    if (status == noErr)
    {
        status = DetermineNumberOfPagesInDoc(pageFormat, &realNumberOfPagesinDoc);
        if (realNumberOfPagesinDoc < lastPage)
            lastPage = realNumberOfPagesinDoc;
    }

    //	Before executing the print loop, tell the Carbon Printing Manager which pages
    //	will be spooled so that the progress dialog can reflect an accurate page count.
	
    if (status == noErr)
        status = PMSetFirstPage(printSettings, firstPage, false);
    if (status == noErr)
        status = PMSetLastPage(printSettings, lastPage, false);
    	
    //	Note, we don't have to worry about the number of copies.  The printing
    //	manager handles this.  So we just iterate through the document from the
    //	first page to be printed, to the last.
    
    // Now, tell the printing system that we promise never to use any Quickdraw calls:
    {
        CFStringRef s[1] = { kPMGraphicsContextCoreGraphics };
        CFArrayRef  graphicsContextsArray = CFArrayCreate(NULL, (const void**)s, 1, &kCFTypeArrayCallBacks);
        PMSessionSetDocumentFormatGeneration(printSession, kPMDocumentFormatPDF, graphicsContextsArray, NULL);
        CFRelease(graphicsContextsArray);
    }
    
    if (status == noErr)
    {
        status = PMSessionBeginDocument(printSession, printSettings, pageFormat);
        check(status == noErr);
        if (status == noErr)
        {
            pageNumber = firstPage;
        
            // Note that we check PMSessionError immediately before beginning a new page.
            // This handles user cancelling appropriately. Also, if we got an error on 
            // any previous iteration of the print loop, we break out of the loop.
            while ( (pageNumber <= lastPage) && (status == noErr) && (PMSessionError(printSession) == noErr) )
            {
                status = PMSessionBeginPage(printSession, pageFormat, NULL);
                check(status == noErr);
                if (status == noErr)
                {
                    status = PMSessionGetGraphicsContext(printSession, kPMGraphicsContextCoreGraphics, (void**)&printingCtx);
                    check(status == noErr);
                    if (status == noErr) 
                    {
						// Will have to do some work to position/scale objects correctly on page, and to support multi-page documents
						CGRect pageSize = CGRectMake(0, 0, docStP->docSize.h, docStP->docSize.v);
						DrawIntoPDFPage(printingCtx, pageSize, docStP, pageNumber);
                    }
                                    
                    tempErr = PMSessionEndPage(printSession);
                    if(status == noErr)
                        status = tempErr;
                }
                pageNumber++;
            } // end while loop
                    
            // Close the print job.  This dismisses the progress dialog on Mac OS X.
            tempErr = PMSessionEndDocument(printSession);
            if (status == noErr)
                status = tempErr;
        }
    }
            
    //	Only report a printing error once we have completed the print loop. This ensures
    //	that every PMBeginXXX call that returns no error is followed by a matching PMEndXXX
    //  call, so the Printing Manager can release all temporary memory and close properly.
    tempErr = PMSessionError(printSession);
    if(status == noErr)
        status = tempErr;
/*
    if ((status != noErr) && (status != kPMCancel))
        PostPrintingErrors(status);
*/		
    return status;
} // DoPrintLoop


//-----------------------------------------------------------------------------------------------------------------------
// (Borrowed from /Developer/Examples/Printing/App/)
static OSStatus DoPrintDialog(PMPrintSession printSession, PMPageFormat pageFormat, PMPrintSettings* printSettings)
{
    OSStatus	status = noErr;
    Boolean     accepted;
    UInt32      realNumberOfPagesinDoc;
    
    //	In this sample code the caller provides a valid PageFormat reference but in
    //	your application you may want to load and unflatten the PageFormat object
    //	that was saved at PageSetup time.  See LoadAndUnflattenPageFormat below.
    
    //	Set up a valid PrintSettings object.
    if (*printSettings == kPMNoPrintSettings)
    {
        status = PMCreatePrintSettings(printSettings);	
        check(status == noErr);
        // Note that PMPrintSettings is not session-specific, but calling
        // PMSessionDefaultPrintSettings assigns values specific to the printer
        // associated with the current printing session.
        if ((status == noErr) && (*printSettings != kPMNoPrintSettings))
            status = PMSessionDefaultPrintSettings(printSession, *printSettings);
        check(status == noErr);
    }
    else
    {
        status = PMSessionValidatePrintSettings(printSession, *printSettings, kPMDontWantBoolean);
        check(status == noErr);
    }
    
    // Before displaying the Print dialog, we calculate the number of pages in the
    // document.  On Mac OS X this is useful because we can prime the Print dialog
    // with the actual page range of the document and prevent the user from entering
    // out-of-range numbers.  This is not possible on Mac OS 8 and 9 because the driver,
    // not the printing manager, controls the page range fields in the Print dialog.

    // Calculate the number of pages required to print the entire document.
    if (status == noErr)
        status = DetermineNumberOfPagesInDoc(pageFormat, &realNumberOfPagesinDoc);

    // Set a valid page range before displaying the Print dialog
    if (status == noErr)
        status = PMSetPageRange(*printSettings, 1, realNumberOfPagesinDoc);
    check(status == noErr);

    //	Display the Print dialog.
    if (status == noErr)
    {
        status = PMSessionPrintDialog(printSession, *printSettings, pageFormat, &accepted);
        check(status == noErr);
        if (status == noErr && !accepted)
            status = kPMCancel;		// user clicked Cancel button
    }

    return status;
	
}   // DoPrintDialog


//-----------------------------------------------------------------------------------------------------------------------
void ProcessPrintCommand(DocStoragePtr docStP, UInt32 commandID)
{
    OSStatus err = noErr;
    PMPrintSession  printSession = NULL;
    
    if ( PMCreateSession(&printSession) == noErr )
    {
		if (commandID == kHICommandPageSetup)
		{
			DoPageSetupDialog(printSession, &docStP->pageFormat, &docStP->flattenedPageFormat);
		}
		else if (commandID == kHICommandPrint)
		{
			if (docStP->pageFormat == NULL)
			{
				err = MyCreatePageFormat(printSession, &docStP->pageFormat);
			}

			if (DoPrintDialog(printSession, docStP->pageFormat, &docStP->printSettings) == noErr)
			{
				DoPrintLoop(docStP, printSession, docStP->pageFormat, docStP->printSettings);
			}
		}
		PMRelease(printSession);
    }
    else
    {
		fprintf(stderr, "PMCreateSession FAILED\n");
    }
}   // ProcessPrintCommand

