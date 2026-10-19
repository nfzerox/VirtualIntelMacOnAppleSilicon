#include "License.h"

#ifndef _TABLES_H
#define _TABLES_H

#include <IOKit/IOTypes.h>

typedef struct {
	UInt32 model;
	char const *name;
} ControllerListItem;

typedef struct {
	UInt32 id;
	char const *name;
} CodecListItem;

typedef struct {
	UInt32 rate;
	int valid;
	UInt16 base;
	UInt16 mul;
	UInt16 div;
} RateTableItem;

typedef struct {
	char const *key;
	UInt32 value;
} QuirkType;

typedef struct {
	UInt32 model;
	UInt32 id;
	UInt32 set, unset;
} QuirkListItem;

typedef struct {
	const char* name;
	int index;
	UInt16 initValue;
}MixerValueName;


extern const ControllerListItem gControllerList[];
extern const CodecListItem gCodecList[];
extern const RateTableItem gRateTable[];
extern const QuirkType gQuirkTypes[];
extern const QuirkListItem gQuirkList[];
extern const MixerValueName MixerValueNamesBind[];

#define HDA_GPIO_MAX			8
/* 0 - 7 = GPIO , 8 = Flush */
#define HDA_QUIRK_GPIO0			(1 << 0)
#define HDA_QUIRK_GPIO1			(1 << 1)
#define HDA_QUIRK_GPIO2			(1 << 2)
#define HDA_QUIRK_GPIO3			(1 << 3)
#define HDA_QUIRK_GPIO4			(1 << 4)
#define HDA_QUIRK_GPIO5			(1 << 5)
#define HDA_QUIRK_GPIO6			(1 << 6)
#define HDA_QUIRK_GPIO7			(1 << 7)
#define HDA_QUIRK_GPIOFLUSH		(1 << 8)

/* 9 - 25 = anything else */
#define HDA_QUIRK_SOFTPCMVOL	(1 << 9)
#define HDA_QUIRK_FIXEDRATE		(1 << 10)
#define HDA_QUIRK_FORCESTEREO	(1 << 11)
#define HDA_QUIRK_EAPDINV		(1 << 12)
#define HDA_QUIRK_DMAPOS		(1 << 13)
#define HDA_QUIRK_SENSEINV		(1 << 14)

/* 26 - 31 = vrefs */
#define HDA_QUIRK_IVREF50		(1 << 26)
#define HDA_QUIRK_IVREF80		(1 << 27)
#define HDA_QUIRK_IVREF100		(1 << 28)
#define HDA_QUIRK_OVREF50		(1 << 29)
#define HDA_QUIRK_OVREF80		(1 << 30)
#define HDA_QUIRK_OVREF100		(1 << 31)

#define HDA_QUIRK_IVREF			(HDA_QUIRK_IVREF50 | HDA_QUIRK_IVREF80 | HDA_QUIRK_IVREF100)
#define HDA_QUIRK_OVREF			(HDA_QUIRK_OVREF50 | HDA_QUIRK_OVREF80 | HDA_QUIRK_OVREF100)
#define HDA_QUIRK_VREF			(HDA_QUIRK_IVREF | HDA_QUIRK_OVREF)

#endif
