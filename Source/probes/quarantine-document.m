// Small, dependency-free PDF for real guest Gatekeeper/Preview qualification.
// Host tool only; it does not remove quarantine or alter security settings.
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=2){fprintf(stderr,"Usage: quarantine-document NEW_OUTPUT.pdf\n");return 2;}
    NSMutableData *data=[NSMutableData data];
    CGDataConsumerRef consumer=CGDataConsumerCreateWithCFData((__bridge CFMutableDataRef)data);
    CGRect page=CGRectMake(0,0,612,792);
    CGContextRef context=CGPDFContextCreate(consumer,&page,NULL);
    CGDataConsumerRelease(consumer);
    if(!context)return 1;
    CGPDFContextBeginPage(context,NULL);
    CGContextSetRGBFillColor(context,1,1,1,1);CGContextFillRect(context,page);
    CGContextSetRGBFillColor(context,0.05,0.3,0.8,1);
    CGContextFillRect(context,CGRectMake(72,500,468,180));
    CGContextSetRGBFillColor(context,0.15,0.7,0.3,1);
    CGContextFillEllipseInRect(context,CGRectMake(216,230,180,180));
    CGPDFContextEndPage(context);CGPDFContextClose(context);CGContextRelease(context);
    int fd=open(argv[1],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0){perror("Create new PDF");return 1;}
    const uint8_t *bytes=data.bytes;NSUInteger offset=0;
    while(offset<data.length){ssize_t n=write(fd,bytes+offset,data.length-offset);if(n<=0){perror("Write PDF");close(fd);return 1;}offset+=n;}
    if(close(fd)){perror("Close PDF");return 1;}
    printf("Created %lu-byte PDF: blue rectangle above green circle\n",(unsigned long)data.length);
    return 0;
}}
