#include "License.h"

#include "VoodooHDADevice.h"
#include "VoodooHDAEngine.h"
#include "Tables.h"
#include "Models.h"
#include "Common.h"
#include "Verbs.h"
#include "OssCompat.h"

#include "Shared.h"

#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/pci/IOPCIDevice.h>

#include <kern/locks.h>

#ifdef TIGER
#include "TigerAdditionals.h"
#endif

#define HDAC_REVISION "20120126_0002"

#define LOCK()	//	lock(__FUNCTION__)
#define UNLOCK()	//unlock(__FUNCTION__)

#define super IOAudioDevice
OSDefineMetaClassAndStructors(VoodooHDADevice, IOAudioDevice)

	//#if __LP64__
#define MSG_BUFFER_SIZE 262140
	//#else
	//#define MSG_BUFFER_SIZE 65535
	//#endif

#define kVoodooHDAVerboseLevelKey "VoodooHDAVerboseLevel"

// cue8chalk: added to allow for the volume change fix to be controlled from the plist
#define kVoodooHDAEnableVolumeChangeFixKey "VoodooHDAEnableVolumeChangeFix"
#define kVoodooHDAEnableHalfVolumeFixKey "VoodooHDAEnableHalfVolumeFix"
// VertexBZ: added to allow for the Mic and Mute fixes to be controlled from the plist
#define kVoodooHDAEnableHalfMicVolumeFixKey "VoodooHDAEnableHalfMicVolumeFix"
#define kVoodooHDAEnableMuteFixKey "VoodooHDAEnableMuteFix"
#define kVoodooHDAAllowMSI "AllowMSI"
#define kDisableInputMonitor "DisableInputMonitor"

bool VoodooHDADevice::init(OSDictionary *dict)
{
	OSNumber *verboseLevelNum;
	OSBoolean *osBool;
	extern kmod_info_t kmod_info;
	mVerbose = 0;
	if (!super::init(dict))
		return false;
	
	dumpMsg("Loading VoodooHDA %s (based on hdac version " HDAC_REVISION ")\n", kmod_info.version);
	
//	ASSERT(dict);
	verboseLevelNum = OSDynamicCast(OSNumber, dict->getObject(kVoodooHDAVerboseLevelKey));
	if (verboseLevelNum)
		mVerbose = verboseLevelNum->unsigned32BitValue();
	else
		mVerbose = 0;

	mMessageLock = IOLockAlloc();

	logMsg("VoodooHDADevice[%p]::init\n", this);

	// cue8chalk: read flag for volume change fix
	// TODO - when VoodooHDA properly supports multiple devices (at least on my system - lol)
	// make this a per-device setting (tied to vid/did)
	osBool = OSDynamicCast(OSBoolean, dict->getObject(kVoodooHDAEnableVolumeChangeFixKey));
	if (osBool) {
		mEnableVolumeChangeFix = (bool)osBool->getValue();
	} else {
		mEnableVolumeChangeFix = false;
	}
	
	// Half volume slider fix
	osBool = OSDynamicCast(OSBoolean, dict->getObject(kVoodooHDAEnableHalfVolumeFixKey));
	if (osBool) {
		mEnableHalfVolumeFix = (bool)osBool->getValue();
	} else {
		mEnableHalfVolumeFix = false;
	}
    
    // VertexBZ: Half Mic volume slider fix
    osBool = OSDynamicCast(OSBoolean, dict->getObject(kVoodooHDAEnableHalfMicVolumeFixKey));
	if (osBool) {
		mEnableHalfMicVolumeFix = (bool)osBool->getValue();
	} else {
		mEnableHalfMicVolumeFix = false;
	}
    
    // VertexBZ: Mute fix
    osBool = OSDynamicCast(OSBoolean, dict->getObject(kVoodooHDAEnableMuteFixKey));
	if (osBool) {
		mEnableMuteFix = (bool)osBool->getValue();
	} else {
		mEnableMuteFix = false;
	} 

//Slice - some chipsets needed Inhibit Cache
	osBool = OSDynamicCast(OSBoolean, dict->getObject("InhibitCache"));
	if (osBool) {
		mInhibitCache = (bool)osBool->getValue();
	} else {
		mInhibitCache = false;
	}
  
  //DisableInputMonitor
	osBool = OSDynamicCast(OSBoolean, dict->getObject("DisableInputMonitor"));
	if (osBool) {
		mDisableInputMonitor = (bool)osBool->getValue();
	} else {
		mDisableInputMonitor = false;
	}
  

	osBool = OSDynamicCast(OSBoolean, dict->getObject("Vectorize"));
	if (osBool) {
		vectorize = (bool)osBool->getValue();
	} else {
		vectorize = true;
	}

	verboseLevelNum = OSDynamicCast(OSNumber, dict->getObject("Noise"));
	if (verboseLevelNum)
		noiseLevel = verboseLevelNum->unsigned32BitValue();
	else
		noiseLevel = 0;

	verboseLevelNum = OSDynamicCast(OSNumber, dict->getObject("Boost"));
	if (verboseLevelNum)
		Boost = verboseLevelNum->unsigned32BitValue();
	else
		Boost = 0;

	osBool = OSDynamicCast(OSBoolean, dict->getObject(kVoodooHDAAllowMSI));
	mAllowMSI = (osBool ? osBool->isTrue() : true);

	mLock = IOLockAlloc();

	mUnsolqState = HDAC_UNSOLQ_READY;

	mActionHandler = (IOCommandGate::Action) &VoodooHDADevice::handleAction;
	if (!mActionHandler) {
		errorMsg("error: couldn't cast command gate action handler\n");
		return false;
	}

	nSliderTabsCount = 0;
	mPrefPanelMemoryBufSize = 0;
	mPrefPanelMemoryBuf = 0;

	return true;
}

#define	PCI_CLASS_MULTI				0x04
#define PCI_SUBCLASS_MULTI_HDA		0x03

void VoodooHDADevice::initMixerDefaultValues(void)
{
	OSDictionary *MixerValues = 0;
	OSNumber *tmpNumber = 0;
	UInt16 tmpUI16 = 0;
	int index;
	OSString *tmpString = 0;
//	int MixValueCount = sizeof(MixerValueNamesBind) / sizeof(MixerValueName);
	
	MixerValues = OSDynamicCast(OSDictionary, getProperty("MixerValues"));

	for(int i=0; i<SOUND_MIXER_NRDEVICES; i++){
						
		tmpUI16 = MixerValueNamesBind[i].initValue;
	
		
		if(MixerValues && MixerValueNamesBind[i].name != 0 && MixerValueNamesBind[i].name[0] != 0) {
			tmpNumber = OSDynamicCast(OSNumber, MixerValues->getObject(MixerValueNamesBind[i].name));
			if (tmpNumber) {
				tmpUI16 = tmpNumber->unsigned16BitValue();
			} else {
				tmpString = OSDynamicCast(OSString, MixerValues->getObject(MixerValueNamesBind[i].name));
				if(tmpString) {
					long unsigned int jj = 0;
					int jjj = 0;
					if(sscanf(tmpString->getCStringNoCopy(), "0x%08lx", &jj)) {
						tmpUI16 = jj;
					}else if(sscanf(tmpString->getCStringNoCopy(), "%4d", &jjj)){
						tmpUI16 = jjj;
					}
				}
			}
		}
		//logMsg("Item %d init %d, index %d\n", i , tmpUI16, MixerValueNamesBind[i].index);
	
		
		index = MixerValueNamesBind[i].index;
		if(index >= 0 && index < SOUND_MIXER_NRDEVICES) 
			mMixerDefaults[index] = tmpUI16;
	}
}

IOService *VoodooHDADevice::probe(IOService *provider, SInt32 *score)
{
	IOService *result;
	UInt16 vendorId, deviceId, subVendorId, subDeviceId;
//	UInt32 classCode;
//	UInt8 devClass, subClass;
//	bool contIsGeneric = false;
	int n;

	//logMsg("VoodooHDADevice[%p]::probe\n", this);
//	IOLog("HDA: MixerInfoSize=%d ChannelInfoSize=%d\n", (int)sizeof(mixerDeviceInfo), (int)sizeof(ChannelInfo));

	result = super::probe(provider, score);
	if (result != static_cast<IOService*>(this))
		return result;
	
	initMixerDefaultValues();
	
//Slice	
	OSDictionary *tmpDict = 0;
	OSIterator *iter = 0;
	const OSSymbol *dictKey = 0;
	OSNumber *tmpNumber = 0;
	UInt32 tmpUI32 = 0;
	OSString *tmpString = 0;
	OSArray *tmpArray = 0;
	UInt32 tmpUIArray[HDA_MAX_CONNS];
	UInt32 nArrayCount = 0;
//	UInt32 j = 0;
	
	NodesToPatch = OSDynamicCast(OSArray, getProperty("NodesToPatch"));
	if(NodesToPatch){
		NumNodes = NodesToPatch->getCount();
		for(int i=0; i<NumNodes; i++){
			NodesToPatchArray[i].Enable = 0;
			NodesToPatchArray[i].cad = 0;
			tmpDict = OSDynamicCast(OSDictionary, NodesToPatch->getObject(i)); 
			iter = OSCollectionIterator::withCollection(tmpDict);
			if (iter) {
				while ((dictKey = (const OSSymbol *)iter->getNextObject())) {
					nArrayCount = 0;
					tmpArray = OSDynamicCast(OSArray, tmpDict->getObject(dictKey));
					if(tmpArray) {
						//logMsg("Array (%d) ", tmpArray->getCount());
						for(unsigned int arrayIndex = 0; arrayIndex < tmpArray->getCount(); arrayIndex++) {
							tmpNumber = OSDynamicCast(OSNumber, tmpArray->getObject(arrayIndex));
							if (tmpNumber) {
								tmpUI32 = tmpNumber->unsigned32BitValue();
							} else {
								tmpString = OSDynamicCast(OSString, tmpArray->getObject(arrayIndex));
								if(tmpString) {
									long unsigned int jj = 0;
									int jjj = 0;
									if(sscanf(tmpString->getCStringNoCopy(), "0x%08lx", &jj)) {
										tmpUI32 = static_cast<UInt32>(jj);
									}else if(sscanf(tmpString->getCStringNoCopy(), "%4d", &jjj)){
										tmpUI32 = jjj;
									}
								}
							}
							tmpUIArray[nArrayCount]= tmpUI32;
							nArrayCount++;
							
							//logMsg("%d ", tmpUI32);
						}
						//logMsg("\n");
					}else{
					
						tmpNumber = OSDynamicCast(OSNumber, tmpDict->getObject(dictKey));
						if (tmpNumber) {
							tmpUI32 = tmpNumber->unsigned32BitValue();
							tmpUIArray[0]= tmpUI32;
							nArrayCount = 1;
						} else {
							tmpString = OSDynamicCast(OSString, tmpDict->getObject(dictKey));
							long unsigned int jj = 0;
							if(sscanf(tmpString->getCStringNoCopy(), "0x%08lx", &jj)) {
								tmpUI32 = static_cast<UInt32>(jj);
								tmpUIArray[0]= tmpUI32;
								nArrayCount = 1;
							}
						}
					}
					tmpString = OSString::withCString(dictKey->getCStringNoCopy());
					if(tmpString->isEqualTo("Node")){
						if(tmpUI32 == 0) 
							break;
						NodesToPatchArray[i].Node = tmpUI32;
					} else if (tmpString->isEqualTo("Config")){
						NodesToPatchArray[i].Config = tmpUI32;
						NodesToPatchArray[i].Enable |= 0x1;
					} else if (tmpString->isEqualTo("Conns")){
						for(unsigned int arrayIndex = 0; arrayIndex < nArrayCount; arrayIndex++) {
							NodesToPatchArray[i].Conns[arrayIndex] = tmpUIArray[arrayIndex];
						}
						NodesToPatchArray[i].nConns = nArrayCount;
						NodesToPatchArray[i].Enable |= 0x2;
					} else if (tmpString->isEqualTo("Type")){
						NodesToPatchArray[i].Type = tmpUI32;
						NodesToPatchArray[i].Enable |= 0x4;
					} else if (tmpString->isEqualTo("Cap")){
						NodesToPatchArray[i].Cap = tmpUI32;
						NodesToPatchArray[i].Enable |= 0x8;
					} else if (tmpString->isEqualTo("Enable")) {
						NodesToPatchArray[i].bEnabledWidget = tmpUI32;
						NodesToPatchArray[i].Enable |= 0x10;
					} else if (tmpString->isEqualTo("Control")) {
						NodesToPatchArray[i].Control = tmpUI32;
						NodesToPatchArray[i].Enable |= 0x20;
					} else if (tmpString->isEqualTo("Codec")) {
						//Codec по умолчанию = 0
						NodesToPatchArray[i].cad = tmpUI32;
					} else if (tmpString->isEqualTo("Select")) {
						NodesToPatchArray[i].nSel = tmpUI32;
						NodesToPatchArray[i].Enable |= 0x40;
					} else if (tmpString->isEqualTo("DAC")) {
						NodesToPatchArray[i].favoritDAC = tmpUI32;
						NodesToPatchArray[i].Enable |= 0x80;
					} else if (tmpString->isEqualTo("SwitchCh")) {
						//Меняем левый канал на правый для входных данных
						mSwitchCh = true;
					}
					
				}
			}
		}
	}
// Temporary trace
	dumpMsg("VHD %d nodes patching \n", NumNodes);
#if __LP64__
    for(int i=0; i<NumNodes; i++){
        dumpMsg("VHD Codec=%d Node=%d Config=%08lx Conns=%ld Type=%d\n", NodesToPatchArray[i].cad, NodesToPatchArray[i].Node,
                (long unsigned int)NodesToPatchArray[i].Config, (long int)NodesToPatchArray[i].Conns, 
				(int)NodesToPatchArray[i].Type);
    }
#else
    for(int i=0; i<NumNodes; i++){
        dumpMsg("VHD Codec=%d Node=%d Config=%08lx Conns=%d Type=%d\n", (int)NodesToPatchArray[i].cad, (int)NodesToPatchArray[i].Node,
                NodesToPatchArray[i].Config, (int)NodesToPatchArray[i].Conns, (int)NodesToPatchArray[i].Type);
    }
#endif//	
	mPciNub = OSDynamicCast(IOPCIDevice, provider);
	if (!mPciNub) {
		errorMsg("error: couldn't cast provider to IOPCIDevice\n");
		return NULL;
	}
	if (!mPciNub->open(this)) {
		errorMsg("error: couldn't open PCI device\n");
		mPciNub = NULL;
		return NULL;
	}
/*
	classCode = mPciNub->configRead32(kIOPCIConfigClassCode & 0xfc) >> 8;
	subClass = (classCode >> 8) & 0xff;
	devClass = (classCode >> 16) & 0xff;
	if ((devClass != PCI_CLASS_MULTI) || (subClass != PCI_SUBCLASS_MULTI_HDA)) {
		result = NULL;
		goto done;
	}*/ //Slice - do not check class code twice, it is performed by IOKit

	vendorId = mPciNub->configRead16(kIOPCIConfigVendorID);
	deviceId = mPciNub->configRead16(kIOPCIConfigDeviceID);
	mDeviceId = (deviceId << 16) | vendorId;
	for (n = 0; gControllerList[n].name; n++) {
		if (gControllerList[n].model == mDeviceId)
			break;
		else if (HDA_DEV_MATCH(gControllerList[n].model, mDeviceId)) {
//			contIsGeneric = true;
			break;
		}
	}
	mControllerName = gControllerList[n].name;
	if (!mControllerName)
		mControllerName = "Generic";

	errorMsg("Controller: %s (vendor ID: %04x, device ID: %04x)\n", mControllerName, vendorId, deviceId);

	subVendorId = mPciNub->configRead16(kIOPCIConfigSubSystemVendorID);
	subDeviceId = mPciNub->configRead16(kIOPCIConfigSubSystemID);
	mSubDeviceId = (subDeviceId << 16) | subVendorId;
	if (mSubDeviceId == HP_NX6325_SUBVENDORX)
		mSubDeviceId = HP_NX6325_SUBVENDOR;

//done:
	mPciNub->close(this);

	return result;
}

__attribute__((noinline, visibility("hidden")))
void VoodooHDADevice::disablePCIeNoSnoop(UInt16 vendorId)
{
	UInt16 snoop16;
	UInt8 snoop8;
	switch (vendorId) {
		case INTEL_VENDORID:
			/* Defines for Intel SCH HDA snoop control */
			snoop16 = mPciNub->configRead16( INTEL_SCH_HDA_DEVC );
			if (snoop16 & INTEL_SCH_HDA_DEVC_NOSNOOP) {
				mPciNub->configWrite16( INTEL_SCH_HDA_DEVC,	snoop16 & (~INTEL_SCH_HDA_DEVC_NOSNOOP));
			}
			break;
		case AMD_VENDORID:
			/* FALL THROUGH */
		case ATI_VENDORID:
			snoop8 = mPciNub->configRead8(0x42U);
			if (!(snoop8 & 2U)) {
				mPciNub->configWrite8(0x42U, (snoop8 & 0xf8U) | 2U);
			}
			break;
		case NVIDIA_VENDORID:
			snoop8 = mPciNub->configRead8(0x4eU);
			if ((snoop8 & 15U) != 15U) {
				mPciNub->configWrite8(0x4eU, (snoop8 & 0xf0U) | 15U);
			}
			break;
	}
}

bool VoodooHDADevice::initHardware(IOService *provider)
{
	bool result = false;
	UInt16 config, vendorId;
	UInt32 gCtl;
	UInt16 msiCtl;

//moved here from init ----------
  mMsgBufferEnabled = false;
	mMsgBufferSize = MSG_BUFFER_SIZE;
	mMsgBufferPos = 0;
	
	mSwitchCh = false;
	mMsgBuffer = (char *) allocMem(mMsgBufferSize);
	if (!mMsgBuffer) {
		errorMsg("error: couldn't allocate message buffer (%ld bytes)\n", mMsgBufferSize);
		return false;
	}
	
	mExtMessageLock = IOLockAlloc();
	mExtMsgBufferSize = MSG_BUFFER_SIZE;
	mExtMsgBufferPos = 0;
	
	mExtMsgBuffer = (char *) allocMem(mExtMsgBufferSize);
	if (!mExtMsgBuffer) {
		errorMsg("error: couldn't allocate ext message buffer (%ld bytes)\n", mExtMsgBufferSize);
		return false;
	}
//--------------  
  
	//logMsg("VoodooHDADevice[%p]::initHardware\n", this);

	oldConfig = UINT16_MAX;
	if (!mPciNub || !super::initHardware(provider))
		goto done;
	if (!mPciNub->open(this)) {
		errorMsg("error: failed to open PCI device\n");
		goto done;
	}

	config = mPciNub->configRead16(kIOPCIConfigCommand);
	oldConfig = config;
	config |= (kIOPCICommandBusMaster | kIOPCICommandMemorySpace); // | kIOPCICommandMemWrInvalidate); //Slice
	 //config &= ~kIOPCICommandIOSpace; //Slice - not implemented for HDA
	mPciNub->configWrite16(kIOPCIConfigCommand, config);
	if (mPciNub->hasPCIPowerManagement(kPCIPMCD3Support))
		mPciNub->enablePCIPowerManagement(kPCIPMCSPowerStateD3);

	// Note: kIOMapInhibitCache is the default for PCI bars... - Zenith432
	mBarMap = mPciNub->mapDeviceMemoryWithIndex(0U);
	if (!mBarMap) {
		errorMsg("error: mapDeviceMemoryWithRegister for BAR0 failed\n");
		goto done;
	}
	mRegBase = mBarMap->getVirtualAddress();
	if (!mRegBase) {
		errorMsg("error: getVirtualAddress for mBarMap failed\n");
		goto done;
	}
	char string[20];
//	strncpy(string, "Voodoo HDA Device", sizeof(string));
  snprintf(string, sizeof(string), "VoodooHDADevice%x ", (unsigned int)(mDeviceId >> 16));
	setDeviceName(string);
	setDeviceShortName("VoodooHDA ");
	setManufacturerName("Voodoo ");
	//TODO: setDeviceModelName
  snprintf(string, sizeof(string), "VoodooHDA:%x ", (unsigned int)(mDeviceId >> 16));
  setDeviceModelName(string);
	setDeviceTransportType(kIOAudioDeviceTransportTypeBuiltIn);

//	logMsg("deviceId: %08lx, subDeviceId: %08lx\n", mDeviceId, mSubDeviceId);

	vendorId = mDeviceId & 0xffff;
	if (vendorId == INTEL_VENDORID) {
		/* TCSEL -> TC0 */
		UInt8 value = mPciNub->configRead8(0x44);
		mPciNub->configWrite8(0x44, value & 0xf8);
//		logMsg("TCSEL: %02x -> %02x\n", value, mPciNub->configRead8(0x44));
	}
	disablePCIeNoSnoop(vendorId);
	if (vendorId == NVIDIA_VENDORID &&
      !OSDynamicCast(OSBoolean, getProperty(kVoodooHDAAllowMSI))) {
		/*
		 * Disable MSI for NVIDIA controller if not in Info.plist.
		 *   Known to have problems with MSI (Quirk from HDAC)
		 */
		mAllowMSI = false;
	}
	msiCtl = mPciNub->configRead16(0x62);
	logMsg("MSI_CTL=0x%04x\n", msiCtl);

	if (!getCapabilities()) {
		errorMsg("error: getCapabilities failed\n");
		goto done;
	}

//	logMsg("Resetting controller...\n");
	if (!resetController(true)) {
		errorMsg("error: resetController failed\n");
		goto done;
	}

	mCorbMem = allocateDmaMemory(mCorbSize * sizeof (UInt32), "CORB", 0 /* kIOMapInhibitCache */);
	if (!mCorbMem) {
		errorMsg("error: allocateDmaMemory for CORB memory failed\n");
		goto done;
	}

	mRirbMem = allocateDmaMemory(mRirbSize * sizeof (RirbResponse), "RIRB", 0 /* kIOMapInhibitCache */);
	if (!mRirbMem) {
		errorMsg("error: allocateDmaMemory for RIRB memory failed\n");
		goto done;
	}

	initCorb();
	initRirb();

	setupWorkloop();
	enableEventSources();

	LOCK();

//	logMsg("Starting CORB Engine...\n");
	startCorb();
// logMsg("Starting RIRB Engine...\n");
	startRirb();

	logMsg("Enabling controller interrupt...\n");
	gCtl = readData32(HDAC_GCTL);
	logMsg("HDAC_CTL=0x%04x\n", gCtl);
	writeData32(HDAC_GCTL, gCtl | HDAC_GCTL_UNSOL);
	writeData32(HDAC_INTCTL, HDAC_INTCTL_CIE | HDAC_INTCTL_GIE);
	IODelay(1000);

	// todo: hdac_config_fetch(&mQuirksOn, &mQuirksOff);
	mQuirksOn = 0;
	mQuirksOff = 0;

	enableMsgBuffer(true);
//	logMsg("Scanning HDA codecs...\n");
	scanCodecs();
	enableMsgBuffer(false);
	UNLOCK();
	for (int n = 0; n < HDAC_CODEC_MAX; n++) {
		Codec *codec = mCodecs[n];
		if (!codec)
			continue;
		dumpMsg("Codec #%d: %s (vendor ID: %04x, device ID: %04x)\n", codec->cad, findCodecName(codec),
				codec->vendorId, codec->deviceId);
	}

	if (!mNumChannels) {
		errorMsg("error: no PCM channels found\n");
		goto done;
	}

	for (int n = 0; n < mNumChannels; n++) {
		if (!createAudioEngine(&mChannels[n])) {
			errorMsg("error: createAudioEngine for channel %d failed\n", n);
			goto done;
		}
	}
	if (!audioEngines || (audioEngines->getCount() == 0)) {
		errorMsg("error: no audio engines were created\n");
		goto done;
	}
	LOCK();
	//Slice - it's a time to switch engines
	for (int n = 0; n < HDAC_CODEC_MAX; n++) {
		Codec *codec = mCodecs[n];
		if (!codec) continue;
		for (int funcGroupNum = 0; funcGroupNum < codec->numFuncGroups; funcGroupNum++) {
			FunctionGroup *funcGroup = &codec->funcGroups[funcGroupNum];
			if (!funcGroup) continue;
			if (funcGroup->nodeType != HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO)
				continue;	
			if (funcGroup->mSwitchEnable)
				switchHandler(funcGroup, true);
		}
	}
	UNLOCK();

	result = true;
done:
	if (!result)
		stop(provider);

	return result;
}

void VoodooHDADevice::stop(IOService *provider)
{
	logMsg("VoodooHDADevice[%p]::stop\n", this);

	disableEventSources();

	if (mWorkLoop) {
		if (mTimerSource) {
			mWorkLoop->removeEventSource(mTimerSource);
			mTimerSource->release();
			mTimerSource = NULL;
		}

		if (mInterruptSource) {
			mWorkLoop->removeEventSource(mInterruptSource);
			mInterruptSource->release();
			mInterruptSource = NULL;
		}

		mWorkLoop->release();
		mWorkLoop = NULL;
	}

	if (mRegBase) {
		LOCK();
		if (!resetController(false))
			errorMsg("warning: resetController failed\n");
		UNLOCK();
	}
	if (mPciNub &&
		(mPciNub->isOpen(this) || mPciNub->open(this))) {
		if (oldConfig != UINT16_MAX)
			mPciNub->configWrite16(kIOPCIConfigCommand, oldConfig); //Slice
			mPciNub->enablePCIPowerManagement(kPCIPMCSPowerStateD0);
		mPciNub->close(this);
	}
	
	super::stop(provider);
}

#define FREE_LOCK(x)		do { if (x) { IOLockLock(x); IOLockFree(x); (x) = NULL; } } while (0)
#define FREE_DMA_MEMORY(x)	do { if (x) { freeDmaMemory(x); (x) = NULL; } } while (0)

void VoodooHDADevice::free()
{
	logMsg("VoodooHDADevice[%p]::free\n", this);

	// if probe or initHardware (called by super start) fails, we end up here - stop is not called

	mMsgBufferEnabled = false;
	FREE(mMsgBuffer);
	FREE_LOCK(mMessageLock);
	
	FREE(mExtMsgBuffer);
	FREE_LOCK(mExtMessageLock);

	FREE(mPrefPanelMemoryBuf);
	FREE_LOCK(mPrefPanelMemoryBufLock);

	FREE_LOCK(mLock);

	if (mRegBase)
		mRegBase = 0;
	RELEASE(mBarMap);

	mPciNub = NULL;

	for (int i = 0; i < HDAC_CODEC_MAX; i++) {
		Codec *codec = mCodecs[i];
		if (!codec)
			continue;
		mCodecs[i] = NULL;
		if (codec->numFuncGroups)
			ASSERT(codec->funcGroups);
		else
			ASSERT(!codec->funcGroups);
		for (int j = 0; j < codec->numFuncGroups; j++) {
			FunctionGroup *funcGroup = &codec->funcGroups[j];
			FREE(funcGroup->widgets);
			if (funcGroup->nodeType == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO) {
				FREE(funcGroup->audio.controls);
				FREE(funcGroup->audio.assocs);
				FREE(funcGroup->audio.pcmDevices);
			}
		}
		FREE(codec->funcGroups);
		FREE(codec);
	}

	FREE_DMA_MEMORY(mDmaPosMem);
	FREE_DMA_MEMORY(mCorbMem);
	FREE_DMA_MEMORY(mRirbMem);

	if (mNumChannels) {
		ASSERT(mChannels);
		for (int i = 0; i < mNumChannels; i++)
   			if (mChannels[i].numBlocks > 0)
   				FREE_DMA_MEMORY(mChannels[i].bdlMem);
		FREE(mChannels);
	} else
		ASSERT(!mChannels);

	super::free();
}

bool VoodooHDADevice::createAudioEngine(Channel *channel)
{
	VoodooHDAEngine *audioEngine = NULL;
	bool result = false;

	//logMsg("VoodooHDADevice[%p]::createAudioEngine\n", this);

	audioEngine = new VoodooHDAEngine;
  if (!audioEngine) return false;
	if (!audioEngine->initWithChannel(channel)) {
		errorMsg("error: VoodooHDAEngine::init failed\n");
  } else {
    
    // cue8chalk: set volume change fix on the engine
    audioEngine->mEnableVolumeChangeFix = mEnableVolumeChangeFix;
    // VertexBZ: set Mute fix on the engine
    audioEngine->mEnableMuteFix = mEnableMuteFix;
    
    //  audioEngine->mDisableInputMonitor = mDisableInputMonitor;
    audioEngine->Boost = Boost;
    // Active the audio engine - this will cause the audio engine to have start() and
    // initHardware() called on it. After this function returns, that audio engine should
    // be ready to begin vending audio services to the system.
    if (activateAudioEngine(audioEngine) != kIOReturnSuccess) {
      errorMsg("error: activateAudioEngine failed\n");
    } else {
      result = true;
    }
  }

	RELEASE(audioEngine);

	return result;
}

IOReturn VoodooHDADevice::performPowerStateChange(IOAudioDevicePowerState oldPowerState,
		IOAudioDevicePowerState newPowerState, __unused UInt32 *microsecondsUntilComplete)
{
	IOReturn result = kIOReturnSuccess;

	//logMsg("VoodooHDADevice[%p]::performPowerStateChange(%d, %d)\n", this, oldPowerState, newPowerState);

	if (oldPowerState == kIOAudioDeviceSleep) {
		if (!resume()) {
			errorMsg("error: resume action failed\n");
			result = kIOReturnError;
		}
	} else if (newPowerState == kIOAudioDeviceSleep) {
		if (!suspend()) {
			errorMsg("error: suspend action failed\n");
			result = kIOReturnError;
		}
	}

	return result;
}

/*
 * Suspend and power down HDA bus and codecs.
 */
bool VoodooHDADevice::suspend()
{
		//logMsg("VoodooHDADevice[%p]::suspend\n", this);

	LOCK();
		//Slice - trace PCI
/*	for (int i=0; i<0xff; i+=16) {
		for(int j=0; j<15; j+=4)
			logMsg("(%02x)=%08x   ",(unsigned int)(i+j), (unsigned int)mPciNub->configRead32(i+j));  //for trace
		logMsg("\n");
	}*/
		//	
	for (int i = 0; i < mNumChannels; i++) {
		if (mChannels[i].flags & HDAC_CHN_RUNNING) {
			errorMsg("warning: found active channel during suspend action\n");
			channelStop(&mChannels[i], false);
		}
		mChannels[i].flags |= HDAC_CHN_SUSPEND;
	}

	for (int codecNum = 0; codecNum < HDAC_CODEC_MAX; codecNum++) {
		Codec *codec = mCodecs[codecNum];
		if (!codec)
			continue;
		for (int funcGroupNum = 0; funcGroupNum < codec->numFuncGroups; funcGroupNum++) {
			FunctionGroup *funcGroup = &codec->funcGroups[funcGroupNum];
	//		logMsg("Power down FG cad=%d nid=%d to the D3 state...\n", codec->cad, funcGroup->nid);
			sendCommand(HDA_CMD_SET_POWER_STATE(codec->cad, funcGroup->nid, HDA_CMD_POWER_STATE_D3),
					codec->cad);
		}
	}

//	logMsg("Resetting controller...\n");
	if (!resetController(false)) {
		errorMsg("error: resetController failed\n");
		return false;
	}

	UNLOCK();

//	logMsg("Suspend done.\n");

	return true;
}

/*
 * Powerup and restore HDA bus and codecs state.
 */
bool VoodooHDADevice::resume()
{
	logMsg("VoodooHDADevice[%p]::resume\n", this);

	LOCK();
		//Slice - dump PCI to understand what is the sleep issue
/*	for (int i=0; i<0xff; i+=16) {
		for(int j=0; j<15; j+=4)
			logMsg("(%02x)=%08x   ",(unsigned int)(i+j), (unsigned int)mPciNub->configRead32(i+j));  //for trace
		logMsg("\n");
	}*/
		//Slice - this trick was resolved weird sleep issue
	int vendorId = mDeviceId & 0xffff;
	if (vendorId == INTEL_VENDORID) {
		/* TCSEL -> TC0 */
		UInt8 value = mPciNub->configRead8(0x44);
		mPciNub->configWrite8(0x44, value & 0xf8);
			//		logMsg("TCSEL: %02x -> %02x\n", value, mPciNub->configRead8(0x44));
	}
	disablePCIeNoSnoop(static_cast<UInt16>(vendorId));

	logMsg("Resetting controller...\n");
	if (!resetController(true)) {
		errorMsg("error: resetController failed\n");
		return false;
	}

	initCorb();
	initRirb();
//Slice

//	setupWorkloop();
//	enableEventSources();

//	logMsg("Starting CORB Engine...\n");
	startCorb();
//	logMsg("Starting RIRB Engine...\n");
	startRirb();

//	logMsg("Enabling controller interrupt...\n");
	writeData32(HDAC_GCTL, readData32(HDAC_GCTL) | HDAC_GCTL_UNSOL);
	writeData32(HDAC_INTCTL, HDAC_INTCTL_CIE | HDAC_INTCTL_GIE);
	IODelay(1000);

	for (int codecNum = 0; codecNum < HDAC_CODEC_MAX; codecNum++) {
		Codec *codec = mCodecs[codecNum];
		if (!codec)
			continue;
		for (int funcGroupNum = 0; funcGroupNum < codec->numFuncGroups; funcGroupNum++) {
			FunctionGroup *funcGroup = &codec->funcGroups[funcGroupNum];
			if (funcGroup->nodeType != HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO) {
				logMsg("Power down unsupported non-audio FG cad=%d nid=%d to the D3 state...\n",
						codec->cad, funcGroup->nid);
				sendCommand(HDA_CMD_SET_POWER_STATE(codec->cad, funcGroup->nid, HDA_CMD_POWER_STATE_D3),
						codec->cad);
				continue;
			}

//			logMsg("Power up audio FG cad=%d nid=%d...\n", funcGroup->codec->cad, funcGroup->nid);
			powerup(funcGroup);
//			logMsg("AFG commit...\n");
			audioCommit(funcGroup);
//			logMsg("HP switch init...\n");
			UNLOCK(); // xxx
			for (int i = 0; i < funcGroup->audio.numPcmDevices; i++) {
//				logMsg("OSS mixer reinitialization...\n");
				mixerResume(&funcGroup->audio.pcmDevices[i]);
			}

			LOCK(); // xxx

			switchInit(funcGroup);

			if (funcGroup->mSwitchEnable)
				switchHandler(funcGroup, false);

		}
	}

	for (int i = 0; i < mNumChannels; i++) {
		if (!(mChannels[i].flags & HDAC_CHN_SUSPEND)) {
			errorMsg("warning: found non-suspended channel during resume action\n");
			continue;
		}
		mChannels[i].flags &= ~HDAC_CHN_SUSPEND;
	}

	UNLOCK();

//	logMsg("Resume done.\n");

	return true;
}

/******************************************************************************************/
/******************************************************************************************/

/*
 * Reset the controller to a quiescent and known state.
 hdac_reset(struct hdac_softc *sc, int wakeup)
 */
bool VoodooHDADevice::resetController(bool wakeup)
{
	UInt32 gctl;

	//logMsg("VoodooHDADevice[%p]::resetController(%d)\n", this, wakeup);

	/* Make sure WAKEEN bits are off */
	writeData16(HDAC_WAKEEN, 0U);

	/* Stop all Streams DMA engine */
	for (int i = 0; i < mInStreamsSup; i++)
		writeData32(HDAC_ISDCTL(i), 0);
	for (int i = 0; i < mOutStreamsSup; i++)
		writeData32(HDAC_OSDCTL(i), 0);
	for (int i = 0; i < mBiStreamsSup; i++)
		writeData32(HDAC_BSDCTL(i), 0);

	/* Stop Control DMA engines. */
	writeData8(HDAC_CORBCTL, 0);
	writeData8(HDAC_RIRBCTL, 0);

	/* Reset DMA position buffer. */
	writeData32(HDAC_DPIBLBASE, 0);
	writeData32(HDAC_DPIBUBASE, 0);

	/* Reset the controller. The reset must remain asserted for a minimum of 100us. */
	gctl = readData32(HDAC_GCTL);
	writeData32(HDAC_GCTL, gctl & ~HDAC_GCTL_CRST);
	for (int count = 0; count < 10000; count++) {
		gctl = readData32(HDAC_GCTL);
		if (!(gctl & HDAC_GCTL_CRST))
			break;
		IODelay(10);
	}
	if (gctl & HDAC_GCTL_CRST) {
		errorMsg("error: unable to put controller in reset\n");
		return false;
	}

	/* If wakeup is not requested - leave the controller in reset state. */
	if (!wakeup)
		return true;

	IODelay(100);
	gctl = readData32(HDAC_GCTL);
	writeData32(HDAC_GCTL, gctl | HDAC_GCTL_CRST);
	for (int count = 0; count < 10000; count++) {
		gctl = readData32(HDAC_GCTL);
		if (gctl & HDAC_GCTL_CRST)
			break;
		IODelay(10);
	}
	if (!(gctl & HDAC_GCTL_CRST)) {
		errorMsg("error: controller stuck in reset\n");
		return false;
	}

	/* Wait for codecs to finish their own reset sequence. The delay here
	 * should be of 250us but for some reasons, on it's not enough on my
	 * computer. Let's use twice as much as necessary to make sure that
	 * it's reset properly. */
	IODelay(1000);

	return true;
}

/*
 * Retreive the general capabilities of the hdac;
 *	Number of Input Streams
 *	Number of Output Streams
 *	Number of bidirectional Streams
 *	64bit ready
 *	CORB and RIRB sizes
 */
bool VoodooHDADevice::getCapabilities()
{
	bool result = false;
	UInt16 globalCap;
	UInt8 corbSizeReg, rirbSizeReg;

//	logMsg("VoodooHDADevice[%p]::getCapabilities\n", this);

	globalCap = readData16(HDAC_GCAP);
	mInStreamsSup = HDAC_GCAP_ISS(globalCap);
	mOutStreamsSup = HDAC_GCAP_OSS(globalCap);
	mBiStreamsSup = HDAC_GCAP_BSS(globalCap);
	mSDO = HDAC_GCAP_NSDO(globalCap);

	mSupports64Bit = HDA_FLAG_MATCH(globalCap, HDAC_GCAP_64OK);
	if ((mDeviceId & ~0x30000U) == HDA_NVIDIA_MCP78_1)	// Quirk from HDAC
		mSupports64Bit = false;

	corbSizeReg = readData8(HDAC_CORBSIZE);
	if ((corbSizeReg & HDAC_CORBSIZE_CORBSZCAP_256) == HDAC_CORBSIZE_CORBSZCAP_256)
		mCorbSize = 256;
	else if ((corbSizeReg & HDAC_CORBSIZE_CORBSZCAP_16) == HDAC_CORBSIZE_CORBSZCAP_16)
		mCorbSize = 16;
	else if ((corbSizeReg & HDAC_CORBSIZE_CORBSZCAP_2) == HDAC_CORBSIZE_CORBSZCAP_2)
		mCorbSize = 2;
	else {
		errorMsg("error: invalid CORB size: %02x\n", corbSizeReg);
		goto done;
	}

	rirbSizeReg = readData8(HDAC_RIRBSIZE);
	if ((rirbSizeReg & HDAC_RIRBSIZE_RIRBSZCAP_256) == HDAC_RIRBSIZE_RIRBSZCAP_256)
		mRirbSize = 256;
	else if ((rirbSizeReg & HDAC_RIRBSIZE_RIRBSZCAP_16) == HDAC_RIRBSIZE_RIRBSZCAP_16)
		mRirbSize = 16;
	else if ((rirbSizeReg & HDAC_RIRBSIZE_RIRBSZCAP_2) == HDAC_RIRBSIZE_RIRBSZCAP_2)
		mRirbSize = 2;
	else {
		errorMsg("error: invalid RIRB size: %02x\n", rirbSizeReg);
		goto done;
	}

//	logMsg("    CORB size: %d\n", mCorbSize);
//	logMsg("    RIRB size: %d\n", mRirbSize);
//	logMsg("      Streams: ISS=%d OSS=%d BSS=%d\n", mInStreamsSup, mOutStreamsSup, mBiStreamsSup);

	ASSERT(mCorbSize);
	ASSERT(mRirbSize);

	result = true;
done:
	return result;
}

/******************************************************************************************/
/******************************************************************************************/

UInt8 VoodooHDADevice::readData8(UInt32 offset)
{
	return *((UInt8 *) mRegBase + offset);
}

UInt16 VoodooHDADevice::readData16(UInt32 offset)
{
	return *(UInt16 *) ((UInt8 *) mRegBase + offset);
}

UInt32 VoodooHDADevice::readData32(UInt32 offset)
{
	return *(UInt32 *) ((UInt8 *) mRegBase + offset);
}

void VoodooHDADevice::writeData8(UInt32 offset, UInt8 value)
{
	*((UInt8 *) mRegBase + offset) = value;
}

void VoodooHDADevice::writeData16(UInt32 offset, UInt16 value)
{
	*(UInt16 *) ((UInt8 *) mRegBase + offset) = value;
}

void VoodooHDADevice::writeData32(UInt32 offset, UInt32 value)
{
	*(UInt32 *) ((UInt8 *) mRegBase + offset) = value;
}

/******************************************************************************************/
/******************************************************************************************/

void VoodooHDADevice::lockMsgBuffer()
{
	ASSERT(mMessageLock);
	IOLockLock(mMessageLock);
}

void VoodooHDADevice::unlockMsgBuffer()
{
	ASSERT(mMessageLock);
	IOLockUnlock(mMessageLock);
}

void VoodooHDADevice::enableMsgBuffer(bool isEnabled)
{
	if (mMsgBufferEnabled == isEnabled) {
		errorMsg("warning: enableMsgBuffer(%d) has no effect\n", isEnabled);
		return;
	}

	lockMsgBuffer();
	mMsgBufferEnabled = isEnabled;
	if (isEnabled) {
		bzero(mMsgBuffer, mMsgBufferSize);
		mMsgBufferPos = 0;
	}
	unlockMsgBuffer();
}

void VoodooHDADevice::logMsg(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	messageHandler(kVoodooHDAMessageTypeGeneral, format, args);
	va_end(args);
}

void VoodooHDADevice::errorMsg(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	messageHandler(kVoodooHDAMessageTypeError, format, args);
	va_end(args);
}

void VoodooHDADevice::dumpMsg(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	messageHandler(kVoodooHDAMessageTypeDump, format, args);
	va_end(args);
}

void VoodooHDADevice::messageHandler(UInt32 type, const char *format, va_list args)
{
	bool lockExists;
	int length;

	ASSERT(type);
	ASSERT(format);
	ASSERT(args);

	lockExists = (!isInactive() && mMessageLock);
	if (lockExists)
		lockMsgBuffer(); // utilize message buffer lock for console logging as well

	switch (type) {

	case kVoodooHDAMessageTypeGeneral:
		if (mVerbose < 1)
			break;
	case kVoodooHDAMessageTypeError:
		vprintf(format, args);
		break;
	case kVoodooHDAMessageTypeDump:
		if (mVerbose >= 2) {
			va_list args2;
			va_copy(args2, args);
			vprintf(format, args2);
			va_end(args2);
		}
		if (lockExists && mMsgBufferEnabled) {
			//ASSERT(mMsgBufferPos < (mMsgBufferSize - 1));
			if(mMsgBufferPos > (mMsgBufferSize - 255)) break;
			if (mMsgBufferPos != (mMsgBufferSize - 256)) {
				length = vsnprintf(mMsgBuffer + mMsgBufferPos, mMsgBufferSize - mMsgBufferPos,
						format, args);
				if (length > 0 && length < 255)
					mMsgBufferPos += length;
				else if (length < 0)
					IOLog("warning: vsnprintf in dumpMsg failed\n");
			}
		}
		break;
	default:
		BUG("unknown message type");
	}

	if (lockExists)
		unlockMsgBuffer();
}

IOReturn VoodooHDADevice::runAction(UInt32 *action, UInt32 *outSize, void **outData, void *extraArg)
{
	//logMsg("VoodooHDADevice[%p]::runAction(0x%lx, %p, %p, %p)\n", this, *action, outSize, outData, extraArg);

	ASSERT(outSize);
	ASSERT(outData);

	ASSERT(commandGate);
	ASSERT(mActionHandler);

	return commandGate->runAction(mActionHandler, action, (UInt32 *) outSize, (void *) outData,
			extraArg);
}

__attribute__((visibility("hidden")))
IOReturn VoodooHDADevice::handleAction(OSObject *owner, void *arg0, void *arg1, void *arg2,
		__unused void *arg3)
{
	VoodooHDADevice *device;
	IOReturn result = kIOReturnSuccess;
	UInt32 action = *static_cast<UInt32 const*>(arg0);

	UInt32 *outSize = (UInt32 *) arg1;
	void **outData = (void **) arg2;

	device = OSDynamicCast(VoodooHDADevice, owner);
	if (!device)
		return kIOReturnBadArgument;

	//device->logMsg("VoodooHDADevice[%p]::handleAction(0x%lx, %p, %p)\n", owner, action, outSize, outData);

	if((action & 0xFF)  == kVoodooHDAActionSetMixer) {
		 //Команда от PrefPanel
		UInt8 value;  // slide value
		UInt8 sliderNum;  //OSS device
		UInt8 tabNum; //Channel number

		tabNum = ((action >> 8) & 0xFF);
		sliderNum = ((action >> 16) & 0xFF);
		value = ((action >> 24) & 0xFF);

		device->changeSliderValue(tabNum, sliderNum, value);

		*outSize = 0;
		*outData = NULL;

		return result;
	}

	if((action & 0x60)  == kVoodooHDAActionSetMath) {
		UInt8 ch, opt, val;
		ch = ((action >> 8) & 0xFF);
		opt = ((action >> 16) & 0xFF);
		val = ((action >> 24) & 0xFF);
		//IOLog("HDA: Channel=%02x Options=%02x Value=%02x\n", ch, opt, val);
			  //device->vectorize?"Yes":"No", device->noiseLevel,
			  //device->useStereo?"Yes":"No", device->StereoBase);


		if (ch < device->nSliderTabsCount) {
			device->lockPrefPanelMemoryBuf();
			device->mPrefPanelMemoryBuf[ch].vectorize = ((opt & 0x1) == 1);
			device->mPrefPanelMemoryBuf[ch].noiseLevel = (val & 0x0F);
			device->mPrefPanelMemoryBuf[ch].useStereo = ((opt & 0x2) == 2);
			device->mPrefPanelMemoryBuf[ch].StereoBase = (val & 0xF0) >> 4;
			device->unlockPrefPanelMemoryBuf();
		}

		device->setMath(ch, opt, val);
/*
		device->vectorize = ((opt & 0x1) == 1);
		device->noiseLevel = (val & 0x0F);
		device->useStereo = ((opt & 0x2) == 2);
		device->StereoBase = (val & 0xF0) >> 4;
*/
		*outSize = 0;
		*outData = NULL;

		return result;		
		}

	//Команда от моей версии getDump для обновления данных об усилении
	if((action & 0xFF)  == kVoodooHDAActionGetMixers) {

//		device->LOCK();
		device->updateExtDump();
//		device->UNLOCK();

		*outSize = 0;
		*outData = NULL;

		return result;
	}


	switch (action) {
	case kVoodooHDAActionTest:
		device->logMsg("test action received\n");
		*outSize = 0;
		*outData = NULL;
		break;
	default:
		result = kIOReturnBadArgument;
		*outSize = 0;
		*outData = NULL;
	}

	return result;
}
// from v0.2.2
/******************************************************************************************/
ChannelInfo *VoodooHDADevice::getChannelInfo() {
	int ossDev=1, i=0;
	ChannelInfo *info = (ChannelInfo*)allocMem(sizeof(ChannelInfo) * mNumChannels);
	VoodooHDAEngine *engine;
	const char *pName;
	
	for(; i < mNumChannels; i++) {
		engine = lookupEngine(i);
		if (!engine)
			continue;
		pName = engine->getPortName();
		snprintf(info[i].name, strlen(pName)+1, "%s", pName);
		info[i].numChannels = mNumChannels;

		// initialise
		// We dont want to control Master Volume in our PrefPane -> we start at ossDev=1
		for(ossDev = 1 ; ossDev < SOUND_MIXER_NRDEVICES ; ossDev++) {
			const char *name;
			UInt32 ossMask;
			//name = engine->getOssDevName(ossDev);
			name = engine->mPortName;
			
			if (engine->getEngineDirection() == kIOAudioStreamDirectionOutput)
				ossMask = engine->mChannel->pcmDevice->devMask;
			else
				ossMask = engine->mChannel->pcmDevice->recDevMask;
			
			info[i].mixerValues[ossDev-1].enabled = false;			
			if ((ossMask & (1 << ossDev)) == 0)
				continue;
			
			info[i].mixerValues[ossDev-1].mixId = ossDev;
			info[i].mixerValues[ossDev-1].enabled = true;
			info[i].mixerValues[ossDev-1].value = engine->mChannel->pcmDevice->left[ossDev];// mMixerDefaults[ossDev];
			snprintf(info[i].mixerValues[ossDev-1].name, strlen(name)+1, "%s", name);
		}
		info[i].mixerValues[24].mixId = 0;
		info[i].mixerValues[24].enabled = true;
		info[i].mixerValues[24].value = engine->mChannel->pcmDevice->left[0];// mMixerDefaults[ossDev];
		info[i].vectorize = engine->mChannel->vectorize;
		info[i].noiseLevel = engine->mChannel->noiseLevel;
		info[i].useStereo = engine->mChannel->useStereo;
		info[i].StereoBase = engine->mChannel->StereoBase;
	}
	
	return info;
}

/******************************************************************************************/
/******************************************************************************************/

bool VoodooHDADevice::setupWorkloop()
{
	//logMsg("VoodooHDADevice[%p]::setupWorkloop\n", this);

	mWorkLoop = IOWorkLoop::workLoop(); // create our own workloop (super has workLoop member)

	mTimerSource = IOTimerEventSource::timerEventSource(this,
			(IOTimerEventSource::Action) &VoodooHDADevice::timeoutOccurred);
	if (!mTimerSource) {
		errorMsg("error: couldn't allocate timer event source\n");
		return false;
	}
	mTimerSource->disable();
	if (mWorkLoop->addEventSource(mTimerSource) != kIOReturnSuccess) {
		errorMsg("error: couldn't add timer event source to workloop\n");
		return false;
	}
	mTimerSource->setTimeoutMS(5000);

	IOService* _provider = getProvider();
	mInterruptSource = IOFilterInterruptEventSource::filterInterruptEventSource(this,
			(IOInterruptEventAction) &VoodooHDADevice::interruptHandler,
			(IOFilterInterruptEventSource::Filter) &VoodooHDADevice::interruptFilter,
			_provider, findInterruptIndex(_provider, mAllowMSI));
	if (!mInterruptSource) {
		errorMsg("error: couldn't allocate interrupt event source\n");
		return false;
	}
	mInterruptSource->disable();
	if (mWorkLoop->addEventSource(mInterruptSource) != kIOReturnSuccess) {
		errorMsg("error: couldn't add interrupt event source to workloop\n");
		return false;
	}

	return true;
}

__attribute__((noinline, visibility("hidden")))
int VoodooHDADevice::findInterruptIndex(IOService* target, bool allowMSI)
{
	int source, interruptType, pinInterruptToUse, msgInterruptToUse;
	bool isMsgInterruptFound;

	isMsgInterruptFound = false;
	pinInterruptToUse = 0;
	msgInterruptToUse = 0;
	for (interruptType = 0, source = 0;
		 target->getInterruptType(source, &interruptType) == kIOReturnSuccess;
		 ++source)
		if (interruptType & kIOInterruptTypePCIMessaged) {
			if (!isMsgInterruptFound) {
				isMsgInterruptFound = true;
				msgInterruptToUse = source;
			}
		} else
			pinInterruptToUse = source;
	return (isMsgInterruptFound && allowMSI) ? msgInterruptToUse : pinInterruptToUse;
}

void VoodooHDADevice::enableEventSources()
{
	//logMsg("VoodooHDADevice[%p]::enableEventSources\n", this);

	if (mInterruptSource)
		mInterruptSource->enable();
	if (mTimerSource && (mVerbose >= 3))
		mTimerSource->enable();
}

void VoodooHDADevice::disableEventSources()
{
	//logMsg("VoodooHDADevice[%p]::disableEventSources\n", this);

	if (mTimerSource)
		mTimerSource->disable();
	if (mInterruptSource)
		mInterruptSource->disable();
}

__attribute__((visibility("hidden")))
bool VoodooHDADevice::interruptFilter(OSObject *owner, __unused IOFilterInterruptEventSource *source)
{
	VoodooHDADevice *device;
	UInt32 status;

#if 0
	device = OSDynamicCast(VoodooHDADevice, owner);
#else
	/*
	 * Not sure OSDynamicCast is safe in interrupt context, and
	 *   anyhow, this function will never get called with any
	 *   other OSObject. - Zenith432
	 */
	device = static_cast<VoodooHDADevice*>(owner);
#endif
	if (!device)
		return false;

	status = *(UInt32 volatile*) ((UInt8 *) device->mRegBase + HDAC_INTSTS);
	if (!HDA_FLAG_MATCH(status, HDAC_INTSTS_GIS))
		return false;
	/*
	 * Note: If multiple stream primary interrupts take place
	 *   before threaded handler, last time-stamp is used.
	 */
	if (status & HDAC_INTSTS_SIS_MASK)
		device->mIntrTimeStamp = mach_absolute_time();
	*(UInt32 volatile*) ((UInt8 *) device->mRegBase + HDAC_INTSTS) = status;
	OSBitOrAtomic(status, &device->mIntStatus);

	return true;
}

__attribute__((visibility("hidden")))
void VoodooHDADevice::interruptHandler(OSObject *owner, __unused IOInterruptEventSource *source,
		__unused int count)
{
#if 0
	VoodooHDADevice *device = OSDynamicCast(VoodooHDADevice, owner);
#else
	/*
	 * Not interrupt context anymore, but OSDynamicCast
	 *   still not needed - Zenith432.
	 */
	VoodooHDADevice *device = static_cast<VoodooHDADevice*>(owner);
#endif
	if (!device)
		return;
	device->handleInterrupt();
}

VoodooHDAEngine *VoodooHDADevice::lookupEngine(int channelId)
{
	OSCollectionIterator *engineIter;
	VoodooHDAEngine *engine = NULL;

	engineIter = OSCollectionIterator::withCollection(audioEngines);
	engineIter->reset();
	while ((engine = OSDynamicCast(VoodooHDAEngine, engineIter->getNextObject()))) {
//		ASSERT(OSDynamicCast(VoodooHDAEngine, engine));
		if (engine->getEngineId() == channelId)
			break;
	}
	RELEASE(engineIter);

	return engine;
}

void VoodooHDADevice::handleChannelInterrupt(int channelId)
{
	VoodooHDAEngine *engine;

	mTotalChanInt++;

	engine = lookupEngine(channelId);
	if (!engine) {
		errorMsg("warning: couldn't find engine matching channel %d\n", channelId);
		return;
	}
	engine->takeTimeStamp(true, reinterpret_cast<AbsoluteTime*>(&mIntrTimeStamp));
}

/******************************************************************************************/
/******************************************************************************************/

void VoodooHDADevice::lock(const char *callerName)
{
	if (mVerbose >= 4)
		logMsg("VoodooHDADevice[%p]::lock(%s)\n", this, callerName);
	ASSERT(mLock);
	IOLockLock(mLock);
}

void VoodooHDADevice::unlock(const char *callerName)
{
	if (mVerbose >= 4)
		logMsg("VoodooHDADevice[%p]::unlock(%s)\n", this, callerName);
	ASSERT(mLock);
	IOLockUnlock(mLock);
}

void VoodooHDADevice::assertLock(IOLock *lock, UInt32 type)
{
	lck_mtx_t *mutex;
	ASSERT(lock);
	ASSERT(type); // type can be either LCK_MTX_ASSERT_OWNED or LCK_MTX_ASSERT_NOTOWNED
	mutex = IOLockGetMachLock(lock);
	ASSERT(mutex);
//	lck_mtx_assert(mutex, type);
}

extern "C" {
	extern void *kern_os_malloc(size_t size);
	extern void *kern_os_realloc(void *addr, size_t size);
	extern void kern_os_free(void *addr);
}

__attribute__((visibility("hidden")))
void *VoodooHDADevice::allocMem(size_t size)
{
	void *addr = kern_os_malloc(size);
//	ASSERT(addr); //will check result
	return addr;
}

__attribute__((visibility("hidden")))
void *VoodooHDADevice::reallocMem(void *addr, size_t size)
{
	void *newAddr = kern_os_realloc(addr, size);
//	ASSERT(newAddr); //will check result
	return newAddr;
}

__attribute__((visibility("hidden")))
void VoodooHDADevice::freeMem(void *addr)
{
	ASSERT(addr);
	kern_os_free(addr);
}

DmaMemory *VoodooHDADevice::allocateDmaMemory(mach_vm_size_t size, const char *description, UInt32 cacheOption)
{
	mach_vm_address_t physMask;
	IOBufferMemoryDescriptor *memDesc = NULL;
	DmaMemory *dmaMemory = NULL;
	UInt64 segAddr;
	IOByteCount segLength;
	void* virtAddr;

	ASSERT(size);
	ASSERT(description);

//	logMsg("VoodooHDADevice::allocateDmaMemory(%llu, %s)\n", size, description);

	if (mSupports64Bit)
		physMask = -HDAC_DMA_ALIGNMENT;
	else
		physMask = -HDAC_DMA_ALIGNMENT & UINT32_MAX;
	cacheOption &= kIOMapCacheMask;
	if (!cacheOption) {
		cacheOption = mInhibitCache ? kIOMapInhibitCache : kIOMapDefaultCache;
    }
	memDesc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
			kIOMemoryPhysicallyContiguous | kIODirectionInOut | cacheOption, size, physMask);
	
	if (!memDesc) {
		errorMsg("error: IOBufferMemoryDescriptor::inTaskWithPhysicalMask failed\n");
		goto failed;
	}
	ASSERT(memDesc->getLength() == size);

	/*
	 * Note: memDesc is kIOMemoryAutoPrepare, but just to conform...
	 */
	if (memDesc->prepare() != kIOReturnSuccess) {
		errorMsg("error: IOMemoryDescriptor::prepare failed\n");
		goto failed;
	}

	segAddr = memDesc->getPhysicalSegment(0U, &segLength, 0U);
	if (!segAddr) {
		errorMsg("error: IOBufferMemoryDescriptor::getPhysicalSegment failed\n");
		memDesc->complete();
		goto failed;
	}

	virtAddr = memDesc->getBytesNoCopy();
	ASSERT(virtAddr);
	bzero(virtAddr, size);

	dmaMemory = new DmaMemory;
	dmaMemory->description = description;
	dmaMemory->md = memDesc;
	dmaMemory->size = size;
	dmaMemory->physAddr = segAddr;
	dmaMemory->virtAddr = reinterpret_cast<IOVirtualAddress>(virtAddr);

//	logMsg("%s: allocated %llu bytes DMA memory (phys: %#llx, virt: %p)\n",
//			dmaMemory->description, dmaMemory->size, dmaMemory->physAddr, virtAddr);

	return dmaMemory;

failed:
	RELEASE(memDesc);
	DELETE(dmaMemory);

	return NULL;
}

void VoodooHDADevice::freeDmaMemory(DmaMemory *dmaMemory)
{
	ASSERT(dmaMemory);

	dmaMemory->description = NULL;
	dmaMemory->size = 0;
	dmaMemory->physAddr = 0;
	dmaMemory->virtAddr = 0;

	if (dmaMemory->md)
		dmaMemory->md->complete();
	RELEASE(dmaMemory->md);

	DELETE(dmaMemory);
}

/******************************************************************************************/
/******************************************************************************************/

/*
 * Wrapper function that sends only one command to a given codec
 */
UInt32 VoodooHDADevice::sendCommand(UInt32 verb, nid_t cad)
{
	CommandList cmdList;
	UInt32 response = HDAC_INVALID;

	//assertLock(mLock, LCK_MTX_ASSERT_OWNED);

	cmdList.numCommands = 1;
	cmdList.verbs = &verb;
	cmdList.responses = &response;

	sendCommands(&cmdList, cad);

	return response;
}

/*
 * Send a command list to the codec via the corb. We queue as much verbs as
 * we can and msleep on the codec. When the interrupt get the responses
 * back from the rirb, it will wake us up so we can queue the remaining verbs
 * if any.
 */
void VoodooHDADevice::sendCommands(CommandList *commands, nid_t cad)
{
	Codec *codec;
	int corbReadPtr;
	UInt32 *corb;
	int timeout;
	int retry = 10;

	if (!mCodecs[cad] || !commands || (commands->numCommands < 1))
		return;

	codec = mCodecs[cad];
	codec->commands = commands;
	codec->numRespReceived = 0;
	codec->numVerbsSent = 0;
	corb = (UInt32 *) mCorbMem->virtAddr;

	do {
		if (codec->numVerbsSent != commands->numCommands) {
			/* Queue as many verbs as possible */
			corbReadPtr = readData16(HDAC_CORBRP);
			while ((codec->numVerbsSent != commands->numCommands) &&
					(((mCorbWritePtr + 1) % mCorbSize) != corbReadPtr)) {
				mCorbWritePtr++;
				mCorbWritePtr %= mCorbSize;
				corb[mCorbWritePtr] = commands->verbs[codec->numVerbsSent++];
			}

			/* Send the verbs to the codecs */
			writeData16(HDAC_CORBWP, mCorbWritePtr);
		}

#if 0
		/*
		 * Lock testing code
		 */
		if (IOLockTryLock(mLock)) {
			IOLog("%s: Was unlocked, should have been locked!!\n", __FUNCTION__);
			IOLockUnlock(mLock);
		}
#endif
		timeout = 200;
		while ((rirbFlush() == 0) && --timeout)
			IODelay(10);
	} while (((codec->numVerbsSent != commands->numCommands) ||
			(codec->numRespReceived != commands->numCommands)) && --retry);

	if (retry == 0)
		errorMsg("TIMEOUT numcmd=%d, sent=%d, received=%d\n", commands->numCommands,
				codec->numVerbsSent, codec->numRespReceived);

	codec->commands = NULL;
	codec->numRespReceived = 0;
	codec->numVerbsSent = 0;

	unsolqFlush();
}

/*
 * Initialize the corb registers for operations but do not start it up yet.
 * The CORB engine must not be running when this function is called.
 */
void VoodooHDADevice::initCorb()
{
	UInt8 corbSizeReg;
	UInt64 corbPhysAddr;

//	logMsg("VoodooHDADevice[%p]::initCorb\n", this);

	/* Setup the CORB size. */
	switch (mCorbSize) {
	case 256:
		corbSizeReg = HDAC_CORBSIZE_CORBSIZE(HDAC_CORBSIZE_CORBSIZE_256);
		break;
	case 16:
		corbSizeReg = HDAC_CORBSIZE_CORBSIZE(HDAC_CORBSIZE_CORBSIZE_16);
		break;
	case 2:
		corbSizeReg = HDAC_CORBSIZE_CORBSIZE(HDAC_CORBSIZE_CORBSIZE_2);
		break;
	default:
		BUG("invalid CORB size");
	}
	writeData8(HDAC_CORBSIZE, corbSizeReg);

	/* Setup the CORB Address in the hdac */
	corbPhysAddr = (uint64_t)mCorbMem->physAddr;
	writeData32(HDAC_CORBLBASE, (UInt32) corbPhysAddr);
	writeData32(HDAC_CORBUBASE, (UInt32) (corbPhysAddr >> 32));

	/* Set the WP and RP */
	mCorbWritePtr = 0;
	writeData16(HDAC_CORBWP, mCorbWritePtr);
	writeData16(HDAC_CORBRP, HDAC_CORBRP_CORBRPRST);
	/* The HDA specification indicates that the CORBRPRST bit will always
	 * read as zero. Unfortunately, it seems that at least the 82801G
	 * doesn't reset the bit to zero, which stalls the corb engine.
	 * manually reset the bit to zero before continuing. */
	writeData16(HDAC_CORBRP, 0);

#if 0
	/* Enable CORB error reporting */
	writeData8(HDAC_CORBCTL, HDAC_CORBCTL_CMEIE);
#endif
}

/*
 * Initialize the rirb registers for operations but do not start it up yet.
 * The RIRB engine must not be running when this function is called.
 */
void VoodooHDADevice::initRirb()
{
	UInt8 rirbSizeReg;
	UInt64 rirbPhysAddr;

//	logMsg("VoodooHDADevice[%p]::initRirb\n", this);

	/* Setup the RIRB size. */
	switch (mRirbSize) {
	case 256:
		rirbSizeReg = HDAC_RIRBSIZE_RIRBSIZE(HDAC_RIRBSIZE_RIRBSIZE_256);
		break;
	case 16:
		rirbSizeReg = HDAC_RIRBSIZE_RIRBSIZE(HDAC_RIRBSIZE_RIRBSIZE_16);
		break;
	case 2:
		rirbSizeReg = HDAC_RIRBSIZE_RIRBSIZE(HDAC_RIRBSIZE_RIRBSIZE_2);
		break;
	default:
		BUG("invalid RIRB size");
	}
	writeData8(HDAC_RIRBSIZE, rirbSizeReg);

	/* Setup the RIRB Address in the hdac */
	rirbPhysAddr = mRirbMem->physAddr;
	writeData32(HDAC_RIRBLBASE, (UInt32) rirbPhysAddr);
	writeData32(HDAC_RIRBUBASE, (UInt32) (rirbPhysAddr >> 32));

	/* Setup the WP and RP */
	mRirbReadPtr = 0;
	writeData16(HDAC_RIRBWP, HDAC_RIRBWP_RIRBWPRST);

	/* Setup the interrupt threshold */
	writeData16(HDAC_RINTCNT, mRirbSize / 2);

	/* Enable Overrun and response received reporting */
#if 0
	writeData8(HDAC_RIRBCTL, HDAC_RIRBCTL_RIRBOIC | HDAC_RIRBCTL_RINTCTL);
#else
	writeData8(HDAC_RIRBCTL, HDAC_RIRBCTL_RINTCTL);
#endif
}

/*
 * Startup the corb DMA engine
 */
void VoodooHDADevice::startCorb()
{
	UInt32 corbCtl;
	corbCtl = readData8(HDAC_CORBCTL);
	corbCtl |= HDAC_CORBCTL_CORBRUN;
	writeData8(HDAC_CORBCTL, corbCtl);
}

/*
 * Startup the rirb DMA engine
 */
void VoodooHDADevice::startRirb()
{
	UInt32 rirbCtl;
	rirbCtl = readData8(HDAC_RIRBCTL);
	rirbCtl |= HDAC_RIRBCTL_RIRBDMAEN;
	writeData8(HDAC_RIRBCTL, rirbCtl);
}

/********************************************************************************************/
/********************************************************************************************/

int VoodooHDADevice::rirbFlush()
{
	RirbResponse *rirbBase;
	UInt8 rirbWritePtr;
	int ret;

	rirbBase = (RirbResponse *) mRirbMem->virtAddr;
	rirbWritePtr = readData8(HDAC_RIRBWP);

	ret = 0;
	while (mRirbReadPtr != rirbWritePtr) {
		RirbResponse *rirb;
		Codec *codec;
		CommandList *commands;
		nid_t cad;
		UInt32 resp;

		mRirbReadPtr++;
		mRirbReadPtr %= mRirbSize;
		rirb = &rirbBase[mRirbReadPtr];
		cad = HDAC_RIRB_RESPONSE_EX_SDATA_IN(rirb->response_ex);
		if ((cad < 0) || (cad >= HDAC_CODEC_MAX) || !mCodecs[cad])
			continue;
		resp = rirb->response;
		codec = mCodecs[cad];
		commands = codec->commands;
		if (rirb->response_ex & HDAC_RIRB_RESPONSE_EX_UNSOLICITED) {
			mUnsolq[mUnsolqWritePtr++] = (cad << 16) | ((resp >> 26) & 0xffff);
			mUnsolqWritePtr %= HDAC_UNSOLQ_MAX;
		} else if (commands && (commands->numCommands > 0) &&
				(codec->numRespReceived < commands->numCommands))
			commands->responses[codec->numRespReceived++] = resp;
		ret++;
	}
		//Slice
	UInt32 rirbCtl;
	rirbCtl = readData8(HDAC_GCTL);
	rirbCtl |= HDAC_GCTL_FCNTRL;
	writeData8(HDAC_GCTL, rirbCtl);
	
	return ret;
}

int VoodooHDADevice::unsolqFlush()
{
	int ret = 0;

	if (mUnsolqState == HDAC_UNSOLQ_READY) {
		mUnsolqState = HDAC_UNSOLQ_BUSY;
		while (mUnsolqReadPtr != mUnsolqWritePtr) {
			nid_t cad;
			UInt32 tag;
			cad = mUnsolq[mUnsolqReadPtr] >> 16;
			tag = mUnsolq[mUnsolqReadPtr++] & 0xffff;
			mUnsolqReadPtr %= HDAC_UNSOLQ_MAX;
			handleUnsolicited(mCodecs[cad], tag);
			ret++;
		}
		mUnsolqState = HDAC_UNSOLQ_READY;
	}

	return ret;
}

/*
 * Unsolicited messages handler.
 */
void VoodooHDADevice::handleUnsolicited(Codec *codec, UInt32 tag)
{
	FunctionGroup *funcGroup = NULL;

	if (!codec)
		return;

//	logMsg("Unsol Tag: 0x%08lx\n", tag);

	for (int i = 0; i < codec->numFuncGroups; i++) {
		if (codec->funcGroups[i].nodeType == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO) {
			funcGroup = &codec->funcGroups[i];
			break;
		}
	}

	if (!funcGroup)
		return;

	switch (tag) {
	case HDAC_UNSOLTAG_EVENT_HP:
		switchHandler(funcGroup, false);
		break;
	default:
		errorMsg("Unknown unsol tag: 0x%08lx!\n", (long unsigned int)tag);
		break;
	}
}

/********************************************************************************************/
/********************************************************************************************/

int VoodooHDADevice::handleStreamInterrupt(Channel *channel)
{
	/* XXX to be removed */
	UInt32 res;

	if (!(channel->flags & HDAC_CHN_RUNNING))
		return 0;

	/* XXX to be removed */
	res = readData8(channel->off + HDAC_SDSTS);

	/* XXX to be removed */
	if (res & (HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE))
		errorMsg("PCMDIR_%s intr triggered beyond stream boundary: %08lx\n",
				(channel->direction == PCMDIR_PLAY) ? "PLAY" : "REC", (long unsigned int)res);

	writeData8(channel->off + HDAC_SDSTS, HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS);

	/* XXX to be removed */
	if (res & HDAC_SDSTS_BCIS)
		return 1;

	return 0;
}

/* Make room for possible 4096 playback/record channels, in 100 years to come. */

#define HDAC_TRIGGER_NONE	0x00000000
#define HDAC_TRIGGER_PLAY	0x00000fff
#define HDAC_TRIGGER_REC	0x00fff000
#define HDAC_TRIGGER_UNSOL	0x80000000

void VoodooHDADevice::handleInterrupt()
{
	UInt32 status, trigger;

	mTotalInt++;
	status = OSBitAndAtomic(0U, &mIntStatus);
	while (HDA_FLAG_MATCH(status, HDAC_INTSTS_GIS)) {

		trigger = 0;
		
		LOCK();

		/* Was this a controller interrupt? */
		if (HDA_FLAG_MATCH(status, HDAC_INTSTS_CIS)) {
			UInt8 rirbStatus = readData8(HDAC_RIRBSTS);
			/* Get as many responses that we can */
			while (HDA_FLAG_MATCH(rirbStatus, HDAC_RIRBSTS_RINTFL)) {
				writeData8(HDAC_RIRBSTS, HDAC_RIRBSTS_RINTFL);
				if (rirbFlush() != 0)
					trigger |= HDAC_TRIGGER_UNSOL;
				rirbStatus = readData8(HDAC_RIRBSTS);
			}
		}

		if (status & HDAC_INTSTS_SIS_MASK) {
			for (int i = 0; i < mNumChannels; i++) {
				if ((status & (1 << (mChannels[i].off >> 5))) &&
				    (handleStreamInterrupt(&mChannels[i]) != 0))
					trigger |= (1 << i);
			}
		}

		for (int i = 0; i < mNumChannels; i++)
			if (trigger & (1 << i))
				handleChannelInterrupt(i);
		if (trigger & HDAC_TRIGGER_UNSOL)
			unsolqFlush();

		UNLOCK();

		status = OSBitAndAtomic(0U, &mIntStatus);
	}
}

__attribute__((visibility("hidden")))
void VoodooHDADevice::timeoutOccurred(OSObject *owner, IOTimerEventSource *source)
{
	VoodooHDADevice *device = OSDynamicCast(VoodooHDADevice, owner);
	if (!device)
		return;

	device->logMsg("total interrupts: %lld (%lld channel interrupts)\n", device->mTotalInt,
			device->mTotalChanInt);

	source->setTimeoutMS(5000);
}

/********************************************************************************************/
/********************************************************************************************/

int VoodooHDADevice::audioCtlOssMixerInit(PcmDevice *pcmDevice)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	AudioControl *control;
	UInt32 mask, recmask, id;
	int softpcmvol;

//	logMsg("VoodooHDADevice[%p]::audioCtlOssMixerInit(%p)\n", this, pcmDevice);

	/* Make sure that in case of soft volume it won't stay muted. */
	for (int i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		pcmDevice->left[i] = 100;
		pcmDevice->right[i] = 100;
	}

	mask = 0;
	recmask = 0;
	id = CODEC_ID(funcGroup->codec);

	/* Declare EAPD as ogain control. */
	if (pcmDevice->playChanId >= 0) {
		for (int i = funcGroup->startNode; i < funcGroup->endNode; i++) {
			Widget *widget = widgetGet(funcGroup, i);
			if (!widget || (widget->enable == 0))
				continue;
			if ((widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) ||
			    	(widget->params.eapdBtl == HDAC_INVALID) ||
					(widget->bindAssoc != mChannels[pcmDevice->playChanId].assocNum))
				continue;
			mask |= SOUND_MASK_OGAIN;
			break;
		}
	}

	/* Declare volume controls assigned to this association. */
	control = NULL;
	for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
		if (control->enable == 0)
			continue;
		if (((pcmDevice->playChanId >= 0) &&
				(control->widget->bindAssoc == mChannels[pcmDevice->playChanId].assocNum)) ||
		    	((pcmDevice->recChanId >= 0) &&
				(control->widget->bindAssoc == mChannels[pcmDevice->recChanId].assocNum)) ||
				((control->widget->bindAssoc == -2) && (pcmDevice->index == 0)))
			mask |= control->ossmask;
	}

	/* Declare record sources available to this association. */
	if (pcmDevice->recChanId >= 0) {
		Channel *channel = &mChannels[pcmDevice->recChanId];
		for (int i = 0; channel->io[i] != -1; i++) {
			Widget *widget = widgetGet(funcGroup, channel->io[i]);
			if (!widget || (widget->enable == 0))
				continue;
			for (int j = 0; j < widget->nconns; j++) {
				Widget *childWidget;
				if (widget->connsenable[j] == 0)
					continue;
				childWidget = widgetGet(funcGroup, widget->conns[j]);
				if (!childWidget || (childWidget->enable == 0))
					continue;
				if ((childWidget->bindAssoc != mChannels[pcmDevice->recChanId].assocNum) &&
						(childWidget->bindAssoc != -2))
					continue;
				recmask |= childWidget->ossmask;
			}
		}
	}

	/* Declare soft PCM volume if needed. */
	if (pcmDevice->playChanId >= 0 && !pcmDevice->digital) {
		control = NULL;
		if ((mask & SOUND_MASK_PCM) == 0 ||
				(funcGroup->audio.quirks & HDA_QUIRK_SOFTPCMVOL)) {
			softpcmvol = 1;
			mask |= SOUND_MASK_PCM;
		} else {
			softpcmvol = 0;
			for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
				if (control->enable == 0)
					continue;
				if ((control->widget->bindAssoc != mChannels[pcmDevice->playChanId].assocNum) &&
				    	((control->widget->bindAssoc != -2) || (pcmDevice->index != 0)))
					continue;
				if (!(control->ossmask & SOUND_MASK_PCM))
					continue;
				if (control->step > 0)
					break;
			}
		}

		if ((softpcmvol == 1) || !control) {
#if 0
			pcm_setflags(pcmDevice->dev, pcm_getflags(pcmDevice->dev) | SD_F_SOFTPCMVOL);
#else
//			logMsg("XXX pcm_setflags SD_F_SOFTPCMVOL\n");
#endif
//			logMsg("%s Soft PCM volume\n", (softpcmvol == 1) ? "Forcing" : "Enabling");
		}
	}

	/* Declare master volume if needed. */
	if (pcmDevice->playChanId >= 0) {
		if ((mask & (SOUND_MASK_VOLUME | SOUND_MASK_PCM)) == SOUND_MASK_PCM) {
			mask |= SOUND_MASK_VOLUME;
#if 0
			mix_setparentchild(m, SOUND_MIXER_VOLUME, SOUND_MASK_PCM);
			mix_setrealdev(m, SOUND_MIXER_VOLUME, SOUND_MIXER_NONE);
#else
//			logMsg("XXX mix_setparentchild SOUND_MIXER_VOLUME SOUND_MASK_PCM\n");
//			logMsg("XXX mix_setrealdev SOUND_MIXER_VOLUME SOUND_MIXER_NONE\n");
#endif
//			logMsg("Forcing master volume with PCM\n");
		}
	}

	recmask &= (1 << SOUND_MIXER_NRDEVICES) - 1;
	mask &= (1 << SOUND_MIXER_NRDEVICES) - 1;

	pcmDevice->recDevMask = recmask;
	pcmDevice->devMask = mask;

	return 0;
}

int VoodooHDADevice::audioCtlOssMixerSet(PcmDevice *pcmDevice, UInt32 dev, UInt32 left, UInt32 right)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	AudioControl *control;
	UInt32 mask = 0;
	
	LOCK();
	
	//logMsg("VoodooHDADevice[%p]::audioCtlOssMixerSet(%p, %ld, %ld, %ld)\n", this, pcmDevice, dev, left, right);

	// Save new values. 
	pcmDevice->left[dev] = left;
	pcmDevice->right[dev] = right;

	
	// 'ogain' is the special case implemented with EAPD.
	if (dev == SOUND_MIXER_OGAIN) {
		Widget *widget = NULL;
		int i;
		UInt32 orig;
	
		for (i = funcGroup->startNode; i < funcGroup->endNode; i++) {
			widget = widgetGet(funcGroup, i);
			if (!widget || (widget->enable == 0))
				continue;
			if ((widget->type != HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX) ||
			    	(widget->params.eapdBtl == HDAC_INVALID))
				continue;
			break;
		}
		
		if (i >= funcGroup->endNode) {
			UNLOCK();
			return -1;
		}
		orig = widget->params.eapdBtl;
		if (left == 0)
			widget->params.eapdBtl &= ~HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
		else
			widget->params.eapdBtl |= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
		if (orig != widget->params.eapdBtl) {
			UInt32 val = widget->params.eapdBtl;
			if (funcGroup->audio.quirks & HDA_QUIRK_EAPDINV)
				val ^= HDA_CMD_SET_EAPD_BTL_ENABLE_EAPD;
			sendCommand(HDA_CMD_SET_EAPD_BTL_ENABLE(funcGroup->codec->cad, widget->nid, val),
					funcGroup->codec->cad);
		}
		UNLOCK();
		return (left | (left << 8));
	}
	//Slice
	mask = (1 << dev);
/*	if(dev == SOUND_MIXER_MIC)
		mask |= SOUND_MASK_MONITOR;*/
	// Recalculate all controls related to this OSS device.
	for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
		UInt32 mute;
		int lvol, rvol;
		if ((control->enable == 0) || !(control->ossmask & mask))
			continue;
		if (!(((pcmDevice->playChanId >= 0) &&
				(control->widget->bindAssoc == mChannels[pcmDevice->playChanId].assocNum)) ||
		    	((pcmDevice->recChanId >= 0) &&
				(control->widget->bindAssoc == mChannels[pcmDevice->recChanId].assocNum)) ||
		    	(control->widget->bindAssoc == -2)))
			continue;

		lvol = 100;
		rvol = 100;
		for (int j = 0; j < SOUND_MIXER_NRDEVICES; j++) {
			if (control->ossmask & (1 << j)) {
				lvol = lvol * pcmDevice->left[j] / 100;
				rvol = rvol * pcmDevice->right[j] / 100;
			}
		}
		mute = (lvol == 0) ? HDA_AMP_MUTE_LEFT : 0;
		mute |= (rvol == 0) ? HDA_AMP_MUTE_RIGHT : 0;

        // VertexBZ: Separated flags for Volume/PCM and Mic Half Volume fixes
		if ((dev == SOUND_MIXER_VOLUME && mEnableHalfVolumeFix) ||
            (dev == SOUND_MIXER_PCM && mEnableVolumeChangeFix) ||
            (dev == SOUND_MIXER_MIC && mEnableHalfMicVolumeFix)) {
			// cue8chalk: lerp the volume between the midpoint and the end to get the true value
			lvol = ilerp(control->offset >> 1, control->offset, ((lvol * control->step + 50) / 100) / (control->offset != 0 ? (float)control->offset : 1));
			rvol = ilerp(control->offset >> 1, control->offset, ((rvol * control->step + 50) / 100) / (control->offset != 0 ? (float)control->offset : 1));
		} else {
			lvol = (lvol * control->step + 50) / 100;
			rvol = (rvol * control->step + 50) / 100;
		}
        
		audioCtlAmpSet(control, mute, lvol, rvol);
	}

	UNLOCK();

	return (left | (right << 8));
}

int VoodooHDADevice::ilerp(int a, int b, float t) {
	return a + (t * (float)(b - a));
}

UInt32 VoodooHDADevice::audioCtlOssMixerSetRecSrc(PcmDevice *pcmDevice, UInt32 src)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	Channel *channel;
	UInt32 ret = 0xffffffff;

//		logMsg("VoodooHDADevice[%p]::audioCtlOssMixerSetRecSrc(%p, 0x%lx)\n", this, pcmDevice, src);

	LOCK();

	/* Commutate requested recsrc for each ADC. */
	channel = &mChannels[pcmDevice->recChanId];
	for (int i = 0; channel->io[i] != -1; i++) {
		Widget *widget = widgetGet(funcGroup, channel->io[i]);
		if (!widget || (widget->enable == 0))
			continue;
		ret &= audioCtlRecSelComm(pcmDevice, src, channel->io[i], 0);
	}

	UNLOCK();

	return ((ret == 0xffffffff) ? 0 : ret);
}
//never used
int VoodooHDADevice::audioCtlOssMixerGet(PcmDevice *pcmDevice, UInt32 dev, UInt32* left, UInt32* right)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	AudioControl *control;
	int lvol = 100, rvol = 100;
//	bool bFound = false;
//	int controlIndex;
	//Slice
	UInt32 mask = (1 << dev);
 
	
	LOCK();
	
	
	/* Recalculate all controls related to this OSS device. */
	for (int i = 0; (control = audioCtlEach(funcGroup, i)); i++) {
		if ((control->enable == 0) || !(control->ossmask & mask))
			continue;
		if (!(((pcmDevice->playChanId >= 0) &&
			   (control->widget->bindAssoc == mChannels[pcmDevice->playChanId].assocNum)) ||
			  ((pcmDevice->recChanId >= 0) &&
			   (control->widget->bindAssoc == mChannels[pcmDevice->recChanId].assocNum)) ||
			  (control->widget->bindAssoc == -2)))
			continue;
	
		audioCtlAmpGetGain(control);
	
		if(control->step != 0) {
//			bFound = true;
//			controlIndex = i;
			lvol = 100 * control->left / control->step;
			rvol = 100 * control->right / control->step;
			
			pcmDevice->left[dev] = lvol;
			pcmDevice->right[dev] = rvol;
		}
	}
	
	UNLOCK();
	
	if(left != 0) (*left) = lvol;
	if(right != 0) (*right) = rvol;
	
	return (lvol | (rvol << 8));
}

void VoodooHDADevice::mixerSetDefaults(PcmDevice *pcmDevice)
{
	//IOLog("VoodooHDADevice::mixerSetDefaults\n");
	for (int n = 0; n < SOUND_MIXER_NRDEVICES; n++) {
		uint32_t def = mMixerDefaults[n];
		if (def > 100)
			def = 100;
		audioCtlOssMixerSet(pcmDevice, n, def, def);
	}
//Slice - attention!	
//	if (audioCtlOssMixerSetRecSrc(pcmDevice, SOUND_MASK_INPUT) == 0)
		//errorMsg("warning: couldn't set recording source to input\n");
		return;
}

void VoodooHDADevice::mixerResume(PcmDevice *pcmDevice)
{
	for (int n = 0; n < SOUND_MIXER_NRDEVICES; n++)
		audioCtlOssMixerSet(pcmDevice, n, pcmDevice->left[n], pcmDevice->right[n]);
	audioCtlOssMixerSetRecSrc(pcmDevice, SOUND_MASK_INPUT);	// ignore error
}

/*******************************************************************************************/
/*******************************************************************************************/

Channel *VoodooHDADevice::channelInit(PcmDevice *pcmDevice, int direction)
{
	FunctionGroup *funcGroup = pcmDevice->funcGroup;
	Channel *channel;
	int ord = 0, chid;

	chid = (direction == PCMDIR_PLAY) ? pcmDevice->playChanId : pcmDevice->recChanId;
	channel = &mChannels[chid];
	for (int i = 0; i < mNumChannels && i < chid; i++)
		if (channel->direction == mChannels[i].direction)
			ord++;
	if (direction == PCMDIR_PLAY)
		channel->off = (mInStreamsSup + ord) << 5;
	else
		channel->off = ord << 5;

	if (funcGroup->audio.quirks & HDA_QUIRK_FIXEDRATE) {
		channel->caps.minSpeed = channel->caps.maxSpeed = 48000;
		channel->pcmRates[0] = 48000;
		channel->pcmRates[1] = 0;
	}
	if (mDmaPosMem)
		channel->dmaPos = (UInt32 *) (mDmaPosMem->virtAddr + (mStreamCount * 8));
	else
		channel->dmaPos = NULL;
	channel->streamId = ++mStreamCount;
	channel->direction = direction;
	channel->blockSize = pcmDevice->chanSize / pcmDevice->chanNumBlocks;
	channel->numBlocks = pcmDevice->chanNumBlocks;

	if (bdlAlloc(channel) != 0) {
		channel->numBlocks = 0;
		return NULL;
	}

//	logMsg("block size: %ld, block count: %ld, buffer size: %ld\n", channel->blockSize, channel->numBlocks,
//			pcmDevice->chanSize);

	channel->buffer = allocateDmaMemory(pcmDevice->chanSize, "buffer");
	if (!channel->buffer) {
		errorMsg("can't allocate sound buffer!\n");
		return NULL;
	}
	ASSERT(channel->buffer->size == pcmDevice->chanSize);

	ASSERT(channel->blockSize <= (pcmDevice->chanSize / HDA_BDL_MIN));
	ASSERT(channel->blockSize >= HDA_BLK_MIN);
	ASSERT(channel->numBlocks <= HDA_BDL_MAX);
	ASSERT(channel->numBlocks >= HDA_BDL_MIN);

	return channel;
}

int VoodooHDADevice::channelSetFormat(Channel *channel, UInt32 format)
{
	for (int i = 0; channel->caps.formats[i] != 0; i++) {
		if (format == channel->caps.formats[i]) {
			channel->format = format;
			return 0;
		}
	}

	return -1;
}

int VoodooHDADevice::channelSetSpeed(Channel *channel, UInt32 reqSpeed)
{
	UInt32 speed = 0;

	for (int i = 0; channel->pcmRates[i] != 0; i++) {
		UInt32 threshold;
		speed = channel->pcmRates[i];
		threshold = speed + ((channel->pcmRates[i + 1] != 0) ? ((channel->pcmRates[i + 1] - speed) >> 1) : 0);
		if (reqSpeed < threshold)
			break;
	}

	if (speed == 0) /* impossible */
		channel->speed = 48000;
	else
		channel->speed = speed;

	return channel->speed;
}

void VoodooHDADevice::channelStop(Channel *channel, const bool shouldLock)
{
	FunctionGroup *funcGroup = channel->funcGroup;
	nid_t cad = funcGroup->codec->cad;

	if (shouldLock)
		LOCK();

	streamStop(channel);

	for (int i = 0; channel->io[i] != -1; i++) {
		Widget *widget = widgetGet(channel->funcGroup, channel->io[i]);
		if (!widget)
			continue;
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap))
			sendCommand(HDA_CMD_SET_DIGITAL_CONV_FMT1(cad, channel->io[i], 0), cad);
		sendCommand(HDA_CMD_SET_CONV_STREAM_CHAN(cad, channel->io[i], 0), cad);
	}

	if (shouldLock)
		UNLOCK();
}

void VoodooHDADevice::channelStart(Channel *channel, const bool shouldLock)
{
	if (shouldLock)
		LOCK();

	streamStop(channel);
	streamReset(channel);
	bdlSetup(channel);
	streamSetId(channel);
	streamSetup(channel);
	streamStart(channel);

	if (shouldLock)
		UNLOCK();
}

int VoodooHDADevice::channelGetPosition(Channel *channel)
{
	UInt32 position;

	LOCK();

	if (channel->dmaPos)
		position = *(channel->dmaPos);
	else
		position = readData32(channel->off + HDAC_SDLPIB);

	UNLOCK();

	/* Round to available space and force 128 bytes aligment. */
	position %= (channel->blockSize * channel->numBlocks - channel->slack);
#if 0
	/* Since mSampleSize may be non-power of 2 */
	position &= HDA_BLK_ALIGN;
#endif

	return position;
}

static
UInt8 calculateStripectl(UInt8 globalSDO, UInt8 stripecap, UInt16 format)
{
#define EXTRACT_CHAN(fmt) (fmt & 15)
#define EXTRACT_BITS(fmt) ((fmt >> 4) & 7)
#define EXTRACT_MULT(fmt) ((fmt >> 11) & 7)
	switch (globalSDO) {
		case 2:
			/* Controller supports 4-stripe */
			if (stripecap & 4) {
				/* Codec audio output widget supports 4-stripe */
				/* Need at least 32-bit per sample frame for 4-stripe */
				if (EXTRACT_BITS(format) == 4)
					return 2;	/* Have 32-bit samples, allow 4-stripe */
				if (EXTRACT_BITS(format)) {
					/* Using 16 to 24-bit samples, check if CHAN > 1 or MULT > 1 */
					if (EXTRACT_CHAN(format))
						return 2;	/* CHAN > 1 */
					if (EXTRACT_MULT(format))
						return 2;	/* MULT > 1 */
				}
				/* Using 8-bit samples, check if CHAN * MULT >= 4 */
				if ((EXTRACT_CHAN(format) + 1) * (EXTRACT_MULT(format) + 1) >= 4)
					return 2;
				/* else can't do 4-stripe */
			} /* else can't do 4-stripe */
			/* fall through and try 2-stripe */
		case 1:
			/* Controller supports 2-stripe */
			if (stripecap & 2) {
				/* Codec audio output widget supports 2-stripe */
				/* Need at least 16-bit per sample frame for 2-stripe */
				if (EXTRACT_BITS(format))
					return 1;	/* Have at least 16-bit samples, allow 2-stripe */
				/* Using 8-bit samples, check if CHAN > 1 or MULT > 1 */
				if (EXTRACT_CHAN(format))
					return 1;	/* CHAN > 1 */
				if (EXTRACT_MULT(format))
					return 1;	/* MULT > 1 */
				/* else can't do 2-stripe */
			} /* else can't do 2-stripe */
			break; /* revert to no-stripe */
	} /* default: controller does not support striping */
	return 0;
#undef EXTRACT_MULT
#undef EXTRACT_BITS
#undef EXTRACT_CHAN
}

/*******************************************************************************************/
/*******************************************************************************************/

void VoodooHDADevice::streamSetup(Channel *channel)
{
	AudioAssoc *assoc = &channel->funcGroup->audio.assocs[channel->assocNum];
	int totalchn;
  int totalextchn;
	nid_t cad = channel->funcGroup->codec->cad;
	UInt16 format, digFormat;
	const static
	UInt16 chmap[2][5] = {{ 0x0010, 0x0001, 0x0201, 0x0321, 0x0321 }, /* 5.1 */
                        { 0x0010, 0x0001, 0x2201, 0x3321, 0x4321 }};/* 7.1 */
	int map = -1;
	
	totalchn = AFMT_CHANNEL(channel->format);
  totalextchn = AFMT_EXTCHANNEL(channel->format);
	if (!totalchn) {
		if (channel->format & (AFMT_STEREO | AFMT_AC3)) { //Slice - AC3 supports more then Stereo, but here we force 2
			totalchn = 2;
		} else
			totalchn = 1;
	}

	format = 0;
	if (channel->format & AFMT_S16_LE)
		format |= channel->bit16 << 4;
	else if (channel->format & AFMT_S32_LE)
		format |= channel->bit32 << 4;
	else
		format |= 1 << 4;

	for (int i = 0; gRateTable[i].rate; i++) {
		if (gRateTable[i].valid && (channel->speed == gRateTable[i].rate)) {
			format |= gRateTable[i].base;
			format |= gRateTable[i].mul;
			format |= gRateTable[i].div;
			break;
		}
	}

	format |= (totalchn - 1);
	//Slice - from BSD
	/* Set channel mapping for known speaker setups. */
	if (assoc->pinset == 0x0007 || assoc->pinset == 0x0013) // Standard 5.1 
		map = 0;
	 else if (assoc->pinset == 0x0017) // Standard 7.1 
		map = 1;
	
	digFormat = HDA_CMD_SET_DIGITAL_CONV_FMT1_DIGEN | HDA_CMD_SET_DIGITAL_CONV_FMT1_COPY;
	if (channel->format & AFMT_AC3)
		digFormat |= HDA_CMD_SET_DIGITAL_CONV_FMT1_NAUDIO;
	
	writeData16(channel->off + HDAC_SDFMT, format);
    
	channel->stripectl = calculateStripectl(static_cast<UInt8>(mSDO), channel->stripecap, format);

	for (int i = 0, chn = 0; channel->io[i] != -1; i++) {
		Widget *widget;
		int c, cchn;

		widget = widgetGet(channel->funcGroup, channel->io[i]);
		if (!widget)
			continue;

//		if ((assoc->hpredir >= 0) && (i == assoc->pincnt))
//			chn = 0;
		/* If HP redirection is enabled, but failed to use same DAC make last DAC one to duplicate first one. */
		if (assoc->fakeredir && i == (assoc->pincnt - 1)) {
			c = (channel->streamId << 4);
			chn = 0;
		} else {
			if (map >= 0) /* Map known speaker setups. */
				chn = (((chmap[map][totalchn / 2] >> i * 4) & 0xf) - 1) * 2;
			if (chn < 0 || chn >= totalchn) {
				/* This is until OSS will support multichannel. Should be: c = 0; to disable unused DAC */
				c = 0;
			} else {
				c = (channel->streamId << 4) | chn;
			}			
		}		
		if(mVerbose >= 2)
			logMsg("PCMDIR_%s: Stream setup nid=%d format=%08lx speed=%ld , dfmt=0x%04x, chan=0x%04x, stripe=%d\n",
				   (channel->direction == PCMDIR_PLAY) ?"PLAY" : "REC", channel->io[i], 
				   (long unsigned int)channel->format, (long int)channel->speed, digFormat, c, channel->stripectl);
		
		
//		logMsg("PCMDIR_%s: Stream setup nid=%d: format=0x%04x, digFormat=0x%04x\n",
//				(channel->direction == PCMDIR_PLAY) ? "PLAY" : "REC", channel->io[i], format, digFormat);
		sendCommand(HDA_CMD_SET_CONV_FMT(cad, channel->io[i], format), cad);
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap))
			sendCommand(HDA_CMD_SET_DIGITAL_CONV_FMT1(cad, channel->io[i], digFormat), cad);
		sendCommand(HDA_CMD_SET_CONV_STREAM_CHAN(cad, channel->io[i], c), cad);
		if (!c)
			continue;
		if (HDA_PARAM_AUDIO_WIDGET_CAP_STRIPE(widget->params.widgetCap))
			sendCommand(HDA_CMD_SET_STRIPE_CONTROL(cad, channel->io[i], channel->stripectl), cad);
		cchn = HDA_PARAM_AUDIO_WIDGET_CAP_CC(widget->params.widgetCap);
		if (cchn > 1 && chn < totalchn) {
			cchn = min(cchn, totalchn - chn - 1);
			sendCommand(HDA_CMD_SET_CONV_CHAN_COUNT(cad, channel->io[i], cchn), cad);
		}
		if (HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL(widget->params.widgetCap))
			streamHDMIorDPExtraSetup(channel, channel->io[i], assoc, totalchn, totalextchn);

		chn += cchn + 1;
	}
}

void VoodooHDADevice::streamHDMIorDPExtraSetup(Channel *channel, nid_t dac, AudioAssoc *assoc, int totalchn, int totalext)
{
  FunctionGroup* funcGroup = channel->funcGroup;
	UInt8 csum;
	UInt16 AudioInfopacketBufferSize = 0xFFFFU;
	nid_t cad = funcGroup->codec->cad;
	nid_t nid_pin;
	Widget *widget_pin;
	/*
	 * EIA-CEA 861-D speaker mappings (section 6.6.2)
	 * ASP Channels 8-7-6-5-4-3-2-1
	 */
//	const static
//	UInt8  hdmica[8] = {
//		0x00, /*  -   -   -  -  -  -  FR FL */
//		0x00, /*  -   -   -  -  -  -  FR FL */
//		0x04, /*  -   -   - RC  -  -  FR FL */
//		0x08, /*  -   -  RR RL  -  -  FR FL */
//		0x0A, /*  -   -  RR RL FC  -  FR FL */
//		0x0B, /*  -   -  RR RL FC LFE FR FL */
//		0x12, /* RRC RLC RR RL FC  -  FR FL */
//		0x13  /* RRC RLC RR RL FC LFE FR FL */
//	};
//	const static
//	UInt32 hdmich[8] = { 0xFFFFFF00, 0xFFFFFF10, 0xFFF2FF10, 0xFF32FF10, 0xFF432F10, 0xFF542310, 0x65432F10, 0x76542310 };
  
  /* Mapping formats to HDMI channel allocations. */
  const static UInt8 hdmica[2][8] =
  /*  1     2     3     4     5     6     7     8  */
  {{ 0x02, 0x00, 0x04, 0x08, 0x0a, 0x0e, 0x12, 0x12 }, /* x.0 */
   { 0x01, 0x03, 0x01, 0x03, 0x09, 0x0b, 0x0f, 0x13 }}; /* x.1 */
  /* Mapping formats to HDMI channels order. */
  const static UInt32 hdmich[2][8] =
  /*  1  /  5     2  /  6     3  /  7     4  /  8  */
  {{ 0xFFFF0F00, 0xFFFFFF10, 0xFFF2FF10, 0xFF32FF10,
     0xFF324F10, 0xF5324F10, 0x54326F10, 0x54326F10 }, /* x.0 */
   { 0xFFFFF000, 0xFFFF0100, 0xFFFFF210, 0xFFFF2310,
     0xFF32F410, 0xFF324510, 0xF6324510, 0x76325410 }}; /* x.1 */


	for (int j = 0; j < 16; j++) {
		if (assoc->dacs[j] != dac)
			continue;
		nid_pin = assoc->pins[j];
		widget_pin = widgetGet(funcGroup, nid_pin);
		if (!widget_pin)
			continue;
		if (!HDA_PARAM_PIN_CAP_DP(widget_pin->pin.cap) &&
			!HDA_PARAM_PIN_CAP_HDMI(widget_pin->pin.cap))
			continue;

		/*
		 * The default mapping is 0x00, 0x11, 0x32, 0x23, 0x44, 0x55, 0x66, 0x77
		 *   PC channel layout is
		 *   ch 0 - FL, ch 1 - FR, ch 2 - FC, ch 3 - LFE, ch 4 - RL, ch 5 - RR, ch 6 - SL,  ch 7 - SR
		 *   HDMI ASP channel layout is
		 *   ch 1 - FL, ch 2 - FR, ch 3 - LFE, ch 4 - FC, ch 5 - RL, ch 6 - RR, ch 7 - RLC, ch 8 - RRC
		 */

		/* Set channel mapping. */
		for (int k = 0; k < 8; k++)
			sendCommand(HDA_CMD_SET_HDMI_CHAN_SLOT(cad, nid_pin,
												   (((hdmich[totalext == 0 ? 0 : 1][totalchn - 1]>> (k * 4)) & 0xf) << 4) | k), cad);

		/*
		 * Note on HBR (High Bit Rate)
		 *   When sending encoded audio with bit rate > 6.144 mbps, HDMI requires using HBR.
		 *   This corresponds to 96 or 192 KHz frame rate, 16 bits/sample, 8 channels in HDA.
		 *   At present, VoodooHDA supportes AFMT_AC3 with
		 *     frame rates 32 - 48 KHz, 16 bits/sample, 2 channels.  So HBR is not needed.
		 */
    /*
     * Enable High Bit Rate (HBR) Encoded Packet Type
     * (EPT), if supported and needed (8ch data).
     */
    if (HDA_PARAM_PIN_CAP_HDMI(widget_pin->pin.cap) &&
        HDA_PARAM_PIN_CAP_HBR(widget_pin->pin.cap)) {
      widget_pin->pin.ctrl &= ~HDA_CMD_SET_PIN_WIDGET_CTRL_VREF_ENABLE_MASK;
      if ((channel->format & AFMT_AC3) && (totalchn == 8))
        widget_pin->pin.ctrl |= 0x03;
      sendCommand(HDA_CMD_SET_PIN_WIDGET_CTRL(cad, nid_pin, widget_pin->pin.ctrl), cad);
    }


		/*
		 * Find size of Audio Infoframe buffer to see if it can be used.
		 * On some HDMI codecs, if HDMI is not connected by graphics card driver,
		 *   the Infoframe buffer has zero length.
		 */
		if (AudioInfopacketBufferSize == 0xFFFFU) {
			/* do this once */
			AudioInfopacketBufferSize = static_cast<UInt16>(sendCommand(HDA_CMD_GET_HDMI_DIP_SIZE(cad, nid_pin, 0x00), cad)) + 1U;
		}

		if (AudioInfopacketBufferSize < 10U)
			continue;

		/*
		 * Send Audio Infoframe
		 */

		/* Stop audio infoframe transmission. */
		sendCommand(HDA_CMD_SET_HDMI_DIP_INDEX(cad, nid_pin, 0x00), cad);
		sendCommand(HDA_CMD_SET_HDMI_DIP_XMIT(cad, nid_pin, 0x00), cad);

		/* Clear audio infoframe buffer. */
		sendCommand(HDA_CMD_SET_HDMI_DIP_INDEX(cad, nid_pin, 0x00), cad);
		for (int k = 0; k < static_cast<int>(AudioInfopacketBufferSize); k++)
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x00), cad);
		
		/* Write HDMI/DisplayPort audio infoframe. */
		sendCommand(HDA_CMD_SET_HDMI_DIP_INDEX(cad, nid_pin, 0x00), cad);
		/*
		 * Need Valid ELD to tell between DP or HDMI
		 */
    if (mVerbose > 1) {
      logMsg("EldLen=%d\n", widget_pin->eld_len);
    }
#if DP_AUDIO
		if (widget_pin->eld != NULL && widget_pin->eld_len >= 6 && ((widget_pin->eld[5] >> 2) & 0x3) == 1) { /* DisplayPort */
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x84), cad);
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x1b), cad);
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x44), cad);
      if (mVerbose > 0) {
        logMsg("DP Audio\n");
      }

		} else {
#endif
      /* HDMI */
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x84), cad);
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x01), cad);
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x0a), cad);
      if (mVerbose > 0) {
        logMsg("HDMI Audio\n");
      }

			csum = 0;
			csum -= 0x84 + 0x01 + 0x0a + (totalchn - 1) + hdmica[totalext == 0 ? 0 : 1][totalchn - 1];
			sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, csum), cad);
#if DP_AUDIO
		}
#endif
		sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, totalchn - 1), cad);
		sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x00), cad);
		sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, 0x00), cad);
		sendCommand(HDA_CMD_SET_HDMI_DIP_DATA(cad, nid_pin, hdmica[totalext == 0 ? 0 : 1][totalchn - 1]), cad);

		/* Start audio infoframe transmission. */
		sendCommand(HDA_CMD_SET_HDMI_DIP_INDEX(cad, nid_pin, 0x00), cad);
		sendCommand(HDA_CMD_SET_HDMI_DIP_XMIT(cad, nid_pin, 0xc0), cad);
	}
}

void VoodooHDADevice::streamStop(Channel *channel)
{
	UInt32 ctl;

	ctl = readData8(channel->off + HDAC_SDCTL0);
	ctl &= ~(HDAC_SDCTL_IOCE | HDAC_SDCTL_FEIE | HDAC_SDCTL_DEIE | HDAC_SDCTL_RUN);
	writeData8(channel->off + HDAC_SDCTL0, ctl);

	channel->flags &= ~HDAC_CHN_RUNNING;

	ctl = readData32(HDAC_INTCTL);
	ctl &= ~(1 << (channel->off >> 5));
	writeData32(HDAC_INTCTL, ctl);
}

void VoodooHDADevice::streamStart(Channel *channel)
{
	UInt32 ctl;

	channel->flags |= HDAC_CHN_RUNNING;

	ctl = readData32(HDAC_INTCTL);
	ctl |= 1 << (channel->off >> 5);
	writeData32(HDAC_INTCTL, ctl);
  
  //FreeBSD update
//  HDAC_WRITE_1(&sc->mem, off + HDAC_SDSTS, HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS);
  writeData8(channel->off + HDAC_SDSTS, HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS);
  //

	if (channel->stripectl) {
		ctl = readData8(channel->off + HDAC_SDCTL2);
		ctl &= ~HDAC_SDCTL2_STRIPE_MASK;
		ctl |= channel->stripectl << HDAC_SDCTL2_STRIPE_SHIFT;
		writeData8(channel->off + HDAC_SDCTL2, ctl);
	}

	ctl = readData8(channel->off + HDAC_SDCTL0);
	ctl |= HDAC_SDCTL_IOCE | HDAC_SDCTL_FEIE | HDAC_SDCTL_DEIE | HDAC_SDCTL_RUN;
	writeData8(channel->off + HDAC_SDCTL0, ctl);
}

void VoodooHDADevice::streamReset(Channel *channel)
{
	int timeout = 1000;
	int to = timeout;
	UInt32 ctl;

	ctl = readData8(channel->off + HDAC_SDCTL0);
	ctl |= HDAC_SDCTL_SRST;
	writeData8(channel->off + HDAC_SDCTL0, ctl);
	do {
		ctl = readData8(channel->off + HDAC_SDCTL0);
		if (ctl & HDAC_SDCTL_SRST)
			break;
		IODelay(10);
	} while (--to);
	if (!(ctl & HDAC_SDCTL_SRST))
		errorMsg("timeout in reset\n");
	ctl &= ~HDAC_SDCTL_SRST;
	writeData8(channel->off + HDAC_SDCTL0, ctl);
	to = timeout;
	do {
		ctl = readData8(channel->off + HDAC_SDCTL0);
		if (!(ctl & HDAC_SDCTL_SRST))
			break;
		IODelay(10);
	} while (--to);
	if (ctl & HDAC_SDCTL_SRST)
		errorMsg("can't reset!\n");
}

void VoodooHDADevice::streamSetId(Channel *channel)
{
	UInt32 ctl;

	ctl = readData8(channel->off + HDAC_SDCTL2);
	ctl &= ~(HDAC_SDCTL2_STRM_MASK | HDAC_SDCTL2_STRIPE_MASK);
	ctl |= channel->streamId << HDAC_SDCTL2_STRM_SHIFT;
	writeData8(channel->off + HDAC_SDCTL2, ctl);
}

/*******************************************************************************************/
/*******************************************************************************************/

void VoodooHDADevice::bdlSetup(Channel *channel)
{
	BdlEntry *bdlEntry;
	UInt64 addr;
	UInt32 blockSize, numBlocks;

	addr = (UInt64) channel->buffer->physAddr;
	bdlEntry = (BdlEntry *) channel->bdlMem->virtAddr;

	blockSize = channel->blockSize;
	numBlocks = channel->numBlocks;

	for (UInt32 n = 1; n <= numBlocks; n++, bdlEntry++) {
		bdlEntry->addrl = (UInt32) addr;
		bdlEntry->addrh = (UInt32) (addr >> 32);
		bdlEntry->len = ((n == numBlocks) ? (blockSize - channel->slack) : blockSize);
		bdlEntry->ioc = (n == numBlocks);
		addr += bdlEntry->len;
	}

	writeData32(channel->off + HDAC_SDCBL, blockSize * numBlocks - channel->slack);
	writeData16(channel->off + HDAC_SDLVI, numBlocks - 1);
	addr = channel->bdlMem->physAddr;
	writeData32(channel->off + HDAC_SDBDPL, (UInt32) addr);
	writeData32(channel->off + HDAC_SDBDPU, (UInt32) (addr >> 32));
	if (channel->dmaPos && !(readData32(HDAC_DPIBLBASE) & 0x00000001)) {
		addr = mDmaPosMem->physAddr;
		writeData32(HDAC_DPIBLBASE, ((UInt32) addr & HDAC_DPLBASE_DPLBASE_MASK) | 0x00000001);
		writeData32(HDAC_DPIBUBASE, (UInt32) (addr >> 32));
	}
}

int VoodooHDADevice::bdlAlloc(Channel *channel)
{
	PcmDevice *pcmDevice = channel->pcmDevice;

	ASSERT(pcmDevice);
	ASSERT(pcmDevice->chanNumBlocks);

	channel->bdlMem = allocateDmaMemory(sizeof (BdlEntry) * pcmDevice->chanNumBlocks, "bdlMem", kIOMapWriteThruCache);
	if (!channel->bdlMem) {
		errorMsg("error: couldn't allocate bdl\n");
		return -1;
	}

	return 0;
}

/*******************************************************************************************/
/*******************************************************************************************/

int VoodooHDADevice::pcmAttach(PcmDevice *pcmDevice)
{
	ASSERT(pcmDevice);

	char buf[256];
	snprintf(buf, sizeof (buf), "HDA %s PCM #%d %s at cad %d nid %d",
			findCodecName(pcmDevice->funcGroup->codec), pcmDevice->index,
	//			 pcmDevice->digital ?"Digital" : "Analog",
			 (pcmDevice->digital == 3)?"DisplayPort":
			 ((pcmDevice->digital == 2)?"HDMI":
			  ((pcmDevice->digital)?"Digital":"Analog")),
			 pcmDevice->funcGroup->codec->cad, pcmDevice->funcGroup->nid);
	dumpMsg("pcmAttach: %s\n", buf);

	pcmDevice->chanSize = HDA_BUFSZ_DEFAULT;
	pcmDevice->chanNumBlocks = HDA_BDL_DEFAULT;

	dumpMsg("+--------------------------------------+\n");
	dumpMsg("| DUMPING PCM Playback/Record Channels |\n");
	dumpMsg("+--------------------------------------+\n");
	dumpPcmChannels(pcmDevice);
	dumpMsg("\n");
	dumpMsg("+-------------------------------+\n");
	dumpMsg("| DUMPING Playback/Record Paths |\n");
	dumpMsg("+-------------------------------+\n");
	dumpDac(pcmDevice);
	dumpAdc(pcmDevice);
	dumpMix(pcmDevice);
	dumpMsg("\n");
	dumpMsg("+-------------------------+\n");
	dumpMsg("| DUMPING Volume Controls |\n");
	dumpMsg("+-------------------------+\n");
	dumpCtls(pcmDevice, "Master Volume", SOUND_MASK_VOLUME);
	dumpCtls(pcmDevice, "PCM Volume", SOUND_MASK_PCM);
	dumpCtls(pcmDevice, "CD Volume", SOUND_MASK_CD);
	dumpCtls(pcmDevice, "Microphone Volume", SOUND_MASK_MIC);
	dumpCtls(pcmDevice, "Microphone2 Volume", SOUND_MASK_MONITOR);
	dumpCtls(pcmDevice, "Line-in Volume", SOUND_MASK_LINE);
	dumpCtls(pcmDevice, "Speaker/Beep Volume", SOUND_MASK_SPEAKER);
	dumpCtls(pcmDevice, "Recording Level", SOUND_MASK_RECLEV);
	dumpCtls(pcmDevice, "Input Mix Level", SOUND_MASK_IMIX);
	dumpCtls(pcmDevice, "Input Monitoring Level", SOUND_MASK_IGAIN);
	dumpCtls(pcmDevice, NULL, 0);
	dumpMsg("\n");

	dumpMsg("OSS mixer initialization...\n");
	
	if (audioCtlOssMixerInit(pcmDevice) != 0) {
		errorMsg("warning: mixer initialization failed\n");
	}
	
	//logMsg("VoodooHDADevice::mixerSetDefaults begin\n");
	UNLOCK(); // xxx
	//logMsg("VoodooHDADevice::mixerSetDefaults mid\n");
	mixerSetDefaults(pcmDevice);
	LOCK(); // xxx
	//logMsg("VoodooHDADevice::mixerSetDefaults end\n");

	dumpMsg("Registering PCM channels...\n");
	if (pcmDevice->playChanId >= 0)
		channelInit(pcmDevice, PCMDIR_PLAY); // mChannels[pcmDevice->playChanId]
	if (pcmDevice->recChanId >= 0)
		channelInit(pcmDevice, PCMDIR_REC); // mChannels[pcmDevice->recChanId]
	pcmDevice->registered = true;

	return 0;
}

//Создаем разделяемую область памяти, откуда будет брать информацию PrefPanel
void VoodooHDADevice::createPrefPanelMemoryBuf(FunctionGroup *funcGroup)
{

	//logMsg("VoodooHDADevice::createPrefPanelMemoryBuf\n");

	createPrefPanelStruct(funcGroup);

	//logMsg("VoodooHDADevice::createPrefPanelMemoryBuf mPrefPanelMemoryBuf = 0x%08X\n", mPrefPanelMemoryBuf);

	if(mPrefPanelMemoryBuf == 0) {
		//logMsg("VoodooHDADevice::createPrefPanelMemoryBuf allocate memory\n");
		//mPrefPanelMemoryBufSize = nSliderTabsCount*sizeof(sliders);
		mPrefPanelMemoryBufSize = SOUND_MIXER_NRDEVICES*sizeof(ChannelInfo);
		mPrefPanelMemoryBuf = (ChannelInfo*)allocMem(mPrefPanelMemoryBufSize);
		bzero(mPrefPanelMemoryBuf, mPrefPanelMemoryBufSize); 

		mPrefPanelMemoryBufLock = IOLockAlloc();
		mPrefPanelMemoryBufEnabled = false;
	}

	for(int i = 0; i < nSliderTabsCount; i++) {
		strlcpy(mPrefPanelMemoryBuf[i].name, sliderTabs[i].name, MAX_SLIDER_TAB_NAME_LENGTH);
		mPrefPanelMemoryBuf[i].numChannels = nSliderTabsCount;
		for(int j = 1; j < 25; j++) {
			if(sliderTabs[i].volSliders[j].enabled == 0) 
				continue;

			mPrefPanelMemoryBuf[i].mixerValues[j - 1].mixId = j;
			strlcpy(mPrefPanelMemoryBuf[i].mixerValues[j - 1].name, sliderTabs[i].volSliders[j].name, 32);
			mPrefPanelMemoryBuf[i].mixerValues[j - 1].enabled = 1;
			mPrefPanelMemoryBuf[i].mixerValues[j - 1].value = mMixerDefaults[j];
		}
		mPrefPanelMemoryBuf[i].mixerValues[24].mixId = 0;
		mPrefPanelMemoryBuf[i].mixerValues[24].enabled = true;
		mPrefPanelMemoryBuf[i].mixerValues[24].value = mMixerDefaults[0];
		mPrefPanelMemoryBuf[i].vectorize = vectorize;
		mPrefPanelMemoryBuf[i].noiseLevel = noiseLevel;
		mPrefPanelMemoryBuf[i].useStereo = useStereo;
		mPrefPanelMemoryBuf[i].StereoBase = StereoBase;
	}
}

//AutumnRain
//Создаем структуру в которой запомним какие объекты AudioControl каким регуляторам на панели PrefPanel соотвествуют
void VoodooHDADevice::createPrefPanelStruct(FunctionGroup *funcGroup)
{
	//logMsg("createPrefPanelStruct: codec %d have %d assocNum\n", funcGroup->codec->cad, funcGroup->audio.numAssocs);
	
	//Перебираем все ассоциации которые были созданы ранее
	for(int i = 0; i < funcGroup->audio.numAssocs; i++) {
		//Получаем ноду которая является главной в ассоциации - это, как правило, устройство к которому или от которого приходит сигнал
		nid_t mainNid = funcGroup->audio.assocs[i].pins[0];
		Widget *mainWidget = widgetGet(funcGroup, mainNid);
		if(mainWidget) {
			//logMsg("createPrefPanelStruct:    Assoc %d have main nid 0x%X %s\n", i, mainNid, mainWidget->name);
			//logMsg("createPrefPanelStruct:    ctrl = %d  ossmask = 0x%08X\n", mainWidget->pin.ctrl, mainWidget->ossmask); 
			 //В соответствии с названием устройства называем вкладку
			//catPinName(mainWidget); //->pin.config, sliderTabs[nSliderTabsCount].name, MAX_SLIDER_TAB_NAME_LENGTH);
			//sliderTabs[nSliderTabsCount].name = (char *)&mainWidget->name[5];
			for(int l = 0; l < MAX_SLIDER_TAB_NAME_LENGTH; l++)
				sliderTabs[nSliderTabsCount].name[l] = mainWidget->name[l+5];
		}
		AudioControl *control;
		UInt32 ossmask = 0;
		PcmDevice* pcmDevice = 0;
		PcmDevice* curPCMDevice = 0;
		char buf[255];
		//Теперь ищем OSS устройства которые влияют на сигнал проходящий по всем нодам текущей ассоциации
		for(int j = 0; (control = audioCtlEach(funcGroup, j)); j++) {
			if((control->enable == 0) || (control->widget->enable == 0))
				continue;
			
			if(control->widget->bindAssoc == i) {
				ossmask |= control->ossmask;
				//Slice - dangerous mode
				if(control->ossmask & SOUND_MASK_MONITOR)
					ossmask |= SOUND_MASK_MIC;
				
				//logMsg("createPrefPanelStruct:        audioControl %d ossmask = 0x%08lx\n", j, (long unsigned int)ossmask);
				
			}
			//Ищем PCM устройство к которому принадлежит OSS устройство
			for(int pcmDeviceIndex = 0; pcmDeviceIndex < funcGroup->audio.numPcmDevices; pcmDeviceIndex++) {
				curPCMDevice = &funcGroup->audio.pcmDevices[pcmDeviceIndex];
				if(curPCMDevice->playChanId >= 0 && mChannels[curPCMDevice->playChanId].assocNum == i)
					pcmDevice = curPCMDevice;
				if(curPCMDevice->recChanId >= 0 && mChannels[curPCMDevice->recChanId].assocNum == i)
					pcmDevice = curPCMDevice;
			}
		}
		//logMsg("createPrefPanelStruct:         ossdev %s, pcmDev = %d\n", audioCtlMixerMaskToString(ossmask, buf, sizeof(buf)), pcmDeviceNum);
		
		if(ossmask & SOUND_MASK_PCM)
			ossmask |= SOUND_MASK_IMIX;
		
		sliderTabs[nSliderTabsCount].pcmDevice = pcmDevice;
		//Создаем регуляторы на текущей вкладке
		for(int j = 0; j < 32; j++) {
			if(ossmask & (1 << j)) {
				//logMsg("%d (%s), ", j, audioCtlMixerMaskToString(1 << j, buf, sizeof(buf)));
				sliderTabs[nSliderTabsCount].volSliders[j].enabled = true;
				strlcpy(sliderTabs[nSliderTabsCount].volSliders[j].name, audioCtlMixerMaskToString(1 << j, buf, sizeof(buf)), 32);
				sliderTabs[nSliderTabsCount].volSliders[j].ossdev = j;
			}
		}
		//logMsg("\n");
		nSliderTabsCount++;
	}
}

//Считываем текущее настройки усиления и сохраняем их в разделяемой памяти
void VoodooHDADevice::updatePrefPanelMemoryBuf(void)
{

	//logMsg("VoodooHDADevice::updatePrefPanelMemoryBuf\n");

	for(int i = 0; i < nSliderTabsCount; i++) {

		if(sliderTabs[i].pcmDevice == 0) continue;

		for(int j = 1; j < 25; j++) {
			if(sliderTabs[i].volSliders[j].enabled == 0)
				continue;

			mPrefPanelMemoryBuf[i].mixerValues[j - 1].value = sliderTabs[i].pcmDevice->left[j];
		}
		mPrefPanelMemoryBuf[i].mixerValues[24].value = sliderTabs[i].pcmDevice->left[0];// mMixerDefaults[0];
	}
}

//Функция меняет значения усиления для регулятора
void VoodooHDADevice::changeSliderValue(UInt8 tabNum, UInt8 sliderNum, UInt8 newValue)
{
	//logMsg("change for Device (%d) ossSlider (%d) value to %d\n", tabNum, sliderNum, newValue);
	if(tabNum < nSliderTabsCount) {
		PcmDevice* _pcmDevice = sliderTabs[tabNum].pcmDevice;
		if(_pcmDevice)
			audioCtlOssMixerSet(_pcmDevice, sliderNum, newValue, newValue);
	}
}

//Slice
void VoodooHDADevice::setMath(UInt8 tabNum, UInt8 sliderNum, UInt8 newValue)
{
	VoodooHDAEngine *engine;
	engine = lookupEngine(tabNum);
	if (!engine) return;
	UInt8 n, b;
	bool v = ((sliderNum & 1) == 1);
	bool s = ((sliderNum & 2) == 2);
	n = newValue & 0x0f;
	b = (newValue & 0xf0) >> 4;
	engine->mChannel->vectorize = v;
	engine->mChannel->useStereo = s;
	engine->mChannel->noiseLevel = n;
	engine->mChannel->StereoBase = b;
	
}

void VoodooHDADevice::freePrefPanelMemoryBuf(void)
{
	IOLockLock(mPrefPanelMemoryBufLock);
	IOLockFree(mPrefPanelMemoryBufLock);
	mPrefPanelMemoryBufEnabled = false;
	
	freeMem(mPrefPanelMemoryBuf);
	mPrefPanelMemoryBuf = 0;
}

void VoodooHDADevice::lockPrefPanelMemoryBuf()
{
	ASSERT(mPrefPanelMemoryBufLock);
	IOLockLock(mPrefPanelMemoryBufLock);
}

void VoodooHDADevice::unlockPrefPanelMemoryBuf()
{
	ASSERT(mPrefPanelMemoryBufLock);
	IOLockUnlock(mPrefPanelMemoryBufLock);
}

void VoodooHDADevice::lockExtMsgBuffer()
{
	ASSERT(mExtMessageLock);
	IOLockLock(mExtMessageLock);
}

void VoodooHDADevice::unlockExtMsgBuffer()
{
	ASSERT(mExtMessageLock);
	IOLockUnlock(mExtMessageLock);
}

void VoodooHDADevice::dumpExtMsg(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	bool lockExists;
	int length;
	
	lockExists = (!isInactive() && mExtMessageLock);
	if (lockExists)
		lockExtMsgBuffer(); // utilize message buffer lock for console logging as well
	
	//ASSERT(mExtMsgBufferPos < (mExtMsgBufferSize - 1));
	if (mExtMsgBufferPos != (mExtMsgBufferSize - 2)) {
		length = vsnprintf(mExtMsgBuffer + mExtMsgBufferPos, mExtMsgBufferSize - mExtMsgBufferPos,
						   format, args);
		if (length > 0)
			mExtMsgBufferPos += length;
		else if (length < 0)
			IOLog("warning: vsnprintf in dumpMsg failed\n");
	}
	
	if (lockExists)
		unlockExtMsgBuffer();
	
	va_end(args);
}
