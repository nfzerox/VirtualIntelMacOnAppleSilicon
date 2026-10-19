#include "License.h"
//#ifndef USE_APPLE_ROUTINES
//#include "AppleAudioClip.h"
//#include "AppleAudioCommon.h"
#include "VoodooHDADevice.h"
#include "VoodooHDAEngine.h"
#include "PCMBlitterLibDispatch.h"

#define errorMsg(fmt, args...)	do { if (mDevice) mDevice->errorMsg(fmt, ##args); } while (0)

IOReturn VoodooHDAEngine::clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame,
		UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat,
		__unused IOAudioStream *audioStream)
{
	if(!streamFormat)
	{
        return kIOReturnBadArgument;
    }
	UInt32 firstSample = firstSampleFrame * streamFormat->fNumChannels;
	UInt32 numSamples = numSampleFrames * streamFormat->fNumChannels;
	Float32 *floatMixBuf = ((Float32*)mixBuf) + firstSample;
	
	UInt8 *sourceBuf = (UInt8 *) sampleBuf;

	// figure out what sort of blit we need to do
	if ((streamFormat->fSampleFormat == kIOAudioStreamSampleFormatLinearPCM) && streamFormat->fIsMixable) {
		// it's mixable linear PCM, which means we will be calling a blitter, which works in samples
		// not frames

		if (streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationSignedInt) {
			// it's some kind of signed integer, which we handle as some kind of even byte length
			bool nativeEndianInts;
			nativeEndianInts = (streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian);

			switch (streamFormat->fBitWidth) {
			case 8:
				if (nativeEndianInts)
				{
					SInt8* theOutputBufferSInt8 = ((SInt8*)sampleBuf) + firstSample;
					ClipFloat32ToSInt8_4(floatMixBuf, theOutputBufferSInt8, numSamples);
				} else
					Float32ToInt8(theMixBuffer, theOutputBufferSInt8, theNumberSamples);
					break;
					
			case 16:
				SInt16* theOutputBufferSInt16 = ((SInt16*)sampleBuf) + firstSample;
				if (nativeEndianInts) {
					if (vectorize) {
						Float32ToNativeInt16(floatMixBuf, theOutputBufferSInt16, numSamples);
					} else {
						ClipFloat32ToSInt16LE_4(floatMixBuf, theOutputBufferSInt16, numSamples);
					}
				}
				else
					Float32ToSwapInt16(floatMixBuf, (SInt16 *) &sourceBuf[2 * firstSample],
							numSamples);
				break;
					
			case 20:
			case 24:
				SInt32* theOutputBufferSInt24 = (SInt32*)(((UInt8*)sampleBuf) + (firstSample * 3));
				if (nativeEndianInts) {
					if (vectorize) {
						Float32ToNativeInt24(floatMixBuf, theOutputBufferSInt24, numSamples);
					} else {
						ClipFloat32ToSInt24LE_4(floatMixBuf, theOutputBufferSInt24, numSamples);						
					}
				}
				else
					Float32ToSwapInt24(floatMixBuf, &sourceBuf[3 * firstSample], numSamples);
				break;

			case 32:
				SInt32* theOutputBufferSInt32 = ((SInt32*)sampleBuf) + firstSample;
				if (nativeEndianInts) {
					if (vectorize) {
						Float32ToNativeInt32(floatMixBuf, theOutputBufferSInt32, numSamples);
					} else {					
						ClipFloat32ToSInt32LE_4(floatMixBuf, theOutputBufferSInt32, theNumberSamples);
					}
				}
				else
					Float32ToSwapInt32(floatMixBuf, (SInt32 *) &sourceBuf[4 * firstSample],
							numSamples);
				break;

			default:
				errorMsg("clipOutputSamples: can't handle signed integers with a bit width of %d",
						streamFormat->fBitWidth);
				break;

			}
		} else if (streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationIEEE754Float) {
			// it is some kind of floating point format
			if ((streamFormat->fBitWidth == 32) && (streamFormat->fBitDepth == 32) &&
					(streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian)) {
				// it's Float32, so we are just going to copy the data
				memcpy(&((Float32 *) sampleBuf)[firstSample], &floatMixBuf[firstSample],
						numSamples * sizeof (Float32));
			} else
				errorMsg("clipOutputSamples: can't handle floats with a bit width of %d, bit depth of %d, "
						"and/or the given byte order", streamFormat->fBitWidth, streamFormat->fBitDepth);
		}
	} else {
		// it's not linear PCM or it's not mixable, so just copy the data into the target buffer
		UInt32 offset = firstSampleFrame * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		UInt32 size = numSampleFrames * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		memcpy(&((SInt8 *) sampleBuf)[offset], &((SInt8 *) mixBuf)[offset], size);
	}

	return kIOReturnSuccess;
}

IOReturn VoodooHDAEngine::convertInputSamples(const void *sampleBuf, void *destBuf,
		UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat,
		__unused IOAudioStream *audioStream)
{
	UInt32	numSamplesLeft, numSamples;
	Float32 	*floatDestBuf;
	
//    floatDestBuf = (float *)destBuf;
	UInt32 firstSample = firstSampleFrame * streamFormat->fNumChannels;
	numSamples = numSamplesLeft = numSampleFrames * streamFormat->fNumChannels;
	long int noiseMask = ~((1 << noiseLevel) - 1);
	
	UInt8 *sourceBuf = (UInt8 *) sampleBuf; 

	// figure out what sort of blit we need to do
	if ((streamFormat->fSampleFormat == kIOAudioStreamSampleFormatLinearPCM) && streamFormat->fIsMixable) {
		// it's linear PCM, which means the target is Float32 and we will be calling a blitter, which
		// works in samples not frames
		floatDestBuf = (Float32 *) destBuf;

		if (streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationSignedInt) {
			// it's some kind of signed integer, which we handle as some kind of even byte length
			bool nativeEndianInts;
			nativeEndianInts = (streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian);

			switch (streamFormat->fBitWidth) {
				case 8:
					SInt8 *inputBuf8;
					
					inputBuf8 = &(((SInt8 *)sampleBuf)[firstSample]);
#if defined(__ppc__)
					Int8ToFloat32(inputBuf8, floatDestBuf, numSamplesLeft);
#elif defined(__i386__) || defined(__x86_64__)
					while (numSamplesLeft-- > 0) 
					{	
						*(floatDestBuf++) = (float)(*(inputBuf8++) &= (SInt8)noiseMask) * kOneOverMaxSInt8Value;
					}
#endif
					
					break;
				case 16:
				if (nativeEndianInts)
					if (vectorize) {
						NativeInt16ToFloat32((SInt16 *) &sampleBuf[2 * firstSample], floatDestBuf, numSamples);
					} else {
						SInt16 *inputBuf16;
						
						inputBuf16 = &(((SInt16 *)sampleBuf)[firstSample]);						
#if defined(__ppc__)
						SwapInt16ToFloat32(inputBuf16, floatDestBuf, numSamplesLeft, 16);
#elif defined(__i386__) || defined(__x86_64__)
						while (numSamplesLeft-- > 0) 
						{	
							*(floatDestBuf++) = (float)(*(inputBuf16++) &= (SInt16)noiseMask) * kOneOverMaxSInt16Value;
						}
#endif
					}

					
				else
					SwapInt16ToFloat32((SInt16 *) &sampleBuf[2 * firstSample], floatDestBuf, numSamples);
				break;

			case 20:
			case 24:
				if (nativeEndianInts)
					if (vectorize) {
						NativeInt24ToFloat32(&sourceBuf[3 * firstSample], floatDestBuf, numSamples);
					} else {
						register SInt8 *inputBuf24;
						
						// Multiply by 3 because 20 and 24 bit samples are packed into only three bytes, so we have to index bytes, not shorts or longs
						inputBuf24 = &(((SInt8 *)sampleBuf)[firstSample * 3]);
						
#if defined(__ppc__)
						SwapInt24ToFloat32((long *)inputBuf24, floatDestBuf, numSamplesLeft, 24);
#elif defined(__i386__) || defined(__x86_64__)
						register SInt32 inputSample;
						
						// [rdar://4311684] - Fixed 24-bit input convert routine. /thw
						while (numSamplesLeft-- > 1) 
						{	
							inputSample = (* (UInt32 *)inputBuf24) & 0x00FFFFFF & noiseMask;
							// Sign extend if necessary
							if (inputSample > 0x7FFFFF)
							{
								inputSample |= 0xFF000000;
							}
							inputBuf24 += 3;
							*(floatDestBuf++) = (float)inputSample * kOneOverMaxSInt24Value;
						}
						// Convert last sample. The following line does the same work as above without going over the edge of the buffer.
						inputSample = SInt32 ((UInt32 (*(UInt16 *) inputBuf24) & 0x0000FFFF & noiseMask)
											  | (SInt32 (*(inputBuf24 + 2)) << 16));
						*(floatDestBuf++) = (float)inputSample * kOneOverMaxSInt24Value;
#endif
						
					}

					
				else
					SwapInt24ToFloat32(&sourceBuf[3 * firstSample], floatDestBuf, numSamples);
				break;

			case 32:
				if (nativeEndianInts) {
					if (vectorize) {
						NativeInt32ToFloat32((SInt32 *) &sourceBuf[4 * firstSample], floatDestBuf, numSamples);
					} else {
						register SInt32 *inputBuf32;
						inputBuf32 = &(((SInt32 *)sampleBuf)[firstSample]);
						
#if defined(__ppc__)
						SwapInt32ToFloat32(inputBuf32, floatDestBuf, numSamplesLeft, 32);
#elif defined(__i386__) || defined(__x86_64__)
						while (numSamplesLeft-- > 0) {	
							*(floatDestBuf++) = (float)(*(inputBuf32++) & noiseMask) * kOneOverMaxSInt32Value;
						}
#endif
						
					}
				}
				else
					SwapInt32ToFloat32((SInt32 *) &sourceBuf[4 * firstSample], floatDestBuf, numSamples);
				break;

			default:
				errorMsg("convertInputSamples: can't handle signed integers with a bit width of %d",
						streamFormat->fBitWidth);
				break;

			}
			
			//Меняю местами значения для левого и правого канала
			if(mDevice && mDevice->mSwitchCh && (streamFormat->fNumChannels > 1)) {
				UInt32 i;
				Float32 tempSamples;
				
				for(i = 0; i < numSamples; i+= streamFormat->fNumChannels) {
					tempSamples = floatDestBuf[i];
					floatDestBuf[i] = floatDestBuf[i+1];
					floatDestBuf[i+1] = tempSamples;
				}
			}
			
		} else if (streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationIEEE754Float) {
			// it is some kind of floating point format
			if ((streamFormat->fBitWidth == 32) && (streamFormat->fBitDepth == 32) &&
					(streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian)) {
				// it's Float32, so we are just going to copy the data
				memcpy(floatDestBuf, &((Float32 *) sampleBuf)[firstSample], numSamples * sizeof (Float32));
			} else
				errorMsg("convertInputSamples: can't handle floats with a bit width of %d, bit depth of %d, "
						"and/or the given byte order", streamFormat->fBitWidth, streamFormat->fBitDepth);
		}
	} else {
		// it's not linear PCM or it's not mixable, so just copy the data into the target buffer
		UInt32 offset = firstSampleFrame * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		UInt32 size = numSampleFrames * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		memcpy(destBuf, &sourceBuf[offset], size);
	}

	return kIOReturnSuccess;
}
//#endif
