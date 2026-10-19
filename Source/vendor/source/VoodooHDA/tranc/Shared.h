#include "License.h"

#ifndef _SHARED_H
#define _SHARED_H

#define kVoodooHDAClassName	"VoodooHDADevice"

enum {
    kVoodooHDAActionMethod = 0,
    kVoodooHDANumMethods
};

enum {
	kVoodooHDAActionTest = 0x1000
};

enum {
	kVoodooHDAMemoryMessageBuffer = 0x2000,
	kVoodooHDAMemoryPinDump,
	kVoodooHDAMemoryCommand = 0x3000,
	kVoodooHDAMemoryExtMessageBuffer
};
#define MAX_SLIDER_TAB_NAME_LENGTH 32

/*  Slice -> sorry AutumnRain = mixerDeviceInfo
typedef struct _sliderInfo{
	unsigned char num; //Порядковый	номер регулятора
	unsigned char value; //Значение усиления от 0 до 0x64
	char sliderName[MAX_SLIDER_TAB_NAME_LENGTH]; //Название регулятора
	unsigned char enabled; //Можно ли пользователю менять значение регулятора
}sliderInfo;

typedef struct sliders{
	char tabName[MAX_SLIDER_TAB_NAME_LENGTH]; //Название PinComplex для которого менятся значения усиления
	sliderInfo info[SOUND_MIXER_NRDEVICES];
	unsigned char size; //Число структур
	unsigned char non[3];
}sliders;
*/

enum {
	kVoodooHDAChannelNames = 0x3000
};

enum {
	kVoodooHDAActionSetMixer = 0x40,
	kVoodooHDAActionGetMixers = 0x50,
	kVoodooHDAActionSetMath = 0x60
};

typedef union {
	struct {
		UInt8 action;
		UInt8 channel;
		UInt8 device;
		UInt8 val;
	} info;
	UInt32 value;
} actionInfo;

typedef struct _mixerDeviceInfo {
	UInt8 mixId;
	UInt8 value;
	char name[MAX_SLIDER_TAB_NAME_LENGTH];
	bool enabled;
	UInt8 non[5]; //align to 8 bytes
} mixerDeviceInfo;

typedef struct _ChannelInfo {
	char name[MAX_SLIDER_TAB_NAME_LENGTH];
	mixerDeviceInfo mixerValues[SOUND_MIXER_NRDEVICES];
	UInt8 numChannels;
	bool vectorize;
	bool useStereo;
    UInt8 noiseLevel;
	UInt8 StereoBase;
	UInt8 empty[3]; //align to 8 bytes
} ChannelInfo;

#endif
