#include "License.h"

#ifndef __PCMBlitterLibDispatch_h__
#define __PCMBlitterLibDispatch_h__

#include "PCMBlitterLib.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
	PCM int<->float library.

	These are the high-level interfaces which dispatch to (often processor-specific) optimized routines.
	Avoid calling the lower-level routines directly; they are subject to renaming etc.
	
	There are two sets of interfaces:
	[1] integer formats are either "native" or "swap"
	[2] integer formats are "BE" or "LE", signifying big or little endian. These are simply macros for the other functions.
	
	All floating point numbers are 32-bit native-endian.
	Supports 16, 24, and 32-bit integers, big and little endian.
	
	32-bit floats and ints must be 4-byte aligned.
	24-bit samples have no alignment requirements.
	16-bit ints must be 2-byte aligned.
	
	On Intel, some implementations assume SSE2.
*/

inline void NativeInt16ToFloat32(const SInt16 *src, Float32 *dest, unsigned int count)
{
	NativeInt16ToFloat32_X86(src, dest, count);
}

inline void SwapInt16ToFloat32(const SInt16 *src, Float32 *dest, unsigned int count)
{
	SwapInt16ToFloat32_X86(src, dest, count);
}

inline void NativeInt24ToFloat32(const UInt8 *src, Float32 *dest, unsigned int count)
{
	NativeInt24ToFloat32_Portable(src, dest, count);
}

inline void SwapInt24ToFloat32(const UInt8 *src, Float32 *dest, unsigned int count)
{
	SwapInt24ToFloat32_Portable(src, dest, count);
}

inline void NativeInt32ToFloat32(const SInt32 *src, Float32 *dest, unsigned int count)
{
	NativeInt32ToFloat32_X86(src, dest, count);
}

inline void SwapInt32ToFloat32(const SInt32 *src, Float32 *dest, unsigned int count)
{
	SwapInt32ToFloat32_X86(src, dest, count);
}


inline void Float32ToNativeInt16(const Float32 *src, SInt16 *dest, unsigned int count)
{
	Float32ToNativeInt16_X86(src, dest, count);
}

inline void Float32ToSwapInt16(const Float32 *src, SInt16 *dest, unsigned int count)
{
	Float32ToSwapInt16_X86(src, dest, count);
}

inline void Float32ToNativeInt24(const Float32 *src, UInt8 *dest, unsigned int count)
{
	Float32ToNativeInt24_X86(src, dest, count);
}

inline void Float32ToSwapInt24(const Float32 *src, UInt8 *dest, unsigned int count)
{
	Float32ToSwapInt24_Portable(src, dest, count);
}

inline void Float32ToNativeInt32(const Float32 *src, SInt32 *dest, unsigned int count)
{
	Float32ToNativeInt32_X86(src, dest, count);
}

inline void Float32ToSwapInt32(const Float32 *src, SInt32 *dest, unsigned int count)
{
	Float32ToSwapInt32_X86(src, dest, count);
}

// Alternate names for the above: these explicitly specify the endianism of the integer format instead of "native"/"swap"
#pragma mark -
#pragma mark Alternate names

#define	LEInt16ToFloat32	NativeInt16ToFloat32
#define	BEInt16ToFloat32	SwapInt16ToFloat32
#define	LEInt24ToFloat32	NativeInt24ToFloat32
#define	BEInt24ToFloat32	SwapInt24ToFloat32
#define	LEInt32ToFloat32	NativeInt32ToFloat32
#define	BEInt32ToFloat32	SwapInt32ToFloat32

#define Float32ToLEInt16	Float32ToNativeInt16
#define Float32ToBEInt16	Float32ToSwapInt16
#define Float32ToLEInt24	Float32ToNativeInt24
#define Float32ToBEInt24	Float32ToSwapInt24
#define Float32ToLEInt32	Float32ToNativeInt32
#define Float32ToBEInt32	Float32ToSwapInt32

#ifdef __cplusplus
};
#endif

#endif // __PCMBlitterLibDispatch_h__
