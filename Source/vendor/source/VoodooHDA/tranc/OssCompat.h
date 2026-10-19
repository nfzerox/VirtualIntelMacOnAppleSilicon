#include "License.h"

#ifndef _OSS_COMPAT_H
#define _OSS_COMPAT_H

#define PCMDIR_FAKE				0
#define PCMDIR_PLAY				1
#define PCMDIR_PLAY_VIRTUAL		2
#define PCMDIR_REC				-1
#define PCMDIR_REC_VIRTUAL		-2

#define SOUND_MIXER_NRDEVICES	25
#define SOUND_MIXER_VOLUME		0	/* Master output level */
#define SOUND_MIXER_BASS		1	/* [unused] Treble level of all output channels */
#define SOUND_MIXER_TREBLE		2	/* [unused] Bass level of all output channels */
#define SOUND_MIXER_SYNTH		3	/* [unused] Volume of synthesier input */
#define SOUND_MIXER_PCM			4	/* Output level for the audio device */
#define SOUND_MIXER_SPEAKER		5	/* Output level for the PC speaker signals */
#define SOUND_MIXER_LINE		6	/* Volume level for the line in jack */
#define SOUND_MIXER_MIC			7	/* Volume for the signal coming from the microphone jack */
#define SOUND_MIXER_CD			8	/* Volume level for the input signal connected to the CD audio input */
#define SOUND_MIXER_IMIX		9	/* Recording monitor. It controls the output volume of the selected
									 * recording sources while recording */
#define SOUND_MIXER_ALTPCM		10	/* [unused] Volume of the alternative codec device */
#define SOUND_MIXER_RECLEV		11	/* Global recording level */
#define SOUND_MIXER_IGAIN		12	/* Input gain */
#define SOUND_MIXER_OGAIN		13	/* Output gain */
/* The AD1848 codec and compatibles have three line level inputs
 * (line, aux1 and aux2). Since each card manufacturer have assigned
 * different meanings to these inputs, it's inpractical to assign
 * specific meanings (line, cd, synth etc.) to them. */
#define SOUND_MIXER_LINE1		14	/* Input source 1  (aux1) */
#define SOUND_MIXER_LINE2		15	/* Input source 2  (aux2) */
#define SOUND_MIXER_LINE3		16	/* Input source 3  (line) */
#define SOUND_MIXER_DIGITAL1    17	/* Digital (input) 1 */
#define SOUND_MIXER_DIGITAL2    18	/* Digital (input) 2 */
#define SOUND_MIXER_DIGITAL3    19	/* Digital (input) 3 */
#define SOUND_MIXER_PHONEIN     20	/* Phone input */
#define SOUND_MIXER_PHONEOUT    21	/* Phone output */
#define SOUND_MIXER_VIDEO       22	/* Video/TV (audio) in */
#define SOUND_MIXER_RADIO       23	/* Radio in */
#define SOUND_MIXER_MONITOR     24	/* Monitor (usually mic) volume */

#define SOUND_MASK_VOLUME		(1 << SOUND_MIXER_VOLUME)
#define SOUND_MASK_BASS			(1 << SOUND_MIXER_BASS)
#define SOUND_MASK_TREBLE		(1 << SOUND_MIXER_TREBLE)
#define SOUND_MASK_SYNTH		(1 << SOUND_MIXER_SYNTH)
#define SOUND_MASK_PCM			(1 << SOUND_MIXER_PCM)
#define SOUND_MASK_SPEAKER		(1 << SOUND_MIXER_SPEAKER)
#define SOUND_MASK_LINE			(1 << SOUND_MIXER_LINE)
#define SOUND_MASK_MIC			(1 << SOUND_MIXER_MIC)
#define SOUND_MASK_CD			(1 << SOUND_MIXER_CD)
#define SOUND_MASK_IMIX			(1 << SOUND_MIXER_IMIX)
#define SOUND_MASK_ALTPCM		(1 << SOUND_MIXER_ALTPCM)
#define SOUND_MASK_RECLEV		(1 << SOUND_MIXER_RECLEV)
#define SOUND_MASK_IGAIN		(1 << SOUND_MIXER_IGAIN)
#define SOUND_MASK_OGAIN		(1 << SOUND_MIXER_OGAIN)
#define SOUND_MASK_LINE1		(1 << SOUND_MIXER_LINE1)
#define SOUND_MASK_LINE2		(1 << SOUND_MIXER_LINE2)
#define SOUND_MASK_LINE3		(1 << SOUND_MIXER_LINE3)
#define SOUND_MASK_DIGITAL1     (1 << SOUND_MIXER_DIGITAL1)
#define SOUND_MASK_DIGITAL2     (1 << SOUND_MIXER_DIGITAL2)
#define SOUND_MASK_DIGITAL3     (1 << SOUND_MIXER_DIGITAL3)
#define SOUND_MASK_PHONEIN      (1 << SOUND_MIXER_PHONEIN)
#define SOUND_MASK_PHONEOUT     (1 << SOUND_MIXER_PHONEOUT)
#define SOUND_MASK_RADIO        (1 << SOUND_MIXER_RADIO)
#define SOUND_MASK_VIDEO        (1 << SOUND_MIXER_VIDEO)
#define SOUND_MASK_MONITOR      (1 << SOUND_MIXER_MONITOR)
//Slice
//#define SOUND_MASK_INPUT	(SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD  | SOUND_MASK_MONITOR)
#define SOUND_MASK_INPUT  (SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_MONITOR)
//

#define SOUND_DEVICE_NAMES	{ \
		"vol", "bass", "treble", "synth", "pcm", "speaker", "line", \
		"mic", "cd", "mix", "pcm2", "rec", "igain", "ogain", \
		"line1", "line2", "line3", "dig1", "dig2", "dig3", \
		"phin", "phout", "video", "radio", "monitor", "out_mix" }

#define AFMT_S16_LE     0x00000010      /* Little endian signed 16-bit */
#define AFMT_AC3        0x00000400      /* Dolby Digital AC3 */
#define AFMT_S32_LE     0x00001000      /* Little endian signed 32-bit */
#define AFMT_S24_LE     0x00010000      /* [unused] Little endian signed 24-bit */
#define AFMT_STEREO     0x10000000      /* can do/want stereo   */

/*
 * We're simply using unused, contiguous bits from various AFMT_ definitions.
 * ~(0xb00ff7ff)
 */
#define AFMT_ENCODING_MASK      0xf00fffff
#define AFMT_CHANNEL_MASK       0x01f00000
#define AFMT_CHANNEL_SHIFT      20
#define AFMT_EXTCHANNEL_MASK    0x0e000000
#define AFMT_EXTCHANNEL_SHIFT   25

#define AFMT_ENCODING(v)        ((v) & AFMT_ENCODING_MASK)

#define AFMT_EXTCHANNEL(v)      (((v) & AFMT_EXTCHANNEL_MASK) >>        \
	AFMT_EXTCHANNEL_SHIFT)

#define AFMT_CHANNEL(v)         (((v) & AFMT_CHANNEL_MASK) >>           \
	AFMT_CHANNEL_SHIFT)


#define SND_FORMAT(f, c, m)     (AFMT_ENCODING(f) |             \
	(((c) << AFMT_CHANNEL_SHIFT) &          \
	AFMT_CHANNEL_MASK) |                    \
	(((m) << AFMT_EXTCHANNEL_SHIFT) &       \
	AFMT_EXTCHANNEL_MASK))

#define AFMT_ALIGN(v)           (AFMT_BPS(v) * AFMT_CHANNEL(v))

#define AFMT_U8_NE      AFMT_U8
#define AFMT_S8_NE      AFMT_S8

#undef AFMT_S16_NE

#define AFMT_S16_NE     AFMT_S16_LE
#define AFMT_S24_NE     AFMT_S24_LE
#define AFMT_S32_NE     AFMT_S32_LE
#define AFMT_U16_NE     AFMT_U16_LE
#define AFMT_U24_NE     AFMT_U24_LE
#define AFMT_U32_NE     AFMT_U32_LE
#define AFMT_S16_OE     AFMT_S16_BE
#define AFMT_S24_OE     AFMT_S24_BE
#define AFMT_S32_OE     AFMT_S32_BE
#define AFMT_U16_OE     AFMT_U16_BE
#define AFMT_U24_OE     AFMT_U24_BE
#define AFMT_U32_OE     AFMT_U32_BE
#endif
