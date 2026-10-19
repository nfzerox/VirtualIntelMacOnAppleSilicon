#include "License.h"

// This class represents the user client object for the driver, which
// will be instantiated by IOKit to represent a connection to the client
// process, in response to the client's call to IOServiceOpen().
// It will be destroyed when the connection is closed or the client
// abnormally terminates, so it should track all the resources allocated
// to the client.

#include "VoodooHDAUserClient.h"
#include "VoodooHDADevice.h"
#include "Common.h"

#include "Shared.h"

#ifdef TIGER
#include "TigerAdditionals.h"
#endif


#define super IOUserClient
OSDefineMetaClassAndStructors(VoodooHDAUserClient, IOUserClient);

#define logMsg(fmt, args...)	messageHandler(kVoodooHDAMessageTypeGeneral, fmt, ##args)
#define errorMsg(fmt, args...)	messageHandler(kVoodooHDAMessageTypeError, fmt, ##args)
#define dumpMsg(fmt, args...)	messageHandler(kVoodooHDAMessageTypeDump, fmt, ##args)

__attribute__((visibility("hidden")))
void VoodooHDAUserClient::messageHandler(UInt32 type, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	if (mDevice)
		mDevice->messageHandler(type, format, args);
	else if (mVerbose >= 1)
		vprintf(format, args);
	va_end(args);
}

bool VoodooHDAUserClient::start(IOService *provider)
{
//	logMsg("VoodooHDAUserClient[%p]::start\n", this);

	if (!super::start(provider))
		return false;

	mDevice = OSDynamicCast(VoodooHDADevice, provider);
	ASSERT(mDevice);

	mVerbose = mDevice->mVerbose;

	return true;
}

bool VoodooHDAUserClient::didTerminate(IOService *provider, IOOptionBits options, bool *defer)
{
//	logMsg("VoodooHDAUserClient[%p]::didTerminate\n", this);

	// if defer is true, stop will not be called on the user client
	*defer = false;

	return super::didTerminate(provider, options, defer);
}

// clientClose is called when the user process calls IOServiceClose
IOReturn VoodooHDAUserClient::clientClose()
{
//    logMsg("VoodooHDAUserClient[%p]::clientClose\n", this);

	if (!isInactive())
		terminate();

	return kIOReturnSuccess;
}

// getTargetAndMethodForIndex looks up the external methods - supply a description of the parameters 
// available to be called 
IOExternalMethod *VoodooHDAUserClient::getTargetAndMethodForIndex(IOService **targetP, UInt32 index)
{
	//logMsg("VoodooHDAUserClient[%p]::getTargetAndMethodForIndex(%ld)\n", this, index);

	static const IOExternalMethod methodDescs[kVoodooHDANumMethods] = {
		{ NULL, (IOMethod) &VoodooHDAUserClient::actionMethod, kIOUCStructIStructO,
				kIOUCVariableStructureSize, kIOUCVariableStructureSize },
	};

	*targetP = this;
	if (index < kVoodooHDANumMethods)
		return (IOExternalMethod *) (methodDescs + index);
	else
		return NULL;
}

__attribute__((visibility("hidden")))
IOReturn VoodooHDAUserClient::actionMethod(UInt32 *dataIn, UInt32 *dataOut, IOByteCount inputSize,
		IOByteCount *outputSize)
{
	IOReturn result;
	UInt32 action, dataSize;
	void *data;
	UInt64 outputMax;

	//logMsg("VoodooHDAUserClient[%p]::actionMethod(%ld, %ld)\n", this, inputSize, *outputSize);

	if (inputSize != sizeof (UInt32))
		return kIOReturnBadArgument;
	action = *dataIn;

	result = mDevice->runAction(&action, &dataSize, &data);

	// note: we can only transfer sizeof (io_struct_inband_t) bytes out at a time

	outputMax = *outputSize;
    *outputSize = dataSize;
	if (dataSize) {
		ASSERT(data);
	    if (outputMax < dataSize)
	        return kIOReturnNoSpace;
		bcopy(data, dataOut, dataSize);
	}

    return result;
}

IOReturn VoodooHDAUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
		IOMemoryDescriptor **memory)
{
	IOReturn result;
	IOBufferMemoryDescriptor *memDesc;

//	logMsg("VoodooHDAUserClient[%p]::clientMemoryForType(0x%lx)\n", this, type);

	// note: IOConnectUnmapMemory should not be used with this user client

	*options = 0;
	*memory = NULL;

	switch (type) {
	case kVoodooHDAMemoryMessageBuffer:
		mDevice->lockMsgBuffer();
		if (!mDevice->mMsgBufferSize) {
			errorMsg("error: message buffer size is zero\n");
			mDevice->unlockMsgBuffer();
			result = kIOReturnUnsupported;
			break;
		}
		memDesc = IOBufferMemoryDescriptor::inTaskWithOptions(0,
															  kIOMemoryPageable | kIODirectionIn,
															  mDevice->mMsgBufferSize);
		if (!memDesc ||
			memDesc->prepare() != kIOReturnSuccess) {
			errorMsg("error: couldn't allocate buffer memory descriptor (size: %ld)\n",
					mDevice->mMsgBufferSize);
			mDevice->unlockMsgBuffer();
			result = kIOReturnVMError;
			RELEASE(memDesc);
			break;
		}
		memDesc->writeBytes(0U, mDevice->mMsgBuffer, mDevice->mMsgBufferSize);
		mDevice->unlockMsgBuffer();
		memDesc->complete();
		*options |= kIOMapReadOnly;
		*memory = memDesc; // automatically released after memory is mapped into task
		result = kIOReturnSuccess;
		break;
			//Разделяемая память для PrefPanel
	case kVoodooHDAMemoryCommand:
		mDevice->lockPrefPanelMemoryBuf();
		if (!mDevice->mPrefPanelMemoryBuf) {
			errorMsg("error: message buffer size is zero\n");
			mDevice->unlockPrefPanelMemoryBuf();
			result = kIOReturnUnsupported;
			break;
		}
		memDesc = IOBufferMemoryDescriptor::inTaskWithOptions(0,
															  kIOMemoryPageable | kIODirectionInOut,
															  mDevice->mPrefPanelMemoryBufSize);
		if (!memDesc ||
			memDesc->prepare() != kIOReturnSuccess) {
			errorMsg("error: couldn't allocate buffer memory descriptor (size: %ld)\n",
					 mDevice->mPrefPanelMemoryBufSize);
			mDevice->unlockPrefPanelMemoryBuf();
			result = kIOReturnVMError;
			RELEASE(memDesc);
			break;
		}
		mDevice->updatePrefPanelMemoryBuf();
		memDesc->writeBytes(0U, mDevice->mPrefPanelMemoryBuf,  mDevice->mPrefPanelMemoryBufSize);
		mDevice->unlockPrefPanelMemoryBuf();
		memDesc->complete();
		*memory = memDesc; // automatically released after memory is mapped into task
		result = kIOReturnSuccess;
		break;
			//Разделяемая память для буфера с текущеми настройками усиления
	case kVoodooHDAMemoryExtMessageBuffer:
		mDevice->lockExtMsgBuffer();
		if (!mDevice->mExtMsgBufferSize) {
			errorMsg("error: ext message buffer size is zero\n");
			mDevice->unlockExtMsgBuffer();
			result = kIOReturnUnsupported;
			break;
		}

		memDesc = IOBufferMemoryDescriptor::inTaskWithOptions(0,
															  kIOMemoryPageable | kIODirectionIn,
															  mDevice->mExtMsgBufferSize);
		if (!memDesc ||
			memDesc->prepare() != kIOReturnSuccess) {
			errorMsg("error: couldn't allocate buffer memory descriptor (size: %ld)\n",
					 mDevice->mExtMsgBufferSize);
			mDevice->unlockExtMsgBuffer();
			result = kIOReturnVMError;
     		RELEASE(memDesc);
			break;
		}
		memDesc->writeBytes(0U, mDevice->mExtMsgBuffer, mDevice->mExtMsgBufferSize);
		mDevice->unlockExtMsgBuffer();
		memDesc->complete();
		*options |= kIOMapReadOnly;
		*memory = memDesc; // automatically released after memory is mapped into task
		result = kIOReturnSuccess;
		break;
	default:
		result = kIOReturnBadArgument;
		break;
	}

	return result;
}
