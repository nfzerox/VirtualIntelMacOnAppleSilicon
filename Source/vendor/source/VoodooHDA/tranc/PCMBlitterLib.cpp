#include "License.h"

#include "PCMBlitterLibDispatch.h"

/*
	This file contains portable int<->float blitters.
*/
#ifdef __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#define TARGET_OS_MAC 1
#endif
#if defined(__i386__) || defined(__x86_64__)
#undef  TARGET_CPU_X86
#define TARGET_CPU_X86 1
#undef  TARGET_RT_LITTLE_ENDIAN
#define TARGET_RT_LITTLE_ENDIAN 0
#endif

#if !TARGET_OS_MAC
// we have optimized versions of most of these for Mac OS

#pragma mark -
#pragma mark 16-bit
// ____________________________________________________________________________
//
void NativeInt16ToFloat32_Portable(const SInt16 *src, Float32 *dest, unsigned int count)
{
	TIntToFloatBlitter<PCMSInt16Native, PCMFloat32> blitter(16);
	
	blitter.Convert(src, dest, count);
}

void SwapInt16ToFloat32_Portable(const SInt16 *src, Float32 *dest, unsigned int count)
{
	TIntToFloatBlitter<PCMSInt16Swap, PCMFloat32> blitter(16);
	
	blitter.Convert(src, dest, count);
}

void Float32ToNativeInt16_Portable(const Float32 *src, SInt16 *dest, unsigned int count)
{
	TFloatToIntBlitter<PCMFloat32, PCMSInt16Native> blitter(16);
	
	blitter.Convert(src, dest, count);
}

void Float32ToSwapInt16_Portable(const Float32 *src, SInt16 *dest, unsigned int count)
{
	TFloatToIntBlitter<PCMFloat32, PCMSInt16Swap> blitter(16);
	
	blitter.Convert(src, dest, count);
}

#pragma mark -
#pragma mark 32-bit
// ____________________________________________________________________________
//
void NativeInt32ToFloat32_Portable(const SInt32 *src, Float32 *dest, unsigned int count)
{
	TIntToFloatBlitter<PCMSInt32Native, PCMFloat32> blitter(32);
	
	blitter.Convert(src, dest, count);
}

void SwapInt32ToFloat32_Portable(const SInt32 *src, Float32 *dest, unsigned int count)
{
	TIntToFloatBlitter<PCMSInt32Swap, PCMFloat32> blitter(32);
	
	blitter.Convert(src, dest, count);
}

void Float32ToNativeInt32_Portable(const Float32 *src, SInt32 *dest, unsigned int count)
{
	TFloatToIntBlitter<PCMFloat32, PCMSInt32Native> blitter(32);
	
	blitter.Convert(src, dest, count);
}

void Float32ToSwapInt32_Portable(const Float32 *src, SInt32 *dest, unsigned int count)
{
	TFloatToIntBlitter<PCMFloat32, PCMSInt32Swap> blitter(32);
	
	blitter.Convert(src, dest, count);
}

#pragma mark -
#pragma mark 24-bit

// ____________________________________________________________________________
//
void	Float32ToNativeInt24_Portable(const Float32 *src, UInt8 *vdest, unsigned int nSamples)
{
	UInt32 *dest = (UInt32 *)vdest;
	double maxInt32 = 2147483648.0;	// 1 << 31
	double round = 128.0;
	double max32 = maxInt32 - 1.0 - round;
	double min32 = -2147483648.0;
	int shift = 8, count;

	SET_ROUNDMODE
			
	count = nSamples >> 2;
	while (count--) {
		double f1 = src[0] * maxInt32 + round;
		double f2 = src[1] * maxInt32 + round;
		double f3 = src[2] * maxInt32 + round;
		double f4 = src[3] * maxInt32 + round;
		SInt32 i1 = FloatToInt(f1, min32, max32) >> shift;
		SInt32 i2 = FloatToInt(f2, min32, max32) >> shift;
		SInt32 i3 = FloatToInt(f3, min32, max32) >> shift;
		SInt32 i4 = FloatToInt(f4, min32, max32) >> shift;
		// memory: a3 a2 a1 b3	register: b3 a1 a2 a3
		dest[0] = (i1 & 0xFFFFFF) | ((i2 & 0xFF) << 24);
		// memory: b2 b1 c3 c2	register: c2 c3 b1 b2
		dest[1] = (i3 << 16) | ((i2 >> 8) & 0xFFFF);
		// memory: c1 d3 d2 d1	register: d1 d2 d3 c1
		dest[2] = ((i3 >> 16) & 0xFF) | ((i4 & 0xFFFFFF) << 8);
		src += 4;
		dest += 3;
	}
	UInt8 *p = (UInt8 *)dest;
	count = nSamples & 3;
	while (count--) {
		double f1 = *src++ * maxInt32 + round;
		SInt32 i1 = FloatToInt(f1, min32, max32) >> shift;
		p[0] = UInt8(i1);
		p[1] = UInt8(i1 >> 8);
		p[2] = UInt8(i1 >> 16);
		p += 3;
	}
	RESTORE_ROUNDMODE
};

#endif // !TARGET_OS_MAC

#if !TARGET_OS_MAC || TARGET_CPU_X86
// These are needed for both non-MacOS and MacOS/X86

// ____________________________________________________________________________
//
void	Float32ToSwapInt24_Portable(const Float32 *src, UInt8 *vdest, unsigned int nSamples)
{
	UInt32 *dest = (UInt32 *)vdest;
	double maxInt32 = 2147483648.0;	// 1 << 31
	double round = 128.0;
	double max32 = maxInt32 - 1.0 - round;
	double min32 = -2147483648.0;
	int shift = 8, count;
	
	SET_ROUNDMODE

	count = nSamples >> 2;
	while (count--) {
		double f1 = src[0] * maxInt32 + round;
		double f2 = src[1] * maxInt32 + round;
		double f3 = src[2] * maxInt32 + round;
		double f4 = src[3] * maxInt32 + round;
		SInt32 i1 = FloatToInt(f1, min32, max32) >> shift;
		SInt32 i2 = FloatToInt(f2, min32, max32) >> shift;
		SInt32 i3 = FloatToInt(f3, min32, max32) >> shift;
		SInt32 i4 = FloatToInt(f4, min32, max32) >> shift;
		// memory: a1 a2 a3 b1	register: b1 a3 a2 a1
		dest[0] = ((i2 & 0x00FF0000) << 8)
				| ((i1 & 0x000000FF) << 16)
				|  (i1 & 0x0000FF00)
				| ((i1 & 0x00FF0000) >> 16);
		// memory: b2 b3 c1 c2	register: c2 c1 b3 b2
		dest[1] = ((i3 & 0x0000FF00) << 16)
				|  (i3 & 0x00FF0000)
				| ((i2 & 0x000000FF) << 8)
				| ((i2 & 0x0000FF00) >> 8);
		// memory: c3 d1 d2 d3	register: d3 d2 d1 c3
		dest[2] = ((i4 & 0x000000FF) << 24)
				| ((i4 & 0x0000FF00) << 8)
				| ((i4 & 0x00FF0000) >> 8)
				|  (i3 & 0x000000FF);
		src += 4;
		dest += 3;
	}
	UInt8 *p = (UInt8 *)dest;
	count = nSamples & 3;
	while (count--) {
		double f1 = *src++ * maxInt32 + round;
		SInt32 i1 = FloatToInt(f1, min32, max32) >> shift;
		p[0] = UInt8(i1 >> 16);
		p[1] = UInt8(i1 >> 8);
		p[2] = UInt8(i1);
		p += 3;
	}
	RESTORE_ROUNDMODE
}

// ____________________________________________________________________________
//
void	NativeInt24ToFloat32_Portable(const UInt8 *vsrc, Float32 *dest, unsigned int count)
{
	const UInt32 *src = (const UInt32 *)vsrc;
	Float32 scale = (1. / 2147483648.0);
	int nSamples4 = count >> 2;
	
	while (nSamples4--) {
		SInt32 lv1 = src[0];	// BE: a1 a2 a3 b1		LE memory: a3 a2 a1 b3	register: b3 a1 a2 a3
		SInt32 lv2 = src[1];	// BE: b2 b3 c1 c2		LE memory: b2 b1 c3 c2	register: c2 c3 b1 b2
		SInt32 lv3 = src[2];	// BE: c3 d1 d2 d3		LE memory: c1 d3 d2 d1	register: d1 d2 d3 c1
		SInt32 lv4;
		
//		printf("%08X %08X %08X => ", lv1, lv2, lv3);
		lv4 = lv3 & 0xFFFFFF00;
		lv3 = (lv3 << 24) | ((lv2 & 0xFFFF0000) >> 8);
		lv2 = (lv2 << 16) | ((lv1 & 0xFF000000) >> 16);
		lv1 = (lv1 << 8);
//		printf("%08X %08X %08X %08X\n", lv1, lv2, lv3, lv4);

		dest[0] = lv1 * scale;
		dest[1] = lv2 * scale;
		dest[2] = lv3 * scale;
		dest[3] = lv4 * scale;
		
		src += 3;
		dest += 4;
	}
	int nSamples = count & 3;
	UInt8 *p = (UInt8 *)src;
	while (nSamples--) {
		SInt32 lv = p[0] | (p[1] << 8) | (p[2] << 16);
		lv <<= 8;
		p += 3;
		*dest++ = lv * scale;
	}
}

// ____________________________________________________________________________
//
void	SwapInt24ToFloat32_Portable(const UInt8 *vsrc, Float32 *dest, unsigned int count)
{
	const UInt32 *src = (const UInt32 *)vsrc;
	Float32 scale = (1. / 2147483648.0);
	int nSamples4 = count >> 2;
	
	while (nSamples4--) {
		SInt32 lv1 = src[0];	// BE: a3 a2 a1 b3		LE memory: a1 a2 a3 b1	register: b1 a3 a2 a1
		SInt32 lv2 = src[1];	// BE: b2 b1 c3 c2		LE memory: b2 b3 c1 c2	register: c2 c1 b3 b2
		SInt32 lv3 = src[2];	// BE: c1 d3 d2 d1		LE memory: c3 d1 d2 d3	register: d3 d2 d1 c3
		SInt32 lv4;

//		printf("%08X %08X %08X => ", lv1, lv2, lv3);
		lv4 = 	  ((lv3 & 0x0000FF00) << 16)
				|  (lv3 & 0x00FF0000)
				| ((lv3 & 0xFF000000) >> 16);
		lv3 = 	  ((lv2 & 0x00FF0000) << 8)
				| ((lv2 & 0xFF000000) >> 8)
				| ((lv3 & 0x000000FF) << 8);
		lv2 =     (lv1 & 0xFF000000)
				| ((lv2 & 0x000000FF) << 16)
				|  (lv2 & 0x0000FF00);
		lv1 = 	   (lv1 << 24)
				| ((lv1 & 0x0000FF00) << 8)
				| ((lv1 & 0x00FF0000) >> 8);
//		printf("%08X %08X %08X %08X\n", lv1, lv2, lv3, lv4);

		dest[0] = lv1 * scale;
		dest[1] = lv2 * scale;
		dest[2] = lv3 * scale;
		dest[3] = lv4 * scale;
		
		src += 3;
		dest += 4;
	}
	int nSamples = count & 3;
	UInt8 *p = (UInt8 *)src;
	while (nSamples--) {
#if TARGET_RT_LITTLE_ENDIAN
		SInt32 lv = (p[0] << 16) | (p[1] << 8) | p[2];
#else
		SInt32 lv = p[0] | (p[1] << 8) | (p[2] << 16);
#endif
		lv <<= 8;
		p += 3;
		*dest++ = lv * scale;
	}
}
#endif

