#include "License.h"

#ifndef _VOODOO_HDA_DEVICE_H
#define _VOODOO_HDA_DEVICE_H

#include <IOKit/IOLib.h>
#include <IOKit/IOCommandGate.h>

#include <IOKit/audio/IOAudioDevice.h>

#include "Private.h"
#include "Shared.h"

#define MAX_TREE_LENGHT 15

enum {
	kVoodooHDAMessageTypeGeneral = 0x2000,
	kVoodooHDAMessageTypeError,
	kVoodooHDAMessageTypeDump
};
//Slice
#define MAX_NODES 100
typedef struct {
	UInt32 Enable;
	UInt32 cad; //Codec number
	UInt32 Node;
	UInt32 Config;
	UInt32 Type;
	UInt32 nConns; //Число соединений 
	UInt32 Conns[HDA_MAX_CONNS];
	UInt32 Cap;
	UInt32 Control;
	UInt32 bEnabledWidget;
	UInt32 nSel;
	UInt32 favoritDAC;
} PatchArray;
//
//Эта глобальная переменная отвечает за замену левого канала на правый для входных данных

#define MAX_AUDIO_CTLS 16
typedef struct _volSlider{
	char name[32]; //Название регулятора
	bool enabled; //активен ли регулятор
	UInt32 ossdev; //к какому ossdev пренадлежит этот регулятор (но влияет он только на AudioControl перечисленные в audioCtls;
	UInt8 pcmDev; //к какому устройству PCM принадлежит регулятор
	AudioControl *audioCtls[MAX_AUDIO_CTLS]; //Здесь перечислены какие AudioControl'ы затрагивает этот регулятор
	UInt8 nAudioCtlsCount; //Здесь храниться число AudioControl в массиве audioCtls
	
	_volSlider() { //Конструктор по умолчанию
		bzero(name, sizeof(name));
		enabled = false;
		nAudioCtlsCount = 0;
		pcmDev = 0;
		bzero(&audioCtls[0], sizeof audioCtls);
	}
}volSlider;

typedef struct _sliderTab{
	char name[MAX_SLIDER_TAB_NAME_LENGTH]; //Имя вкладки с регуляторами
	PcmDevice *pcmDevice; //Указатель на устройство PCM к которому принадлежат OSS Dev регуляторов на данной вкладке
	volSlider volSliders[25]; //Регуляторы на вкладке
	ChannelInfo *sliderChan;
	
	_sliderTab() {
		pcmDevice = 0;
		sliderChan = NULL;
		bzero(name, sizeof(name));
	}
}sliderTab;

class VoodooHDAEngine;

class IOPCIDevice;
class IOFilterInterruptEventSource;
class IOInterruptEventSource;
class IOTimerEventSource;

class VoodooHDADevice : public IOAudioDevice
{
//	friend class AppleHDAEngine;
	friend class VoodooHDAEngine;

	OSDeclareDefaultStructors(VoodooHDADevice)

public:
	IOLock *mLock;
	IOWorkLoop *mWorkLoop;

	IOCommandGate::Action mActionHandler;
//Slice	
	OSArray *NodesToPatch;
	int NumNodes;
	PatchArray NodesToPatchArray[MAX_NODES];
	UInt16 oldConfig;
//
	bool mSwitchCh;
	UInt32 Boost;
	
	const char *mControllerName;
	UInt32 mDeviceId, mSubDeviceId;

	IOPCIDevice *mPciNub;
	IOMemoryMap *mBarMap;
	IOVirtualAddress mRegBase;

	int mSupports64Bit;

	int mInStreamsSup; // ISS
	int mOutStreamsSup; // OSS
	int mBiStreamsSup; // BSS
	int mSDO; //SDO ?

	int mCorbSize;
	int mCorbWritePtr; // WP
	DmaMemory *mCorbMem;

	int mRirbSize;
	int mRirbReadPtr; // RP
	DmaMemory *mRirbMem;

	int mStreamCount;
	DmaMemory *mDmaPosMem;

	UInt32 mQuirksOn;
	UInt32 mQuirksOff;

	Codec *mCodecs[HDAC_CODEC_MAX];

	Channel *mChannels;
	int mNumChannels;

	int mUnsolqReadPtr;
	int mUnsolqWritePtr;
	int mUnsolqState;
	UInt32 mUnsolq[HDAC_UNSOLQ_MAX];
	// todo: unsolq task

	IOTimerEventSource *mTimerSource;
	IOFilterInterruptEventSource *mInterruptSource;

	UInt64 mIntTimestamp;
	UInt64 mChanIntMissed;
	UInt32 mIntStatus;
	UInt64 mTotalInt;
	UInt64 mTotalChanInt;
	UInt64 mIntrTimeStamp;

	UInt32 mVerbose;
	//Temporary for init driver
	bool vectorize;
	int noiseLevel;
	bool useStereo;
	int StereoBase;
	//

	bool mMsgBufferEnabled;
	char *mMsgBuffer;
	size_t mMsgBufferSize;
	size_t mMsgBufferPos;
	IOLock *mMessageLock;
	
	bool mSwitchEnable;
	bool mInhibitCache;
	bool mDisableInputMonitor;

	// cue8chalk: flag to enable/disable volume fix (loaded from plist)
	bool mEnableVolumeChangeFix;
	bool mEnableHalfVolumeFix;
    // VertexBZ: flags to enable/disable Mic and Mute fixes (loaded from plist)
	bool mEnableHalfMicVolumeFix;
	bool mEnableMuteFix;

	// Zenith432
	UInt16 mMixerDefaults[SOUND_MIXER_NRDEVICES];
	bool mAllowMSI;
	bool mDmaPosMemAllocated;

	/**************/

	bool resetController(bool wakeup);
	bool getCapabilities();

	UInt8 readData8(UInt32 offset);
	UInt16 readData16(UInt32 offset);
	UInt32 readData32(UInt32 offset);
	void writeData8(UInt32 offset, UInt8 value);
	void writeData16(UInt32 offset, UInt16 value);
	void writeData32(UInt32 offset, UInt32 value);

	virtual bool init(OSDictionary *dictionary = 0);
	virtual IOService *probe(IOService *provider, SInt32 *score);
	virtual bool initHardware(IOService *provider);
	virtual bool createAudioEngine(Channel *channel);
	virtual void stop(IOService *provider);
	virtual void free();

	virtual IOReturn performPowerStateChange(IOAudioDevicePowerState oldPowerState,
			IOAudioDevicePowerState newPowerState, UInt32 *microsecondsUntilComplete);
	bool suspend();
	bool resume();

	/**************/

	void lockMsgBuffer();
	void unlockMsgBuffer();
	void enableMsgBuffer(bool isEnabled);
	void logMsg(const char *format, ...) __attribute__ ((format (printf, 2, 3)));
	void errorMsg(const char *format, ...) __attribute__ ((format (printf, 2, 3)));
	void dumpMsg(const char *format, ...) __attribute__ ((format (printf, 2, 3)));
	void messageHandler(UInt32 type, const char *format, va_list args);

	IOReturn runAction(UInt32 *action, UInt32 *outSize, void **outData, void *extraArg = 0);
	static IOReturn handleAction(OSObject *owner, void *arg0 = 0, void *arg1 = 0, void *arg2 = 0,
			void *arg3 = 0);
	ChannelInfo *getChannelInfo();
	void pinDump();

	void enableEventSources();
	void disableEventSources();

	static void timeoutOccurred(OSObject *owner, IOTimerEventSource *source);
	static bool interruptFilter(OSObject *owner, IOFilterInterruptEventSource *source);
	static void interruptHandler(OSObject *owner, IOInterruptEventSource *source, int count);
	void handleInterrupt();
	static int findInterruptIndex(IOService*, bool);

	bool setupWorkloop();

	/***************/

	void lock(const char *callerName);
	void unlock(const char *callerName);

	void assertLock(IOLock *lock, UInt32 type);

	static void *allocMem(size_t size);
	static void *reallocMem(void *addr, size_t size);
	static void freeMem(void *addr);

	DmaMemory *allocateDmaMemory(mach_vm_size_t size, const char *description, UInt32 cacheOption = kIOMapDefaultCache);
	void freeDmaMemory(DmaMemory *dmaMemory);

	void initCorb();
	void initRirb();
	void startCorb();
	void startRirb();

	int rirbFlush();
	int handleStreamInterrupt(Channel *channel);
	VoodooHDAEngine *lookupEngine(int channelId);
	void handleChannelInterrupt(int channelId);

	UInt32 sendCommand(UInt32 verb, nid_t cad);
	void sendCommands(CommandList *commands, nid_t cad);

	static const char *findCodecName(Codec *codec);
	void scanCodecs();
	void probeCodec(Codec *codec);
	void probeFunction(Codec *codec, nid_t nid);

	int unsolqFlush();
	void handleUnsolicited(Codec *codec, UInt32 tag);
	void micSwitchHandlerEnableWidget(FunctionGroup *funcGroup, nid_t widget, int connNum, bool Enabled);
	void SwitchHandlerRename(FunctionGroup *funcGroup, int assocsNum, nid_t nid, UInt32 res);
	void micSwitchHandler(FunctionGroup *funcGroup, int nid, UInt32 res);
	void hpSwitchHandler(FunctionGroup *funcGroup, int nid, UInt32 res);
	void switchHandler(FunctionGroup *funcGroup, bool first);
	void switchInit(FunctionGroup *funcGroup);

	char *audioCtlMixerMaskToString(UInt32 mask, char *buf, size_t len);

	AudioControl *audioCtlEach(FunctionGroup *funcGroup, int index);
	AudioControl *audioCtlAmpGet(FunctionGroup *funcGroup, nid_t nid, int direction, int index, int cnt);
	void audioCtlAmpSetInternal(nid_t cad, nid_t nid, int index, int lmute, int rmute, int left, int right,
			int direction);
	void audioCtlAmpSet(AudioControl *ctl, UInt32 mute, int left, int right);
	void audioCtlAmpGetInternal(nid_t cad, nid_t nid, int index, int *lmute, int *rmute, int *left, int *right, 
								int direction);
	void audioCtlAmpGetGain(AudioControl *control);
	void widgetConnectionSelect(Widget *widget, UInt8 index);

	UInt32 audioCtlRecSelComm(PcmDevice *pcmDevice, UInt32 src, nid_t nid, int depth);

	int audioCtlSourceAmp(FunctionGroup *funcGroup, nid_t nid, int index, int ossdev, int ctlable, int depth, int need);
	void audioCtlDestAmp(FunctionGroup *funcGroup, nid_t nid, int index, int ossdev,
						 int depth, int need);

	void widgetConnectionParse(Widget *widget);
	UInt32 widgetPinPatch(UInt32 config, const char *str);
	UInt32 widgetPinGetConfig(Widget *widget);
	UInt32 widgetPinGetCaps(Widget *widget);
	void widgetPinParse(Widget *widget);
	UInt32 widgetGetCaps(Widget *widget, int *waspin);
	void widgetParse(Widget *widget);
	Widget *widgetGet(FunctionGroup *funcGroup, nid_t nid);

	void dumpCtls(PcmDevice *pcmDevice, const char *banner, UInt32 flag);
	void dumpAudioFormats(UInt32 fcap, UInt32 pcmcap);
	void dumpPin(Widget *widget);
	void dumpPinConfig(Widget *widget, UInt32 conf);
	void dumpPinConfigs(FunctionGroup *funcGroup);
	void dumpAmp(UInt32 cap, const char *banner);
	void dumpNodes(FunctionGroup *funcGroup);
	void dumpDstNid(PcmDevice *pcmDevice, nid_t nid, int depth);
	void dumpDac(PcmDevice *pcmDevice);
	void dumpAdc(PcmDevice *pcmDevice);
	void dumpMix(PcmDevice *pcmDevice);
	void dumpPcmChannels(PcmDevice *pcmDevice);

	void powerup(FunctionGroup *funcGroup);
	void audioParse(FunctionGroup *funcGroup);
	void audioCtlParse(FunctionGroup *funcGroup);
	void vendorPatchParse(FunctionGroup *funcGroup);
	void audioDisableNonAudio(FunctionGroup *funcGroup);
	void audioDisableUseless(FunctionGroup *funcGroup);
	void audioAssociationParse(FunctionGroup *funcGroup);
	void audioBuildTree(FunctionGroup *funcGroup);
	void audioDisableUnassociated(FunctionGroup *funcGroup);
	void audioDisableNonSelected(FunctionGroup *funcGroup);
	void audioDisableCrossAssociations(FunctionGroup *funcGroup);
	nid_t audioTraceDac(FunctionGroup *funcGroup, int assocNum, int seq, nid_t nid, int dupseq, int min,
			int only, int depth);
	nid_t audioTraceAdc(FunctionGroup *funcGroup, int assocNum, int seq, nid_t nid, int only, int depth);
	void audioUndoTrace(FunctionGroup *funcGroup, int assocNum, int seq);
	int audioTraceAssociationOut(FunctionGroup *funcGroup, int assocNum, int seq);
	int audioTraceAssociationIn(FunctionGroup *funcGroup, int assocNum);
	int audioTraceToOut(FunctionGroup *funcGroup, nid_t nid, int depth);
//	void audioTraceSwitchNid(FunctionGroup *funcGroup, int assocNum);
	void audioTraceAssociationExtra(FunctionGroup *funcGroup);
	void audioBindAssociation(FunctionGroup *funcGroup);
	void audioAssignNames(FunctionGroup *funcGroup);
	void audioAssignMixers(FunctionGroup *funcGroup);
	void audioPreparePinCtrl(FunctionGroup *funcGroup);
	void audioCtlCommit(FunctionGroup *funcGroup);
	void audioCommit(FunctionGroup *funcGroup);

	int pcmChannelSetup(Channel *channel);
	void createPcms(FunctionGroup *funcGroup);

	/***************/

	int audioCtlOssMixerInit(PcmDevice *pcmDevice);
	int audioCtlOssMixerSet(PcmDevice *pcmDevice, UInt32 dev, UInt32 left, UInt32 right);
	int ilerp(int a, int b, float t);	// cue8chalk
	UInt32 audioCtlOssMixerSetRecSrc(PcmDevice *pcmDevice, UInt32 src);
	int audioCtlOssMixerGet(PcmDevice *pcmDevice, UInt32 dev, UInt32* left, UInt32* right);
	void mixerSetDefaults(PcmDevice *pcmDevice);
	void mixerResume(PcmDevice *pcmDevice);

	Channel *channelInit(PcmDevice *pcmDevice, int direction);
	int channelSetFormat(Channel *channel, UInt32 format);
	int channelSetSpeed(Channel *channel, UInt32 reqSpeed);
	void channelStop(Channel *channel, bool shouldLock = true);
	void channelStart(Channel *channel, bool shouldLock = true);
	int channelGetPosition(Channel *channel);

	void streamSetup(Channel *channel);
	void streamHDMIorDPExtraSetup(Channel *channel, nid_t, AudioAssoc*, int, int);
	void streamStop(Channel *channel);
	void streamStart(Channel *channel);
	void streamReset(Channel *channel);
	void streamSetId(Channel *channel);

	void bdlSetup(Channel *channel);
	int bdlAlloc(Channel *channel);

	int pcmAttach(PcmDevice *pcmDevice);
//AutumnRain	
/***************/
	
	sliderTab sliderTabs[25];
	UInt8 nSliderTabsCount;
	
	ChannelInfo *mPrefPanelMemoryBuf;
	bool mPrefPanelMemoryBufEnabled;
	size_t mPrefPanelMemoryBufSize;
	IOLock *mPrefPanelMemoryBufLock;
	void lockPrefPanelMemoryBuf();
	void unlockPrefPanelMemoryBuf();
	
  void hdaa_eld_handler(Widget *widget);
	void catPinName(Widget *widget); //UInt32 config, char *buf, size_t size);
	
	void changeSliderValue(UInt8 tabNum, UInt8 sliderNum, UInt8 newValue);
	void setMath(UInt8 tabNum, UInt8 sliderNum, UInt8 newValue);
	
	//Создаем разделяемую область памяти, откуда будет брать информацию PrefPanel
	void createPrefPanelMemoryBuf(FunctionGroup *funcGroup);
	//Создаем структуру в которой запомним какие объекты AudioControl каким регуляторам на панели PrefPanel соотвествуют
	void createPrefPanelStruct(FunctionGroup *funcGroup);
	//Считываем текущее настройки усиления и сохраняем их в разделяемой памяти
	void updatePrefPanelMemoryBuf(void);
	void freePrefPanelMemoryBuf(void);
	
	/*********************/
	
	struct assocTree{
		nid_t nid[MAX_TREE_LENGHT];
		int count;
	} treePin[16];
	
	/*********************/
	
	char *mExtMsgBuffer;
	size_t mExtMsgBufferSize;
	size_t mExtMsgBufferPos;
	IOLock *mExtMessageLock;
	
	void lockExtMsgBuffer();
	void unlockExtMsgBuffer();
	void dumpExtMsg(const char *format, ...) __attribute__ ((format (printf, 2, 3)));

	void updateExtDump(void);
	void updateExtDumpForFunctionGroup(FunctionGroup *funcGroup);
	void extDumpAmp(UInt32 cap, const char *banner);
	void extDumpNodes(FunctionGroup *funcGroup);
	void extDumpCtls(PcmDevice *pcmDevice, const char *banner, UInt32 flag);
	void extDumpPin(Widget *widget);
	
	/*********************/
	void initMixerDefaultValues(void);
	void disablePCIeNoSnoop(UInt16 vendorId);
};

#endif
