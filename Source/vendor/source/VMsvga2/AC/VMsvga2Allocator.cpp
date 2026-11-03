/*
 *  VMsvga2Allocator.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 28th 2009.
 *  Copyright 1999 A.G.McDowell. All rights reserved.
 *
 *  This Allocator was written by A.G.McDowell.  The original code can be found at
 *    http://www.mcdowella.demon.co.uk/buddy.html
 *
 */

/* A.G.McDowell,  2 May 1999
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

#include <IOKit/IOLib.h>
#include "VMsvga2Allocator.h"

#define CLASS VMsvga2Allocator
#define super OSObject
OSDefineMetaClassAndStructors(VMsvga2Allocator, OSObject);

#define HIDDEN __attribute__((visibility("hidden")))

/*
 * Size of header + trailer in a free block, in ints
 */
#define HEADER_LEN 3
#define TRAILER_LEN 1
#define FREEBLOCK (HEADER_LEN + TRAILER_LEN)

/* define FRAGILE if you want more speed. This means that the worst				[We do, and it's defined]
 * case cost of Malloc() and Free() grows only with the number of
 * different block sizes, not the size of each block. The flip side
 * of this is less error checking to pick up store corruption from
 * bugs here or in user code. The worst case dependency of Realloc()
 * is inevitable - it may have to copy the old data
 */

static __attribute__((used)) char const version[] = "A.G.McDowell 19990501" ;

/* Bit macros, since it should be both faster and more compact to use
 * macros than functions for this
 */
#define BITTEST(p, n) ((p)->map[(n) >> 3] & (128U >> ((n) & 7)))
#define BITSET(p, n) {(p)->map[(n) >> 3] |= (128U >> ((n) & 7));}
#define BITCLEAR(p, n) {(p)->map[(n) >> 3] &= ~(128U >> ((n) & 7));}

/* The top of a free block contains HEADER_LEN pool_size_ts. We could
 * use either pointers or block offsets. We choose block offsets
 * to make it
 * easy to run this in shared memory, where different processes
 * may see the memory at different addresses. Despite this, we don't
 * attempt to handle locking or recovering from processes that die
 * leaving shared memory inconsistent
 */

/*
 * use OURNULL in our own lists.
 */
#define OURNULL static_cast<pool_size_t>(-1)
/* magic number. The first block in a series
 * of free blocks starts with this value. The
 * later blocks start with zero. Any non-zero
 * value would do, but picking something unlikely
 * helps us pick up corruption earlier
 */
#define MAGIC static_cast<pool_size_t>(0xA7130423U)
/*
 * offset of magic number in a free block
 */
#define OFF_MAGIC 0
/*
 * offset of pointer to prev free block
 */
#define OFF_PREV 1
/*
 * offset of pointer to next free block
 */
#define OFF_NEXT 2
/*
 * trailer.  1 << trailer value is # of blocks in this chunk
 */
#define OFF_BITS (-1)
/*
 * offset on block count to make zero an unlikely value
 */
#define LEN_OFFSET 1234

#define POOL_ONE static_cast<pool_size_t>(1U)

#pragma mark -
#pragma mark Private Methods
#pragma mark -

/*
 * checks if all bytes in a range are 0xFFU
 */
HIDDEN
bool CLASS::memAll(void const *p, size_t bytes)
{
	if (!p || !bytes)
		return true;

	__asm__ volatile ("cld; repe scasb" : "+D"(p), "+c"(bytes) : "a"(static_cast<uint8_t>(0xFFU)));
	return !bytes;
}

/*
 * Checks if all bits in a range are 1
 */
HIDDEN
bool CLASS::testAll(size_t firstBit, size_t pastBit)
{
	size_t firstFull = (firstBit + 7U) & ~7UL;
	size_t pastFull = pastBit & ~7UL;
	size_t past = pastBit;
	size_t i, bytes;

	if (past > firstFull)
		past = firstFull;
	for (i = firstBit; i < past; ++i)
		if (!BITTEST(this, i))
			return false;
	if (pastFull > firstFull) {
		bytes = (pastFull - firstFull) >> 3;
		if (!memAll(map + (firstFull >> 3), bytes))
			return false;
	}
	if (pastFull < past)
		pastFull = past;
	for (i = pastFull; i < pastBit; ++i)
		if (!BITTEST(this, i))
			return false;
	return true;
}

/*
 * Clears a range of bits to 0
 */
HIDDEN
void CLASS::clearAll(size_t firstBit, size_t pastBit)
{
	size_t firstFull = (firstBit + 7U) & ~7UL;
	size_t pastFull = pastBit & ~7UL;
	size_t past = pastBit;
	size_t i, bytes;

	if (past > firstFull)
		past = firstFull;
	for (i = firstBit; i < past; ++i)
		BITCLEAR(this, i);
	if (pastFull > firstFull) {
		bytes = (pastFull - firstFull) >> 3;
		bzero(map + (firstFull >> 3), bytes);
	}
	if (pastFull < past)
		pastFull = past;
	for (i = pastFull; i < pastBit; ++i)
		BITCLEAR(this, i);
}

/*
 * Checks if all bits in a range are 1 and clears them if so
 */
HIDDEN
IOReturn CLASS::clearCheck(size_t firstBit, size_t pastBit)
{
	if (!testAll(firstBit, pastBit))
		return kIOReturnBadMedia /* "bit already clear" */;
	clearAll(firstBit, pastBit);
	return kIOReturnSuccess;
}

/*
 * checks if any bytes in range are non-zero
 */
HIDDEN
bool CLASS::memAny(void const *p, size_t bytes)
{
	if (!p || !bytes)
		return false;

	__asm__ volatile ("cld; repe scasb" : "+D"(p), "+c"(bytes) : "a"(static_cast<uint8_t>(0U)));
	return bytes != 0U;
}

/*
 * Checks if any bits in a range are 1
 */
HIDDEN
bool CLASS::testAny(size_t firstBit, size_t pastBit)
{
	size_t firstFull = (firstBit + 7U) & ~7UL;
	size_t pastFull = pastBit & ~7UL;
	size_t past = pastBit;
	size_t i, bytes;

	if (past > firstFull)
		past = firstFull;
	for (i = firstBit; i < past; ++i)
		if (BITTEST(this, i))
			return true;
	if (pastFull > firstFull) {
		bytes = (pastFull - firstFull) >> 3;
		if (memAny(map + (firstFull >> 3), bytes))
			return true;
	}
	if (pastFull < past)
		pastFull = past;
	for (i = pastFull; i < pastBit; i++)
		if (BITTEST(this, i))
			return true;
	return false;
}

/*
 * Adds a single aligned range to a single free list
 */
HIDDEN
void CLASS::makeFree(pool_size_t firstFree, int bitsFree, bool zap)
{
	pool_size_t *ptr, *pastPtr, *nextPtr, nextOff;
	int byteBits = bitsFree + minBits;
	pool_size_t bytesToFree = POOL_ONE << byteBits;
	pool_size_t blocksToFree = POOL_ONE << bitsFree;
	ptr = reinterpret_cast<pool_size_t*>(poolStart + (firstFree << minBits));
	if (zap) {
		/*
		 * Zap free area and bitmap to zero
		 * Once the area has been initialised and released, it is
		 * never necessary to zap out ALL of an area - either free
		 * store or bitmap. You will always know that correctly
		 * functioning code either means all but a tiny part of
		 * this area is already clear (the bitmap), or will
		 * ever be accessed (only tail probed by
		 * poolFree() in memory). But for the moment it makes it
		 * easier to see this code is correct and easier to check
		 * for corruption with poolCheck() if we zap everything.
		 * The cost is always kept no more than linear in the
		 * amount of core the users is allocing or freeing anyway
		 *
		 * In fact, under FRAGILE we don't change this code,
		 * just change the zap argument
		 */
		bzero(ptr, bytesToFree);
		clearAll(firstFree, firstFree + blocksToFree);
	}
	/*
	 * build up our own area and link in to free list
	 */
	ptr[OFF_MAGIC] = MAGIC;
	ptr[OFF_PREV] = OURNULL;
	nextOff = freeList[bitsFree];
	ptr[OFF_NEXT] = nextOff;
	pastPtr = ptr + bytesToFree / sizeof *ptr;
	pastPtr[OFF_BITS] = static_cast<pool_size_t>(bitsFree + LEN_OFFSET);
	if (nextOff != OURNULL) {
		nextPtr = reinterpret_cast<pool_size_t*>(poolStart + (nextOff << minBits));
		nextPtr[OFF_PREV] = firstFree;
	}
	freeList[bitsFree] = firstFree;
	freeBytes += bytesToFree;
}

/*
 * Add an arbitrary range of blocks to free lists
 */
HIDDEN
void CLASS::toFree(pool_size_t firstBlock, pool_size_t pastBlock, bool zap)
{
	pool_size_t i, nextLen, tryLen = 1U;
	int tryBits = 0;
	for(i = firstBlock; i < pastBlock;) {
		/*
		 * To avoid cost quadratic in the number of different
		 * blocks produced from this chunk of store, we have to
		 * use the size of the previous block produced from this
		 * chunk as the starting point to work out the size of the
		 * next block we can produce. If you look at the binary
		 * representation of the starting points of the blocks 
		 * produced, you can see that you first of all increase the 
		 * size of the blocks produced up to some maximum as the
		 * address dealt with gets offsets added on which zap out
		 * low order bits, then decrease as the low order bits of the
		 * final block produced get added in. E.g. as you go from
		 * 001 to 0111 you generate blocks
		 * of size 001 at 001 taking you to 010
		 * of size 010 at 010 taking you to 100
		 * of size 010 at 100 taking you to 110
		 * of size 001 at 110 taking you to 111
		 * So the maximum total cost of the loops below this comment
		 * is one trip from the lowest blocksize to the highest and
		 * back again.
		 */
		for(;tryBits < numSizes - 1; ++tryBits) {
			nextLen = tryLen << 1;
			if(i + nextLen > pastBlock)
				/*
				 * off end of chunk to be freed
				 */
				break;
			if (i & (nextLen - 1U))
				/*
				 * block would not be on boundary
				 */
				break;
			tryLen = nextLen;
		}
		for(;tryBits >= 0; --tryBits, tryLen >>= 1) {
			if (i + tryLen > pastBlock)
				/*
				 * off end of chunk to be freed
				 */
				continue;
			if (i & (tryLen - 1U))
				/*
				 * block would not be on boundary
				 */
				continue;
			/*
			 * OK
			 */
			break;
		}
		if (tryBits < 0)
			break;
		makeFree(i, tryBits, zap);
		i += tryLen;
	}
}

/*
 * Allocates a block of size 2^(bits + minBits)
 */
HIDDEN
IOReturn CLASS::BuddyMalloc(int bits, void **newStore)
{
	int retBits;
	pool_size_t *ptr, *pastPtr, offset, nextOffset, *nextPtr, past;
	if (!newStore)
		return kIOReturnBadArgument /* "null pointer to new store" */;
	if (freeBytes < (POOL_ONE << (bits + minBits)))
		return kIOReturnNoMemory;
    /*
	 * Find a list with blocks big enough on it
	 */
	for (retBits = bits; retBits < numSizes; ++retBits)
		if (freeList[retBits] != OURNULL)
			break;
	if (retBits >= numSizes)
		return kIOReturnNoMemory;
	offset = freeList[retBits];
	if (offset >= poolBlocks)
		return kIOReturnInternalError /* "corrupted free list" */;
	ptr = reinterpret_cast<pool_size_t*>(poolStart + (offset << minBits));
	if (ptr[OFF_MAGIC] != MAGIC)
		return kIOReturnInternalError /* "corrupted free area" */;
	if (ptr[OFF_PREV] != OURNULL)
		return kIOReturnInternalError /* "corrupted free area" */;
	pastPtr = ptr + (POOL_ONE << (retBits + minBits)) / sizeof *ptr;
	if (pastPtr[OFF_BITS] != static_cast<pool_size_t>(retBits + LEN_OFFSET))
		return kIOReturnInternalError /* "corrupted free area" */;
#ifndef FRAGILE
	/*
	 * Check out area you are going to return to the user as
	 * allocated store. Don't check out the rest, as that would make
	 * the cost of this operation unpredictable. This code produces
	 * no result except a check for errors which should never occur
	 * so it's check code only, which you could delete to save time
	 */
	if (testAny(offset, offset + (POOL_ONE << bits)))
		return kIOReturnInternalError /* "free area does not match bitmap" */;
	if (memAny(ptr + HEADER_LEN, (POOL_ONE << (bits + minBits)) - FREEBLOCK * sizeof *ptr))
		return kIOReturnInternalError /* "corrupted free area" */;
#endif

	/*
	 * remove from free list
	 */
	nextOffset = ptr[OFF_NEXT];
	if (nextOffset != OURNULL) {
		if (nextOffset >= poolBlocks)
			return kIOReturnInternalError /* "corrupted free list" */;
		nextPtr = reinterpret_cast<pool_size_t*>(poolStart + (nextOffset << minBits));
		if (nextPtr[OFF_MAGIC] != MAGIC ||
			nextPtr[OFF_PREV] != offset)
			return kIOReturnInternalError /* "corrupted free area" */;
		nextPtr[OFF_PREV] = OURNULL;
	}
	freeList[retBits] = nextOffset;
	/*
	 * Mark end of allocated area
	 */
	past = offset + (POOL_ONE << bits);
	BITSET(this, past - 1U);
	/*
	 * If we used a larger free block than we needed, free the rest
	 */
	freeBytes -= (POOL_ONE << (retBits + minBits));
	/*
	 * accounts for chunks about to be freed again by toFree
	 */
	toFree(past, offset + (POOL_ONE << retBits), false);
	*newStore = ptr;
	return kIOReturnSuccess;
}

/*
 * Determines size of a busy block
 */
HIDDEN
IOReturn CLASS::BuddyAllocSize(void const *sss, int *numBits)
{
	size_t byteOff, blockOff, blocks;
	uint8_t const* storage = static_cast<uint8_t const*>(sss);
	int ourBits;
	if (!numBits)
		return kIOReturnBadArgument /* "nowhere to store result" */;
	if (storage < poolStart)
		return kIOReturnNotAligned /* "storage not in pool" */;
	byteOff = storage - poolStart;
	blockOff = byteOff >> minBits;
	if (byteOff != (blockOff << minBits))
		return kIOReturnNotAligned /* "storage not on block boundary" */;
	if (blockOff >= poolBlocks)
		return kIOReturnNotAligned /* "storage not in pool" */;
	blocks = 1U;
	for (ourBits = 0;; ++ourBits) {
		if ((blockOff + blocks > poolBlocks) ||
			(ourBits >= numSizes) ||
			(blockOff & (blocks - 1U)))
			return kIOReturnNotAligned /* "bad alloc pointer" */;
		if (BITTEST(this, blockOff + blocks - 1U))
			break;
		blocks <<= 1;
	}
	*numBits = ourBits;
	return kIOReturnSuccess;
}

/*
 * frees free-block bitmap
 */
HIDDEN
void CLASS::ReleaseMap()
{
	if (!map)
		return;
	IOFree(map, (poolBlocks + 7U) >> 3);
	map = 0;
}

#pragma mark -
#pragma mark OSObject Methods
#pragma mark -

bool CLASS::init()
{
	map = 0;
	return super::init();
}

void CLASS::free()
{
	ReleaseMap();
	super::free();
}

CLASS* CLASS::factory()
{
	CLASS* inst = new CLASS;

	if (inst && !inst->init())
	{
		inst->release();
		inst = 0;
	}

	return inst;
}

#pragma mark -
#pragma mark Public Methods
#pragma mark -

IOReturn CLASS::Init(void* startAddress, size_t bytes)
{
	int i;
	size_t setBits;
	int const minBits = 12;
	int const numSizes = 13;

#if 0
	if ((1UL << minBits) < sizeof(pool_size_t) * FREEBLOCK)
		return kIOReturnBadArgument /* "minBits too small" */ ;
	if (numSizes <= 0)
		return kIOReturnBadArgument /* "numSizes <= 0" */;
	if (!(POOL_ONE << (minBits + numSizes - 1)))
		return kIOReturnBadArgument /* "numSizes + minBits too large" */;
#endif
	if (reinterpret_cast<vm_address_t>(startAddress) & ((1UL << minBits) - 1U))
		return kIOReturnBadArgument /* "startAddress not on block boundary" */;
	if (bytes != static_cast<pool_size_t>(bytes))
		return kIOReturnBadArgument /* "bytes too big" */;
	ReleaseMap();	// In case we get reinitialized
	poolStart = static_cast<uint8_t*>(startAddress);
	setBits = bytes >> minBits;
	poolBlocks = static_cast<pool_size_t>(setBits);
	this->minBits = minBits;
	this->numSizes = numSizes;
	map = static_cast<uint8_t*>(IOMalloc((setBits + 7U) >> 3));
	if (!map)
		return kIOReturnNoMemory;
	freeBytes = 0U;
	/*
	 * The bitmap will think everything is allocated, so areas
	 * not handed over to us will not get merged in with any
	 * freed blocks
	 */
	memset(map, 0xFFU, (setBits + 7U) >> 3);
	for (i = 0; i < numSizes; ++i)
		freeList[i] = OURNULL;
	return kIOReturnSuccess;
}

IOReturn CLASS::Rebase(void* newStartAddress)
{
	if (reinterpret_cast<vm_address_t>(newStartAddress) & ((1UL << minBits) - 1U))
		return kIOReturnBadArgument /* "newStartAddress not on block boundary" */;
	poolStart = static_cast<uint8_t*>(newStartAddress);
	return kIOReturnSuccess;
}

IOReturn CLASS::Release(size_t startOffsetBytes, size_t endOffsetBytes)
{
	size_t startBlock;
	size_t pastBlockOff;
	IOReturn ret;

	if (startOffsetBytes & ((1UL << minBits) - 1U))
		return kIOReturnBadArgument;
	if (endOffsetBytes & ((1UL << minBits) - 1U))
		return kIOReturnBadArgument;
	startBlock = startOffsetBytes >> minBits;
	pastBlockOff = endOffsetBytes >> minBits;
	if (startBlock >= poolBlocks || pastBlockOff > poolBlocks)
		return kIOReturnBadArgument;
	ret = clearCheck(startBlock, pastBlockOff);
	if (ret != kIOReturnSuccess)
		return ret;
	/*
	 * Now add everything in range to the free list
	 */
	toFree(static_cast<pool_size_t>(startBlock),
		   static_cast<pool_size_t>(pastBlockOff),
#ifdef FRAGILE
		   false
#else
		   true
#endif
		   );
	return kIOReturnSuccess;
}

IOReturn CLASS::Malloc(size_t bytes, void** newStore)
{
	int bits;
	size_t size;
	if (!newStore)
		return kIOReturnBadArgument /* "null pointer to new store" */;
	size = 1UL << minBits;
	for (bits = 0; size < bytes; ++bits) {
		if (bits >= numSizes)
			return kIOReturnNoResources;	// can't allocate blocks this size
		size <<= 1;
		if (!size)
			return kIOReturnNoResources;	// can't allocate blocks this size
    }
	return BuddyMalloc(bits, newStore);
}

IOReturn CLASS::Realloc(void* ptrv, size_t size, void** newPtr)
{
	int canBits, bits, lg2Bytes;
	pool_size_t blockNo, oldBlocks, newBlocks;
	IOReturn error;
	uint8_t* ptr = static_cast<uint8_t*>(ptrv);
	if (!size)
		return Free(ptr);
	if (!ptr)
		return Malloc(size, newPtr);
	error = BuddyAllocSize(ptr, &bits);
	if (error != kIOReturnSuccess)
		return error;
	lg2Bytes = bits + minBits;
	if ((1UL << lg2Bytes) < size) {
		error = Malloc(size, newPtr);
		if (error != kIOReturnSuccess)
			return error;
		memcpy(*newPtr, ptr, 1UL << lg2Bytes);
		error = Free(ptr);
		if (error != kIOReturnSuccess)
			Free(*newPtr);
		return error;
	}
	*newPtr = ptr;
	for (canBits = minBits; (1UL << canBits) < size; ++canBits);
	if (canBits >= lg2Bytes)
		return kIOReturnSuccess;
	oldBlocks = POOL_ONE << bits;
	newBlocks = POOL_ONE << (canBits - minBits);
	blockNo = static_cast<pool_size_t>((ptr - poolStart) >> minBits);
	/*
	 * set end bits to mark new size
	 */
	BITSET(this, blockNo + newBlocks - 1U);

#ifdef FRAGILE
	BITCLEAR(this, blockNo + oldBlocks - 1U);
	toFree(blockNo + newBlocks, blockNo + oldBlocks, false);
#else
	/*
	 * The new free block is set up by toFree
	 */
	toFree(blockNo + newBlocks, blockNo + oldBlocks, true);
#endif
	return kIOReturnSuccess;
}

IOReturn CLASS::Free(void* storage2)
{
	pool_size_t *sptr, *nextPtr, nextOffset, *pastPtr, blockOff, blocksHere;
	IOReturn ret;
	int bits, oldBits;
	uint8_t* storage = static_cast<uint8_t*>(storage2);
	if (!storage)
		return kIOReturnSuccess;
	ret = BuddyAllocSize(storage, &bits);
	if (ret != kIOReturnSuccess)
		return ret;
	blocksHere = POOL_ONE << bits;
	blockOff = static_cast<pool_size_t>((storage - poolStart) >> minBits);
    /*
	 * mark as free in bitmap
	 */
	BITCLEAR(this, blockOff + blocksHere - 1U);
	/*
	 * clear store. For absolute speed and much less error
	 * checking could only clear areas checked during buddy
	 * merge
	 */
#ifndef FRAGILE
	bzero(storage, 1UL << (bits + minBits));
#endif
	/*
	 * merge with any buddies
	 */
	oldBits = bits;
	while(bits < numSizes - 1) {
		/*
		 * while you can merge two blocks and get a legal block size
		 */
		pool_size_t *buddyPtr, *prevPtr, prevOffset;
		pool_size_t buddy = blockOff ^ (POOL_ONE << bits);
		pool_size_t pastBuddy = buddy + (POOL_ONE << bits);
		int trueBits;
		/*
		 * Could buddy be out of range?
		 */
		if (pastBuddy > poolBlocks)
			break;
		if (BITTEST(this, pastBuddy - 1U))
			break; /* buddy is allocated */
		pastPtr = reinterpret_cast<pool_size_t*>(poolStart + (pastBuddy << minBits));
		trueBits = static_cast<int>(pastPtr[OFF_BITS] - LEN_OFFSET);
		if (trueBits < 0 || trueBits >= numSizes)
			return kIOReturnInternalError /* "corruption 1.1 in free store" */;
		if (trueBits != bits) /* not completely free */
			break;
		/*
		 * Found buddy on free list - remove it and merge
		 */
		pastPtr[OFF_BITS] = 0U;
		buddyPtr = reinterpret_cast<pool_size_t*>(poolStart + (buddy << minBits));
		if (buddyPtr[OFF_MAGIC] != MAGIC)
			return kIOReturnInternalError /* "corruption 2 in free store" */;
		if (freeList[bits] == OURNULL)
			return kIOReturnInternalError /* "corruption 3 in free store" */;
		prevOffset = buddyPtr[OFF_PREV];
		if (prevOffset == OURNULL) {
			nextOffset = buddyPtr[OFF_NEXT];
			if (nextOffset != OURNULL) {
				if (nextOffset >= poolBlocks)
					return kIOReturnInternalError /* "corruption 3.1 in free store" */;
				nextPtr = reinterpret_cast<pool_size_t*>(poolStart + (nextOffset << minBits));
				if (nextPtr[OFF_MAGIC] != MAGIC)
					return kIOReturnInternalError /* "corruption 4 in free store" */;
				if (nextPtr[OFF_PREV] != buddy)
					return kIOReturnInternalError /* "corruption 5 in free store" */;
				nextPtr[OFF_PREV] = OURNULL;
			}
			freeList[bits] = nextOffset;
		} else {
			if (prevOffset >= poolBlocks)
				return kIOReturnInternalError /* "corruption 5.1 in free store" */;
			prevPtr = reinterpret_cast<pool_size_t*>(poolStart + (prevOffset << minBits));
			if (prevPtr[OFF_MAGIC] != MAGIC)
				return kIOReturnInternalError /* "corruption 6 in free store" */;
			if (prevPtr[OFF_NEXT] != buddy)
				return kIOReturnInternalError /* "corruption 7 in free store" */;
			nextOffset = buddyPtr[OFF_NEXT];
			if (nextOffset != OURNULL) {
				if (nextOffset >= poolBlocks)
					return kIOReturnInternalError /* "corruption 7.1 in free store" */;
				nextPtr = reinterpret_cast<pool_size_t*>(poolStart + (nextOffset << minBits));
				if (nextPtr[OFF_MAGIC] != MAGIC)
					return kIOReturnInternalError /* "corruption 8 in free store" */;
				if (nextPtr[OFF_PREV] != buddy)
					return kIOReturnInternalError /* "corruption 9 in free store" */;
				nextPtr[OFF_PREV] = prevOffset;
			}
			prevPtr[OFF_NEXT] = nextOffset;
		}
		/*
		 * clear buddy's free block
		 */
		bzero(buddyPtr, HEADER_LEN * sizeof *buddyPtr);
		++bits;
		if (buddy < blockOff) {
			/*
			 * Merged block starts at buddy
			 */
			blockOff = buddy;
			storage = reinterpret_cast<uint8_t*>(buddyPtr);
		}
	}
	/*
	 * add to free list
	 */
	sptr = reinterpret_cast<pool_size_t*>(storage);
	sptr[OFF_MAGIC] = MAGIC;
	sptr[OFF_PREV] = OURNULL;
	nextOffset = freeList[bits];
	if (nextOffset != OURNULL) {
		if (nextOffset >= poolBlocks)
			return kIOReturnInternalError /* "corruption 9.1 in free store" */;
		nextPtr = reinterpret_cast<pool_size_t*>(poolStart + (nextOffset << minBits));
		if (nextPtr[OFF_MAGIC] != MAGIC)
			return kIOReturnInternalError /* "corruption 10 in free store" */;
		if (nextPtr[OFF_PREV] != OURNULL)
			return kIOReturnInternalError /* "corruption 11 in free store" */;
		nextPtr[OFF_PREV] = blockOff;
	}
	sptr[OFF_NEXT] = nextOffset;
	pastPtr = reinterpret_cast<pool_size_t*>(storage + (1UL << (minBits + bits)));
	pastPtr[OFF_BITS] = static_cast<pool_size_t>(bits + LEN_OFFSET);
	freeList[bits] = blockOff;
	freeBytes += (POOL_ONE << (oldBits + minBits));
	return kIOReturnSuccess;
}

IOReturn CLASS::Available(size_t* bytesFree)
{
	if (!bytesFree)
		return kIOReturnBadArgument /* "no room to store bytes free" */;
	*bytesFree = freeBytes;
	return kIOReturnSuccess;
}

IOReturn CLASS::Check(size_t* counts)
{
	int size;
	pool_size_t seenStore = 0U;
	/*
	 * Check the free list for all possible sizes
	 */
	for (size = 0; size < numSizes; ++size) {
		pool_size_t offset, *ptr, *pastPtr, prevOffset = OURNULL, blocksSeen = 0U;
		/*
		 * maxBlocks catches loops in the free list
		 */
		pool_size_t maxBlocks = poolBlocks >> size;
		for (offset = freeList[size];
			 offset != OURNULL && blocksSeen < maxBlocks; ++blocksSeen) {
			if (offset >= poolBlocks)
				return kIOReturnInternalError /* "bad pointer" */;
			if (testAny(offset,
						offset + (POOL_ONE << size)))
				return kIOReturnInternalError /* "free area not free in bitmap" */;
			ptr = reinterpret_cast<pool_size_t*>(poolStart + (offset << minBits));
			if (ptr[OFF_MAGIC] != MAGIC)
				return kIOReturnInternalError /* "bad magic" */;
			if (ptr[OFF_PREV] != prevOffset)
				return kIOReturnInternalError /* "crossed pointers" */;
			pastPtr = ptr + (POOL_ONE << (minBits + size)) / sizeof *ptr;
			if (pastPtr[OFF_BITS] != static_cast<pool_size_t>(size + LEN_OFFSET))
				return kIOReturnInternalError /* "size mismatch" */;
#ifndef FRAGILE
			if (memAny(ptr + HEADER_LEN,
					   (POOL_ONE << (size + minBits)) - FREEBLOCK * sizeof *ptr))
				return kIOReturnInternalError /* "free area corrupt" */;
#endif
			prevOffset = offset;
			offset = ptr[OFF_NEXT];
			seenStore += (POOL_ONE << size);
		}
		if (offset != OURNULL)
			return kIOReturnInternalError /* "free list impossibly long - must be cycle" */;
		if (counts)
			counts[size] = blocksSeen;
	}
	if ((seenStore << minBits) != freeBytes)
		return kIOReturnInternalError /* "store accounting does not balance" */;
	return kIOReturnSuccess;
}
