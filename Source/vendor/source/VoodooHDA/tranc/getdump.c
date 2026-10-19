/* to compile
  clang getdump.c -o getdump -framework IOKit -framework CoreFoundation -Wall -Wextra -Werror
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <IOKit/IOKitLib.h>
//#include <IOKitLib.h>
#define SOUND_MIXER_NRDEVICES 25
#include "Shared.h"

void printMsgBuffer(io_service_t service)
{
	kern_return_t ret;
	io_connect_t connect = 0;
#if __LP64__
	mach_vm_address_t address;
	mach_vm_size_t size;	
#else	
	vm_address_t address;
	vm_size_t size;
#endif	

	ret = IOServiceOpen(service, mach_task_self(), 0, &connect);
	if (ret != KERN_SUCCESS) {
		printf("error: IOServiceOpen returned 0x%08x\n", ret);
		goto failure;
	}

	ret = IOConnectMapMemory(connect, kVoodooHDAMemoryMessageBuffer, mach_task_self(), &address, &size,
			kIOMapAnywhere | kIOMapDefaultCache);
	if (ret != kIOReturnSuccess) {
		printf("error: IOConnectMapMemory returned 0x%08x\n", ret);
		goto failure;
	}

	printf("%s\n", (char *) address);

failure:
	if (connect) {
		ret = IOServiceClose(connect);
		if (ret != KERN_SUCCESS)
			printf("warning: IOServiceClose returned 0x%08x\n", ret);
	}
}

int main()
{
	mach_port_t masterPort;
	io_iterator_t iter = 0;
	io_service_t service = 0;
	kern_return_t ret;
	io_string_t path;

	ret = IOMasterPort(MACH_PORT_NULL, &masterPort);
	if (ret != KERN_SUCCESS) {
		printf("error: IOMasterPort returned 0x%08x\n", ret);
		goto failure;
	}

	ret = IOServiceGetMatchingServices(masterPort, IOServiceMatching(kVoodooHDAClassName), &iter);
	if (ret != KERN_SUCCESS) {
		printf("error: IOServiceGetMatchingServices returned 0x%08x\n", ret);
		goto failure;
	}
	while ((service = IOIteratorNext(iter)) != 0) {
		ret = IORegistryEntryGetPath(service, kIOServicePlane, path);
		if (ret != KERN_SUCCESS) {
	    	printf("error: IORegistryEntryGetPath returned 0x%08x\n", ret);
			goto failure;
		}
		printf("Found a device of class "kVoodooHDAClassName": %s\n\n", path);
		printMsgBuffer(service);
		IOObjectRelease(service);
	}

failure:
	if (service)
		IOObjectRelease(service);
	if (iter)
		IOObjectRelease(iter);

	return 0;
}
