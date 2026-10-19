#include "License.h"

#ifndef __PCMBlitterLib_h__
#define __PCMBlitterLib_h__

#include <IOKit/IOTypes.h>

typedef float	Float32;
typedef double	Float64;

// ============================================================================
//
// N.B. These functions should not be called directly; use the interfaces in 
//	PCMBlitterLibDispatch.h.
//
// ============================================================================

typedef const void *ConstVoidPtr;

#pragma mark -
#pragma mark Utilities

// our compiler does ALL floating point with SSE
#define GETCSR()    ({ int _result; asm volatile ("stmxcsr %0" : "=m" (*&_result)); /*return*/ _result; })
#define SETCSR(a)    { int _temp = a; asm volatile("ldmxcsr %0" : : "m" (*&_temp)); }

#define DISABLE_DENORMALS int _savemxcsr = GETCSR(); SETCSR(_savemxcsr | 0x8040);
#define RESTORE_DENORMALS SETCSR(_savemxcsr);

#define ROUNDMODE_NEG_INF int _savemxcsr = GETCSR(); SETCSR((_savemxcsr & ~0x6000) | 0x2000);
#define RESTORE_ROUNDMODE SETCSR(_savemxcsr);
#define SET_ROUNDMODE 		ROUNDMODE_NEG_INF

// ____________________________________________________________________________
//
// FloatToInt
// N.B. Functions which use this should invoke SET_ROUNDMODE / RESTORE_ROUNDMODE.
static inline SInt32 FloatToInt(double inf, __unused double min32, double max32)
{
	if (inf >= max32) return 0x7FFFFFFF;
	return (SInt32)inf;
}

#ifdef __cplusplus
#pragma mark -
#pragma mark C++ templates
// ____________________________________________________________________________
//
//	PCMBlitter
class PCMBlitter {
public:
	virtual void	Convert(const void *vsrc, void *vdest, unsigned int nSamples) = 0;
						// nSamples = nFrames * nChannels
	virtual ~PCMBlitter() { }
};

// ____________________________________________________________________________
//
//	Types for use in templates
class PCMFloat32 {
public:
	typedef Float32 value_type;
	
	static value_type load(const value_type *p) { return *p; }
	static void store(value_type *p, float val)	{ *p = val; }
};

class PCMFloat64 {
public:
	typedef Float64 value_type;
	
	static value_type load(const value_type *p) { return *p; }
	static void store(value_type *p, value_type val) { *p = val; }
};

class PCMSInt8 {
public:
	typedef SInt8 value_type;
	
	static value_type load(const value_type *p) { return *p; }
	static void store(value_type *p, int val)	{ *p = val; }
};

class PCMUInt8 {
public:
	typedef SInt8 value_type;	// signed so that sign-extending works right
	
	static value_type load(const value_type *p) { return *p ^ 0x80; }
	static void store(value_type *p, int val)	{ *p = val ^ 0x80; }
};

class PCMSInt16Native {
public:
	typedef SInt16 value_type;
	
	static value_type load(const value_type *p) { return *p; }
	static void store(value_type *p, int val)	{ *p = val; }
};

class PCMSInt16Swap {
public:
	typedef SInt16 value_type;
	
	static value_type load(const value_type *p)
	{
		return OSReadSwapInt16(p, 0);
	}
	static void store(value_type *p, int val)
	{
		OSWriteSwapInt16(p, 0, val);
	}
};

class PCMSInt32Native {
public:
	typedef SInt32 value_type;
	
	static value_type load(const value_type *p) { return *p; }
	static void store(value_type *p, int val)	{ *p = val; }
};

class PCMSInt32Swap {
public:
	typedef UInt32 value_type;
	
	static value_type load(const value_type *p)
	{
		return OSReadSwapInt32(p, 0);
	}
	static void store(value_type *p, int val)
	{
		*p = val;
		OSWriteSwapInt32(p, 0, val);
	}
};

class PCMFloat64Swap {
public:
	typedef Float64 value_type;
	
	static value_type load(const value_type *vp) {
		union {
			Float64 d;
			UInt32	i[2];
		} u;
		UInt32 *p = (UInt32 *)vp;
		u.i[0] = PCMSInt32Swap::load(p + 1);
		u.i[1] = PCMSInt32Swap::load(p + 0);
		return u.d;
	}
	static void store(value_type *vp, value_type val) {
		union {
			Float64 d;
			UInt32	i[2];
		} u;
		u.d = val;
		UInt32 *p = (UInt32 *)vp;
		PCMSInt32Swap::store(p + 1, u.i[0]);
		PCMSInt32Swap::store(p + 0, u.i[1]);
	}
};

// ____________________________________________________________________________
//
// FloatToIntBlitter
class FloatToIntBlitter : public PCMBlitter {
public:
	FloatToIntBlitter(int bitDepth)
	{
		int rightShift = 32 - bitDepth;
		mShift = rightShift;
		mRound = (rightShift > 0) ? double(1L << (rightShift - 1)) : 0.;
	}

protected:
	double	mRound;
	int		mShift;	// how far to shift a 32 bit value right
};

// ____________________________________________________________________________
//
// TFloatToIntBlitter
// only used for: 8-bit, low-aligned, or non-PPC
template <class FloatType, class IntType>
class TFloatToIntBlitter : public FloatToIntBlitter {
public:
	typedef typename FloatType::value_type float_val;
	typedef typename IntType::value_type int_val;

	TFloatToIntBlitter(int bitDepth) : FloatToIntBlitter(bitDepth) { }

	virtual void	Convert(const void *vsrc, void *vdest, unsigned int nSamples)
	{
		const float_val *src = (const float_val *)vsrc;
		int_val *dest = (int_val *)vdest;
		double maxInt32 = 2147483648.0;	// 1 << 31
		double round = mRound;
		double max32 = maxInt32 - 1.0 - round;
		double min32 = -2147483648.0;
		int shift = mShift, count;
		double f1, f2, f3, f4;
		int i1, i2, i3, i4;

		SET_ROUNDMODE
		
		if (nSamples >= 8) {
			f1 = FloatType::load(src + 0);
			
			f2 = FloatType::load(src + 1);
			f1 = f1 * maxInt32 + round;
			
			f3 = FloatType::load(src + 2);
			f2 = f2 * maxInt32 + round;
			i1 = FloatToInt(f1, min32, max32);
			
			src += 3;
			
			nSamples -= 4;
			count = nSamples >> 2;
			nSamples &= 3;
			
			while (count--) {
				f4 = FloatType::load(src + 0);
				f3 = f3 * maxInt32 + round;
				i2 = FloatToInt(f2, min32, max32);
				IntType::store(dest + 0, i1 >> shift);
	
				f1 = FloatType::load(src + 1);
				f4 = f4 * maxInt32 + round;
				i3 = FloatToInt(f3, min32, max32);
				IntType::store(dest + 1, i2 >> shift);
	
				f2 = FloatType::load(src + 2);
				f1 = f1 * maxInt32 + round;
				i4 = FloatToInt(f4, min32, max32);
				IntType::store(dest + 2, i3 >> shift);
				
				f3 = FloatType::load(src + 3);
				f2 = f2 * maxInt32 + round;
				i1 = FloatToInt(f1, min32, max32);
				IntType::store(dest + 3, i4 >> shift);
				
				src += 4;
				dest += 4;
			}
			
			f4 = FloatType::load(src);
			f3 = f3 * maxInt32 + round;
			i2 = FloatToInt(f2, min32, max32);
			IntType::store(dest + 0, i1 >> shift);
		
			f4 = f4 * maxInt32 + round;
			i3 = FloatToInt(f3, min32, max32);
			IntType::store(dest + 1, i2 >> shift);

			i4 = FloatToInt(f4, min32, max32);
			IntType::store(dest + 2, i3 >> shift);

			IntType::store(dest + 3, i4 >> shift);
			
			src += 1;
			dest += 4;
		}

		count = nSamples;
		while (count--) {
			f1 = FloatType::load(src) * maxInt32 + round;
			i1 = FloatToInt(f1, min32, max32) >> shift;
			IntType::store(dest, i1);
			src += 1;
			dest += 1;
		}
		RESTORE_ROUNDMODE
	}
};

// ____________________________________________________________________________
//
// IntToFloatBlitter
class IntToFloatBlitter : public PCMBlitter {
public:
	IntToFloatBlitter(int bitDepth) :
		mBitDepth(bitDepth)
	{
		mScale = static_cast<Float32>(1.0 / float(1UL << (bitDepth - 1)));
	}
	
	Float32		mScale;
	UInt32		mBitDepth;
};

// ____________________________________________________________________________
//
// TIntToFloatBlitter - only used for non-PPC
// On PPC these are roughly only half as fast as the optimized versions
template <class IntType, class FloatType>
class TIntToFloatBlitter : public IntToFloatBlitter {
public:
	typedef typename FloatType::value_type float_val;
	typedef typename IntType::value_type int_val;

	TIntToFloatBlitter(int bitDepth) : IntToFloatBlitter(bitDepth) { }

	virtual void	Convert(const void *vsrc, void *vdest, unsigned int nSamples)
	{
		const int_val *src = (const int_val *)vsrc;
		float_val *dest = (float_val *)vdest;
		int count = nSamples;
		Float32 scale = mScale;
		int_val i0, i1, i2, i3;
		float_val f0, f1, f2, f3;
		
		/*
			$i = IntType::load(src); ++src;
			$f = $i;
			$f *= scale;
			FloatType::store(dest, $f); ++dest;
		*/

		if (count >= 4) {
			// Cycle 1
			i0 = IntType::load(src); ++src;

			// Cycle 2
			i1 = IntType::load(src); ++src;
			f0 = i0;

			// Cycle 3
			i2 = IntType::load(src); ++src;
			f1 = i1;
			f0 *= scale;

			// Cycle 4
			i3 = IntType::load(src); ++src;
			f2 = i2;
			f1 *= scale;
			FloatType::store(dest, f0); ++dest;

			count -= 4;
			int loopCount = count / 4;
			count -= 4 * loopCount;

			while (loopCount--) {
				// Cycle A
				i0 = IntType::load(src); ++src;
				f3 = i3;
				f2 *= scale;
				FloatType::store(dest, f1); ++dest;

				// Cycle B
				i1 = IntType::load(src); ++src;
				f0 = i0;
				f3 *= scale;
				FloatType::store(dest, f2); ++dest;

				// Cycle C
				i2 = IntType::load(src); ++src;
				f1 = i1;
				f0 *= scale;
				FloatType::store(dest, f3); ++dest;

				// Cycle D
				i3 = IntType::load(src); ++src;
				f2 = i2;
				f1 *= scale;
				FloatType::store(dest, f0); ++dest;
			}

			// Cycle 3
			f3 = i3;
			f2 *= scale;
			FloatType::store(dest, f1); ++dest;

			// Cycle 2
			f3 *= scale;
			FloatType::store(dest, f2); ++dest;

			// Cycle 1
			FloatType::store(dest, f3); ++dest;
		}

		while (count--) {
			i0 = IntType::load(src); ++src;
			f0 = i0;
			f0 *= scale;
			FloatType::store(dest, f0); ++dest;
		}
	}
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark -
#pragma mark X86 SSE2
// ____________________________________________________________________________________
// X86 SSE2
void	NativeInt16ToFloat32_X86(const SInt16 *src, Float32 *dest, unsigned int count);
void	SwapInt16ToFloat32_X86(const SInt16 *src, Float32 *dest, unsigned int count);
void	Float32ToNativeInt16_X86(const Float32 *src, SInt16 *dest, unsigned int count);
void	Float32ToSwapInt16_X86(const Float32 *src, SInt16 *dest, unsigned int count);

void	Float32ToNativeInt24_X86(const Float32 *src, UInt8 *dst, unsigned int numToConvert);
void	Float32ToSwapInt24_X86(const Float32 *src, UInt8 *dst, unsigned int numToConvert);

void	NativeInt32ToFloat32_X86(const SInt32 *src, Float32 *dest, unsigned int count);
void	SwapInt32ToFloat32_X86(const SInt32 *src, Float32 *dest, unsigned int count);
void	Float32ToNativeInt32_X86(const Float32 *src, SInt32 *dest, unsigned int count);
void	Float32ToSwapInt32_X86(const Float32 *src, SInt32 *dest, unsigned int count);

#pragma mark -
#pragma mark Portable
// ____________________________________________________________________________________
// Portable
void	Float32ToNativeInt16_Portable(const Float32 *src, SInt16 *dest, unsigned int count);
void	Float32ToSwapInt16_Portable(const Float32 *src, SInt16 *dest, unsigned int count);
void	NativeInt16ToFloat32_Portable(const SInt16 *src, Float32 *dest, unsigned int count);
void	SwapInt16ToFloat32_Portable(const SInt16 *src, Float32 *dest, unsigned int count);

void	Float32ToNativeInt24_Portable(const Float32 *src, UInt8 *dest, unsigned int count);
void	Float32ToSwapInt24_Portable(const Float32 *src, UInt8 *dest, unsigned int count);
void	NativeInt24ToFloat32_Portable(const UInt8 *src, Float32 *dest, unsigned int count);
void	SwapInt24ToFloat32_Portable(const UInt8 *src, Float32 *dest, unsigned int count);

void	Float32ToNativeInt32_Portable(const Float32 *src, SInt32 *dest, unsigned int count);
void	Float32ToSwapInt32_Portable(const Float32 *src, SInt32 *dest, unsigned int count);
void	NativeInt32ToFloat32_Portable(const SInt32 *src, Float32 *dest, unsigned int count);
void	SwapInt32ToFloat32_Portable(const SInt32 *src, Float32 *dest, unsigned int count);

#ifdef __cplusplus
};
#endif

#endif // __PCMBlitterLib_h__
