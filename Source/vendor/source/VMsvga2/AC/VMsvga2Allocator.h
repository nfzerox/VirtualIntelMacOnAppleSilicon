/*
 *  VMsvga2Allocator.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 28th 2009.
 *  Copyright 1999 A.G.McDowell. All rights reserved.
 *
 *  This Allocator was written by A.G.McDowell.  The original code can be found at
 *    http://www.mcdowella.demon.co.uk/buddy.html
 *
 */

/* A.G.McDowell, 2 May 1999
 * This comes WITHOUT WARRANTY - it's really just demo code. You		[Awww...come on, don't be modest]
 * can find a PROPER memory allocator, by Doug Lea, at
 * http://g.oswego.edu
 * There is a paper pointing out why it is very good and this
 * (the buddy block system) wastes memory in comparison
 * at http://www.cs.utexas.edu/usrs/oops/papers.html -
 * "The Memory Fragmentation Problem: solved?"
 * by Wilson and Johnstone
 * Thanks to Niall Murphy - http://www.iol.ie/~nmurphy for these
 * references
 */

/* These routines manage a pool of free storage, using the Buddy
 system. See "The Art of Computer Programming", by D.E.Knuth, Vol. 1,
 section 2.5, Algorithm R, and Exercise 29 of that section.
 
 We manage a pool of storage of some fixed length and starting point,
 using an auxiliary bitmap, and some small space for linked lists. We
 only ever deal with block of free and allocated storage of size
 exactly 2**k, for some integer k in a pre-determined range. Each block
 starts at an address which is a multiple of its blocksize. When a
 block is allocated, all of it may be used. The auxiliary bitmap needs
 one bit for each possible block of the smallest size. The minimum
 block size must be large enough to cover at least FREEBLOCK ints
 
 The worst case time taken to allocate a block grows only with the
 size of the block required plus the
 number of different possible blocksizes, which is constant for all
 practical purposes (certainly less than 64, since each size is a power
 of two). The worst case time taken to free a block is the same.
 
 If this allocator fails, there is no way to place a block of the size
 requested within the pool starting at an address that is a multiple of
 its blocksize without overlapping some already allocated block.
 */

/*
 * Note: rest of the comments from pool.h snipped due to excessive verbosity
 */

#ifndef __VMSVGA2ALLOCATOR_H__
#define __VMSVGA2ALLOCATOR_H__

#include <libkern/c++/OSObject.h>
#include <libkern/OSTypes.h>
#include <IOKit/IOReturn.h>

class VMsvga2Allocator : public OSObject
{
	OSDeclareDefaultStructors(VMsvga2Allocator);

private:
	typedef uint32_t pool_size_t;

	uint8_t* poolStart;		// First byte in the pool
	pool_size_t poolBlocks;

	int minBits;			// Minimum block size is 1 << minBits (expect 12 = log_2(PAGE_SIZE))
	int numSizes;			// sizes go up by powers of two (expect 13, max block == 2^24 bytes == SVGA_FB_MAX_TRACEABLE_SIZE)
	pool_size_t freeList[13];	// free lists
	uint8_t* map;			// bit map
	pool_size_t freeBytes;

	static bool memAll(void const *p, size_t bytes);
	bool testAll(size_t firstBit, size_t pastBit);
	void clearAll(size_t firstBit, size_t pastBit);
	IOReturn clearCheck(size_t firstBit, size_t pastBit);
	static bool memAny(void const *p, size_t bytes);
	bool testAny(size_t firstBit, size_t pastBit);
	void makeFree(pool_size_t firstFree, int bitsFree, bool zap);
	void toFree(pool_size_t firstBlock, pool_size_t pastBlock, bool zap);
	IOReturn BuddyMalloc(int bits, void **newStore);
	IOReturn BuddyAllocSize(void const *sss, int *numBits);
	void ReleaseMap();

public:
	/*
	 * Methods overridden from superclass
	 */
	bool init();
	void free();
	static VMsvga2Allocator* factory();

	/*
	 * Allocator's Methods
	 */
	IOReturn Init(void* startAddress, size_t bytes);
	IOReturn Rebase(void* newStartAddress);
	IOReturn Release(size_t startOffsetBytes, size_t endOffsetBytes);
	IOReturn Malloc(size_t bytes, void** newStore);
	IOReturn Realloc(void* ptrv, size_t size, void** newPtr);
	IOReturn Free(void* storage2);
	IOReturn Available(size_t* bytesFree);
	IOReturn Check(size_t* counts);
};

#endif /* __VMSVGA2ALLOCATOR_H__ */
