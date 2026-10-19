@implementation CacheFile

-(instancetype)initWithPath:(NSString*)path
{
	self=super.init;
	
	self.data=[NSMutableData dataWithContentsOfFile:path];
	if(!self.data)
	{
		return nil;
	}
	
	NSMutableArray<CacheImage*>* images=NSMutableArray.alloc.init.autorelease;
	
	uint32_t imagesOffset=self.header->imagesOffsetOld;
	uint32_t imagesCount=self.header->imagesCountOld;
	if(self.header->mappingOffset>=offsetof(struct dyld_cache_header,imagesCount))
	{
		imagesOffset=self.header->imagesOffset;
		imagesCount=self.header->imagesCount;
	}

	struct dyld_cache_image_info* infos=(struct dyld_cache_image_info*)wrapOffset(self,imagesOffset).pointer;
	
	for(int index=0;index<imagesCount;index++)
	{
		CacheImage* image=[CacheImage.alloc initWithCacheFile:self info:&infos[index]].autorelease;
		if(image)
		{
			[images addObject:image];
		}
	}
	
	self.images=images;
	
	NSMutableDictionary<NSNumber*,NSMutableArray<NSNumber*>*>* rebasesByChunk=NSMutableDictionary.alloc.init.autorelease;
	
	[self forEachMapping:^(struct dyld_cache_mapping_and_slide_info* mapping)
	{
		if(mapping->slideInfoFileOffset)
		{
			dyld_cache_slide_info2* slide=(struct dyld_cache_slide_info2*)wrapOffset(self,mapping->slideInfoFileOffset).pointer;
			assert(slide->version==2);
			
			// dyld_cache_format.h
			
			unsigned long valueMask=~(slide->delta_mask);
			int deltaShift=__builtin_ctzll(slide->delta_mask)-2;
			
			short* starts=(short*)((char*)slide+slide->page_starts_offset);
			for(long pageIndex=0;pageIndex<slide->page_starts_count;pageIndex++)
			{
				if(starts[pageIndex]==DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE)
				{
					continue;
				}
				
				assert((starts[pageIndex]&DYLD_CACHE_SLIDE_PAGE_ATTR_EXTRA)==0);
				
				unsigned long startAddress=mapping->address+pageIndex*slide->page_size+starts[pageIndex]*4;
				char* pointer=wrapAddress(self,startAddress).pointer;
				
				int delta=1;
				while(delta)
				{
					unsigned long* valuePointer=(unsigned long*)pointer;
					delta=(*valuePointer&slide->delta_mask)>>deltaShift;
					
					unsigned long value=(*valuePointer&valueMask)+slide->value_add;
					*valuePointer=value;
					
					unsigned long address=wrapPointer(self,pointer).address;
					NSNumber* chunk=[NSNumber numberWithLong:address/FAST_CHUNK_SIZE];
					if(!rebasesByChunk[chunk])
					{
						rebasesByChunk[chunk]=NSMutableArray.alloc.init.autorelease;
					}
					[rebasesByChunk[chunk] addObject:[NSNumber numberWithLong:address]];
					
					pointer+=delta;
				}
			}
		}
	}];
	self.fastRebasesByChunk=rebasesByChunk;
	
	NSMutableDictionary<NSNumber*,NSMutableArray<CacheImage*>*>* imagesByChunk=NSMutableDictionary.alloc.init.autorelease;
	for(CacheImage* image in self.images)
	{
		NSArray<NSNumber*>* chunks=[image enclosingChunksWithSize:FAST_CHUNK_SIZE];
		for(NSNumber* chunk in chunks)
		{
			if(!imagesByChunk[chunk])
			{
				imagesByChunk[chunk]=NSMutableArray.alloc.init.autorelease;
			}
			[imagesByChunk[chunk] addObject:image];
		}
	}
	self.fastImagesByChunk=imagesByChunk;
	
	NSMutableDictionary<NSString*,CacheImage*>* imagesByPath=NSMutableDictionary.alloc.init.autorelease;
	for(CacheImage* image in self.images)
	{
		imagesByPath[image.path]=image;
	}
	self.fastImagesByPath=imagesByPath;
	
	return self;
}

-(long)maxConstDataMappingAddress
{
	__block long max=0;
	
	[self forEachMapping:^(struct dyld_cache_mapping_and_slide_info* info)
	{
		if(info->flags==DYLD_CACHE_MAPPING_CONST_DATA)
		{
			assert(max==0);
			max=info->address+info->size;
		}
	}];
	
	assert(max!=0);
	return max;
}

-(long)minConstDataMappingAddress
{
	__block long start=0;
	[self forEachMapping:^(struct dyld_cache_mapping_and_slide_info* info) {
		if(info->flags==DYLD_CACHE_MAPPING_CONST_DATA) start=info->address;
	}];
	assert(start);
	return start;
}

-(long)minConstDataSegmentAddress
{
	long start=self.minConstDataMappingAddress;
	long end=self.maxConstDataMappingAddress;
	__block long first=end;
	for(CacheImage* image in self.images) {
		[image.header forEachSegmentCommand:^(struct segment_command_64* command) {
			if(command->vmaddr>=start&&command->vmaddr<end)
				first=MIN(first,command->vmaddr);
		}];
	}
	assert(first>start&&first<end);
	return first;
}

-(long)maxConstDataSegmentAddress
{
	long mappingEnd=self.maxConstDataMappingAddress;
	
	__block long max=0;
	
	for(CacheImage* image in self.images)
	{
		[image.header forEachSegmentCommand:^(struct segment_command_64* command)
		{
			long end=command->vmaddr+command->vmsize;
			if(end<mappingEnd)
			{
				max=MAX(max,end);
			}
		}];
	}
	
	assert(max!=0);
	return max;
}

-(struct dyld_cache_header*)header
{
	return (struct dyld_cache_header*)self.data.bytes;
}

-(void)forEachMapping:(void (^)(struct dyld_cache_mapping_and_slide_info*))block
{
	// using Location here would create a circular dependency

	if(self.header->mappingOffset<=offsetof(struct dyld_cache_header,mappingWithSlideOffset))
	{
		// Catalina and older caches have one legacy slide-info blob for the
		// writable data mapping instead of a parallel mapping-with-slide table.
		struct dyld_cache_mapping_info* infos=(struct dyld_cache_mapping_info*)((char*)self.data.mutableBytes+self.header->mappingOffset);
		for(int index=0;index<self.header->mappingCount;index++)
		{
			struct dyld_cache_mapping_and_slide_info info={};
			info.address=infos[index].address;
			info.size=infos[index].size;
			info.fileOffset=infos[index].fileOffset;
			info.maxProt=infos[index].maxProt;
			info.initProt=infos[index].initProt;
			if(index==1)
			{
				info.slideInfoFileOffset=self.header->slideInfoOffsetUnused;
				info.slideInfoFileSize=self.header->slideInfoSizeUnused;
			}
			block(&info);
		}
		return;
	}

	struct dyld_cache_mapping_and_slide_info* infos=(struct dyld_cache_mapping_and_slide_info*)((char*)self.data.mutableBytes+self.header->mappingWithSlideOffset);
	for(int index=0;index<self.header->mappingWithSlideCount;index++)
	{
		block(&infos[index]);
	}
}

-(long)addressWithOffset:(long)offset
{
	__block long result=-1;
	
	[self forEachMapping:^(struct dyld_cache_mapping_and_slide_info* mapping)
	{
		if(offset>=mapping->fileOffset&&offset<mapping->fileOffset+mapping->size)
		{
			assert(result==-1);
			result=mapping->address+offset-mapping->fileOffset;
		}
	}];
	
	return result;
}

-(long)addressWithPointer:(char*)pointer
{
	return [self addressWithOffset:pointer-(char*)self.data.mutableBytes];
}

-(long)offsetWithAddress:(long)address
{
	__block long result=-1;
	
	[self forEachMapping:^(struct dyld_cache_mapping_and_slide_info* mapping)
	{
		if(address>=mapping->address&&address<mapping->address+mapping->size)
		{
			assert(result==-1);
			result=mapping->fileOffset+address-mapping->address;
		}
	}];
	
	return result;
}

-(char*)pointerWithAddress:(long)address
{
	long offset=[self offsetWithAddress:address];
	if(offset==-1)
	{
		return NULL;
	}
	
	return (char*)self.data.mutableBytes+offset;
}

-(CacheImage*)imageWithPath:(NSString*)path
{
	return self.fastImagesByPath[path];
}

-(NSArray<CacheImage*>*)imagesWithPathPrefix:(NSString*)path
{
	NSMutableArray<CacheImage*>* result=NSMutableArray.alloc.init.autorelease;
	
	for(CacheImage* image in self.images)
	{
		if([image.path hasPrefix:path])
		{
			[result addObject:image];
		}
	}
	
	return result;
}

-(CacheImage*)imageWithAddress:(long)address
{
	NSNumber* chunk=[NSNumber numberWithLong:address/FAST_CHUNK_SIZE];
	NSArray<CacheImage*>* candidates=self.fastImagesByChunk[chunk];
	
	for(CacheImage* image in candidates)
	{
		if([image.header segmentCommandWithAddress:address indexOut:NULL])
		{
			return image;
		}
	}
	
	return nil;
}

-(NSArray<NSNumber*>*)rebasesWithStartAddress:(long)start endAddress:(long)end
{
	NSMutableArray<NSNumber*>* result=NSMutableArray.alloc.init.autorelease;
	
	long startChunk=start/FAST_CHUNK_SIZE;
	long endChunk=(end-1)/FAST_CHUNK_SIZE;
	for(long chunk=startChunk;chunk<=endChunk;chunk++)
	{
		for(NSNumber* rebase in self.fastRebasesByChunk[[NSNumber numberWithLong:chunk]])
		{
			if(rebase.longValue>=start&&rebase.longValue<end)
			{
				[result addObject:rebase];
			}
		}
	}
	
	return result;
}

@end
