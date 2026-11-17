// Finite diagnosis of the same static libusb Darwin backend as the USB helper.
// Root only; caller must normally unmount the explicitly selected test device.
// No endpoints, file data or USB storage commands are transferred.
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void log_message(libusb_context *context,enum libusb_log_level level,const char *message) {
    (void)context;fprintf(stderr,"libusb[%d] %s",level,message);
}
int main(int argc,char **argv) {
    if(argc!=5||geteuid()!=0)return 64;
    unsigned bus=(unsigned)strtoul(argv[1],NULL,0),address=(unsigned)strtoul(argv[2],NULL,0);
    unsigned vendor=(unsigned)strtoul(argv[3],NULL,0),product=(unsigned)strtoul(argv[4],NULL,0);
    if(!bus||!address||!vendor||!product)return 64;
    libusb_context *context=NULL;libusb_device_handle *handle=NULL;
    if(libusb_init(&context))return 1;
    libusb_set_log_cb(context,log_message,LIBUSB_LOG_CB_CONTEXT);
    libusb_set_option(context,LIBUSB_OPTION_LOG_LEVEL,LIBUSB_LOG_LEVEL_INFO);
    libusb_device **devices=NULL;ssize_t count=libusb_get_device_list(context,&devices);
    for(ssize_t i=0;i<count;i++) {
        struct libusb_device_descriptor descriptor;
        if(libusb_get_bus_number(devices[i])!=bus||libusb_get_device_address(devices[i])!=address||
           libusb_get_device_descriptor(devices[i],&descriptor)||descriptor.idVendor!=vendor||descriptor.idProduct!=product)continue;
        int result=libusb_open(devices[i],&handle);printf("open=%s\n",libusb_error_name(result));break;
    }
    if(devices)libusb_free_device_list(devices,1);
    if(!handle){libusb_exit(context);return 1;}
    printf("kernel-driver-before=%d\n",libusb_kernel_driver_active(handle,0));
    printf("auto-detach=%s\n",libusb_error_name(libusb_set_auto_detach_kernel_driver(handle,1)));
    int result=libusb_claim_interface(handle,0);
    printf("claim=%s kernel-driver-after=%d\n",libusb_error_name(result),libusb_kernel_driver_active(handle,0));
    if(!result)printf("release=%s\n",libusb_error_name(libusb_release_interface(handle,0)));
    libusb_close(handle);libusb_exit(context);
    return result?1:0;
}
