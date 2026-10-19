#include "License.h"

#ifndef _VOODOO_HDA_ENGINE_H
#define _VOODOO_HDA_ENGINE_H

#include <IOKit/IOLib.h>

#include <IOKit/audio/IOAudioEngine.h>

#include "Private.h"

class VoodooHDADevice;

class IOAudioPort;
class IOAudioSelectorControl;
class IOAudioLevelControl;
class IOAudioToggleControl;

class VoodooHDAEngine : public IOAudioEngine
{
	OSDeclareDefaultStructors(VoodooHDAEngine)

public:
	UInt32 mVerbose;

	UInt32 mBufferSize;
	UInt32 mSampleSize;
	UInt32 mNumSampleFrames;
	UInt32 Boost;

	Channel *mChannel;
	VoodooHDADevice *mDevice;
	IOAudioStream *mStream;
	bool emptyStream;

	const char *mPortName;
	UInt32 mPortType;

	IOAudioSelectorControl *mSelControl;
	
	UInt32					oldOutVolumeLeft;
	UInt32					oldOutVolumeRight;
	UInt32					oldInputGain;

	// cue8chalk: flag for volume change fix
	bool mEnableVolumeChangeFix;
    // VertexBZ: flag for mute fix
	bool mEnableMuteFix;
	
	void messageHandler(UInt32 type, const char *format, ...) __attribute__ ((format (printf, 3, 4)));

	void setPinName(UInt32 pinConfig, const char* name);
	const char *getPortName();
	UInt64 getMinMaxDb(UInt32 mask);
	bool haveDigitalMuteControl(UInt32 mask);

	IOAudioStreamDirection getEngineDirection();
	int getEngineId();

	bool publishChannelLayout(IOAudioStreamDirection direction, UInt32 channels);
	bool createAudioStream(IOAudioStreamDirection direction, void *sampleBuffer,
			UInt32 sampleBufferSize, UInt32 *pcmRates,
			UInt32 supPcmSizeRates, UInt32 supStreamFormats, UInt32 channels);
	bool createAudioStream();

	bool createAudioControls();
	
	static IOReturn volumeChangeHandler(IOService *target, IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
	static IOReturn muteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);

	IOReturn volumeChanged(IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
	IOReturn muteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);

	virtual bool initWithChannel(Channel *channel);
	virtual void free();
	virtual bool initHardware(IOService *provider);

	virtual IOReturn performAudioEngineStart();
	virtual IOReturn performAudioEngineStop();

	virtual UInt32 getCurrentSampleFrame();

	virtual IOReturn performFormatChange(IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat,
			const IOAudioSampleRate *newSampleRate);

	virtual IOReturn clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame,
			UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
	virtual IOReturn convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame,
			UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
	virtual OSString *getLocalUniqueID();
};

#endif
