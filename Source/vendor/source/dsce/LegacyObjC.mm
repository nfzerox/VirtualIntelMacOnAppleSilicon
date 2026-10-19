#include <algorithm>
#include <map>
#include <set>
#include <vector>

struct LegacyRebaseLocation
{
	uint8_t type;
	uint8_t segmentIndex;
	uint64_t segmentOffset;

	bool operator<(const LegacyRebaseLocation& other) const
	{
		if(segmentIndex!=other.segmentIndex)
			return segmentIndex<other.segmentIndex;
		if(segmentOffset!=other.segmentOffset)
			return segmentOffset<other.segmentOffset;
		return type<other.type;
	}
};

static uint64_t readULEB(const uint8_t*& cursor,const uint8_t* end)
{
	uint64_t result=0;
	unsigned shift=0;
	while(cursor<end)
	{
		uint8_t byte=*cursor++;
		result|=(uint64_t)(byte&0x7f)<<shift;
		if(!(byte&0x80))
			return result;
		shift+=7;
		assert(shift<64);
	}
	abort();
}

static void appendULEB(std::vector<uint8_t>& output,uint64_t value)
{
	do
	{
		uint8_t byte=value&0x7f;
		value>>=7;
		if(value)
			byte|=0x80;
		output.push_back(byte);
	}
	while(value);
}

class LegacyObjCConverter
{
public:
	NSMutableData* data;
	struct mach_header_64* header;
	std::vector<struct segment_command_64*> segments;
	std::set<LegacyRebaseLocation> rebases;
	std::map<uint64_t,uint64_t> expandedLists;
	uint64_t allocationCursor;
	uint64_t allocationEnd;
	uint8_t dataSegmentIndex;

	LegacyObjCConverter(NSMutableData* input):data(input)
	{
		header=(struct mach_header_64*)data.mutableBytes;
		assert(header->magic==MH_MAGIC_64);
		assert(header->cputype==CPU_TYPE_X86_64);

		struct load_command* command=(struct load_command*)(header+1);
		for(uint32_t index=0;index<header->ncmds;index++)
		{
			assert((char*)command+command->cmdsize<=(char*)data.mutableBytes+data.length);
			if(command->cmd==LC_SEGMENT_64)
				segments.push_back((struct segment_command_64*)command);
			command=(struct load_command*)((char*)command+command->cmdsize);
		}

		struct segment_command_64* dataSegment=segmentNamed("__DATA",&dataSegmentIndex);
		assert(dataSegment);
		allocationCursor=dataSegment->vmaddr;
		struct section_64* sections=(struct section_64*)(dataSegment+1);
		for(uint32_t index=0;index<dataSegment->nsects;index++)
			allocationCursor=MAX(allocationCursor,sections[index].addr+sections[index].size);
		allocationCursor=(allocationCursor+7)&~7ULL;
		allocationEnd=dataSegment->vmaddr+dataSegment->filesize;
		assert(allocationCursor<=allocationEnd);
	}

	struct load_command* commandWithType(uint32_t type)
	{
		struct load_command* command=(struct load_command*)(header+1);
		for(uint32_t index=0;index<header->ncmds;index++)
		{
			if(command->cmd==type)
				return command;
			command=(struct load_command*)((char*)command+command->cmdsize);
		}
		return NULL;
	}

	struct segment_command_64* segmentNamed(const char* name,uint8_t* indexOut=NULL)
	{
		for(uint8_t index=0;index<segments.size();index++)
		{
			if(!strncmp(segments[index]->segname,name,16))
			{
				if(indexOut)
					*indexOut=index;
				return segments[index];
			}
		}
		return NULL;
	}

	struct section_64* sectionNamed(const char* segmentName,const char* sectionName)
	{
		struct segment_command_64* segment=segmentNamed(segmentName);
		if(!segment)
			return NULL;
		struct section_64* sections=(struct section_64*)(segment+1);
		for(uint32_t index=0;index<segment->nsects;index++)
			if(!strncmp(sections[index].sectname,sectionName,16))
				return &sections[index];
		return NULL;
	}

	uint64_t fileOffsetForVM(uint64_t address,size_t length=1)
	{
		for(struct segment_command_64* segment:segments)
		{
			if(address>=segment->vmaddr&&address+length<=segment->vmaddr+segment->filesize)
				return segment->fileoff+(address-segment->vmaddr);
		}
		trace(@"address outside file-backed segments: %llx",address);
		abort();
	}

	template<typename T> T* pointerForVM(uint64_t address)
	{
		uint64_t offset=fileOffsetForVM(address,sizeof(T));
		return (T*)((uint8_t*)data.mutableBytes+offset);
	}

	void addRebase(uint64_t address,uint8_t type=REBASE_TYPE_POINTER)
	{
		for(uint8_t index=0;index<segments.size();index++)
		{
			struct segment_command_64* segment=segments[index];
			if(address>=segment->vmaddr&&address<segment->vmaddr+segment->vmsize)
			{
				rebases.insert({type,index,address-segment->vmaddr});
				return;
			}
		}
		abort();
	}

	void parseRebases()
	{
		struct dyld_info_command* info=(struct dyld_info_command*)commandWithType(LC_DYLD_INFO_ONLY);
		assert(info);
		const uint8_t* cursor=(const uint8_t*)data.bytes+info->rebase_off;
		const uint8_t* end=cursor+info->rebase_size;
		uint8_t type=REBASE_TYPE_POINTER;
		uint8_t segmentIndex=0;
		uint64_t segmentOffset=0;
		bool done=false;

		auto emit=[&]()
		{
			assert(segmentIndex<segments.size());
			rebases.insert({type,segmentIndex,segmentOffset});
			segmentOffset+=sizeof(uint64_t);
		};

		while(cursor<end&&!done)
		{
			uint8_t byte=*cursor++;
			uint8_t opcode=byte&REBASE_OPCODE_MASK;
			uint8_t immediate=byte&REBASE_IMMEDIATE_MASK;
			switch(opcode)
			{
				case REBASE_OPCODE_DONE:
					done=true;
					break;
				case REBASE_OPCODE_SET_TYPE_IMM:
					type=immediate;
					break;
				case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
					segmentIndex=immediate;
					segmentOffset=readULEB(cursor,end);
					break;
				case REBASE_OPCODE_ADD_ADDR_ULEB:
					segmentOffset+=readULEB(cursor,end);
					break;
				case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
					segmentOffset+=immediate*sizeof(uint64_t);
					break;
				case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
					for(uint8_t count=0;count<immediate;count++) emit();
					break;
				case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
				{
					uint64_t count=readULEB(cursor,end);
					for(uint64_t index=0;index<count;index++) emit();
					break;
				}
				case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
					emit();
					segmentOffset+=readULEB(cursor,end);
					break;
				case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
				{
					uint64_t count=readULEB(cursor,end);
					uint64_t skip=readULEB(cursor,end);
					for(uint64_t index=0;index<count;index++)
					{
						emit();
						segmentOffset+=skip;
					}
					break;
				}
				default:
					abort();
			}
		}
		// ld may end a bounded rebase stream immediately after the final
		// operation without emitting REBASE_OPCODE_DONE.
		assert(done||cursor==end);
	}

	uint64_t expandMethodList(uint64_t oldAddress)
	{
		if(!oldAddress)
			return 0;
		auto existing=expandedLists.find(oldAddress);
		if(existing!=expandedLists.end())
			return existing->second;

		struct objc_method_list* oldHeader=pointerForVM<struct objc_method_list>(oldAddress);
		if(!(oldHeader->flags&usesRelativeOffsets))
			return oldAddress;
		bool directSelectors=oldHeader->flags&usesDirectOffsetsToSelectors;

		uint64_t length=sizeof(struct objc_method_list)+oldHeader->count*sizeof(struct objc_method);
		uint64_t newAddress=(allocationCursor+7)&~7ULL;
		assert(newAddress+length<=allocationEnd);
		allocationCursor=newAddress+length;
		expandedLists[oldAddress]=newAddress;

		struct objc_method_list* newHeader=pointerForVM<struct objc_method_list>(newAddress);
		newHeader->flags=sizeof(struct objc_method);
		newHeader->count=oldHeader->count;
		struct objc_relative_method* inputs=(struct objc_relative_method*)(oldHeader+1);
		struct objc_method* outputs=(struct objc_method*)(newHeader+1);

		for(uint32_t index=0;index<oldHeader->count;index++)
		{
			uint64_t nameField=oldAddress+sizeof(*oldHeader)+index*sizeof(*inputs);
			uint64_t typesField=nameField+offsetof(struct objc_relative_method,signature);
			uint64_t impField=nameField+offsetof(struct objc_relative_method,implementation);
			uint64_t nameTarget=nameField+inputs[index].name;
			uint64_t selectorName=directSelectors
				?nameTarget
				:*pointerForVM<uint64_t>(nameTarget);
			uint64_t typesAddress=typesField+inputs[index].signature;
			uint64_t impAddress=impField+inputs[index].implementation;

			outputs[index].name=(char*)selectorName;
			outputs[index].signature=(char*)typesAddress;
			outputs[index].implementation=(void*)impAddress;
			uint64_t outputAddress=newAddress+sizeof(*newHeader)+index*sizeof(*outputs);
			addRebase(outputAddress+offsetof(struct objc_method,name));
			addRebase(outputAddress+offsetof(struct objc_method,signature));
			addRebase(outputAddress+offsetof(struct objc_method,implementation));
		}
		return newAddress;
	}

	void replaceMethodPointer(uint64_t fieldAddress)
	{
		uint64_t* field=pointerForVM<uint64_t>(fieldAddress);
		if(!*field)
			return;
		uint64_t replacement=expandMethodList(*field);
		if(replacement!=*field)
		{
			*field=replacement;
			addRebase(fieldAddress);
		}
	}

	void convertClass(uint64_t classAddress)
	{
		if(!classAddress)
			return;
		struct objc_class* cls=pointerForVM<struct objc_class>(classAddress);
		uint64_t dataAddress=((uint64_t)cls->data)&~7ULL;
		uint64_t methodsField=dataAddress+offsetof(struct objc_data,baseMethods);
		replaceMethodPointer(methodsField);
	}

	void convertClassSection(const char* sectionName)
	{
		struct section_64* section=sectionNamed("__DATA_CONST",sectionName);
		if(!section)
			return;
		assert(section->size%sizeof(uint64_t)==0);
		uint64_t* classes=pointerForVM<uint64_t>(section->addr);
		for(uint64_t index=0;index<section->size/sizeof(uint64_t);index++)
		{
			struct objc_class* cls=pointerForVM<struct objc_class>(classes[index]);
			convertClass(classes[index]);
			convertClass((uint64_t)cls->metaclass);
		}
	}

	void convertCategories(const char* sectionName)
	{
		struct section_64* section=sectionNamed("__DATA_CONST",sectionName);
		if(!section)
			return;
		uint64_t* categories=pointerForVM<uint64_t>(section->addr);
		for(uint64_t index=0;index<section->size/sizeof(uint64_t);index++)
		{
			uint64_t category=categories[index];
			replaceMethodPointer(category+offsetof(struct objc_category,instanceMethods));
			replaceMethodPointer(category+offsetof(struct objc_category,classMethods));
		}
	}

	void convertProtocols()
	{
		struct section_64* section=sectionNamed("__DATA_CONST","__objc_protolist");
		if(!section)
			return;
		uint64_t* protocols=pointerForVM<uint64_t>(section->addr);
		for(uint64_t index=0;index<section->size/sizeof(uint64_t);index++)
		{
			uint64_t protocol=protocols[index];
			replaceMethodPointer(protocol+offsetof(struct objc_protocol,instanceMethods));
			replaceMethodPointer(protocol+offsetof(struct objc_protocol,classMethods));
			replaceMethodPointer(protocol+offsetof(struct objc_protocol,optionalInstanceMethods));
			replaceMethodPointer(protocol+offsetof(struct objc_protocol,optionalClassMethods));
		}
	}

	void writeRebases()
	{
		std::vector<uint8_t> encoded;
		std::vector<LegacyRebaseLocation> ordered(rebases.begin(),rebases.end());
		std::sort(ordered.begin(),ordered.end(),[](const auto& left,const auto& right)
		{
			if(left.segmentIndex!=right.segmentIndex)
				return left.segmentIndex<right.segmentIndex;
			if(left.type!=right.type)
				return left.type<right.type;
			return left.segmentOffset<right.segmentOffset;
		});

		size_t index=0;
		while(index<ordered.size())
		{
			uint8_t segmentIndex=ordered[index].segmentIndex;
			uint8_t type=ordered[index].type;
			encoded.push_back(REBASE_OPCODE_SET_TYPE_IMM|type);
			encoded.push_back(REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB|segmentIndex);
			appendULEB(encoded,ordered[index].segmentOffset);

			uint64_t cursor=ordered[index].segmentOffset;
			while(index<ordered.size()&&ordered[index].segmentIndex==segmentIndex&&ordered[index].type==type)
			{
				assert(ordered[index].segmentOffset>=cursor);
				if(ordered[index].segmentOffset>cursor)
				{
					encoded.push_back(REBASE_OPCODE_ADD_ADDR_ULEB);
					appendULEB(encoded,ordered[index].segmentOffset-cursor);
					cursor=ordered[index].segmentOffset;
				}

				size_t runStart=index;
				while(index+1<ordered.size()&&
					ordered[index+1].segmentIndex==segmentIndex&&
					ordered[index+1].type==type&&
					ordered[index+1].segmentOffset==ordered[index].segmentOffset+sizeof(uint64_t))
					index++;
				uint64_t count=index-runStart+1;
				if(count<=REBASE_IMMEDIATE_MASK)
					encoded.push_back(REBASE_OPCODE_DO_REBASE_IMM_TIMES|count);
				else
				{
					encoded.push_back(REBASE_OPCODE_DO_REBASE_ULEB_TIMES);
					appendULEB(encoded,count);
				}
				cursor=ordered[index].segmentOffset+sizeof(uint64_t);
				index++;
			}
		}
		encoded.push_back(REBASE_OPCODE_DONE);
		while(encoded.size()%8)
			encoded.push_back(0);

		struct dyld_info_command* oldInfo=(struct dyld_info_command*)commandWithType(LC_DYLD_INFO_ONLY);
		assert(oldInfo);
		uint32_t outputOffset=oldInfo->rebase_off;
		uint32_t oldSize=oldInfo->rebase_size;
		uint32_t tailOffset=outputOffset+oldSize;
		int64_t delta=(int64_t)encoded.size()-oldSize;
		[data replaceBytesInRange:NSMakeRange(outputOffset,oldSize)
			withBytes:encoded.data() length:encoded.size()];

		// NSMutableData may have moved. Reload all load-command pointers.
		header=(struct mach_header_64*)data.mutableBytes;
		segments.clear();
		struct load_command* reloadCommand=(struct load_command*)(header+1);
		for(uint32_t index=0;index<header->ncmds;index++)
		{
			if(reloadCommand->cmd==LC_SEGMENT_64)
				segments.push_back((struct segment_command_64*)reloadCommand);
			reloadCommand=(struct load_command*)((char*)reloadCommand+reloadCommand->cmdsize);
		}
		auto shift=[&](uint32_t& offset)
		{
			if(offset&&offset>=tailOffset)
				offset=(uint32_t)((int64_t)offset+delta);
		};

		struct load_command* command=(struct load_command*)(header+1);
		for(uint32_t commandIndex=0;commandIndex<header->ncmds;commandIndex++)
		{
			switch(command->cmd)
			{
				case LC_DYLD_INFO:
				case LC_DYLD_INFO_ONLY:
				{
					struct dyld_info_command* info=(struct dyld_info_command*)command;
					shift(info->bind_off);
					shift(info->weak_bind_off);
					shift(info->lazy_bind_off);
					shift(info->export_off);
					info->rebase_off=outputOffset;
					info->rebase_size=encoded.size();
					break;
				}
				case LC_SYMTAB:
				{
					struct symtab_command* table=(struct symtab_command*)command;
					shift(table->symoff);
					shift(table->stroff);
					break;
				}
				case LC_DYSYMTAB:
				{
					struct dysymtab_command* table=(struct dysymtab_command*)command;
					shift(table->tocoff);
					shift(table->modtaboff);
					shift(table->extrefsymoff);
					shift(table->indirectsymoff);
					shift(table->extreloff);
					shift(table->locreloff);
					break;
				}
				case LC_FUNCTION_STARTS:
				case LC_DATA_IN_CODE:
				case LC_SEGMENT_SPLIT_INFO:
				case LC_DYLIB_CODE_SIGN_DRS:
				case LC_LINKER_OPTIMIZATION_HINT:
#ifdef LC_DYLD_EXPORTS_TRIE
				case LC_DYLD_EXPORTS_TRIE:
#endif
#ifdef LC_DYLD_CHAINED_FIXUPS
				case LC_DYLD_CHAINED_FIXUPS:
#endif
				{
					struct linkedit_data_command* linkeditData=(struct linkedit_data_command*)command;
					shift(linkeditData->dataoff);
					break;
				}
				case LC_TWOLEVEL_HINTS:
					shift(((struct twolevel_hints_command*)command)->offset);
					break;
			}
			command=(struct load_command*)((char*)command+command->cmdsize);
		}

		for(struct segment_command_64* segment:segments)
		{
			struct section_64* sections=(struct section_64*)(segment+1);
			for(uint32_t sectionIndex=0;sectionIndex<segment->nsects;sectionIndex++)
				shift(sections[sectionIndex].reloff);
		}

		struct segment_command_64* linkedit=segmentNamed("__LINKEDIT");
		assert(linkedit&&outputOffset>=linkedit->fileoff&&delta>=-(int64_t)oldSize);
		linkedit->filesize=data.length-linkedit->fileoff;
		// Leave room for the ad-hoc signature that the caller adds after this
		// rewrite. __LINKEDIT is the final segment, so extending it cannot
		// overlap a following mapping.
		uint64_t requiredVMSize=(linkedit->filesize+0x10000+0x3fff)&~0x3fffULL;
		linkedit->vmsize=MAX(linkedit->vmsize,requiredVMSize);
		trace(@"rebuilt rebase opcodes %x -> %lx bytes (linkedit delta %lld)",oldSize,encoded.size(),delta);
	}

	void run()
	{
		parseRebases();
		size_t originalRebases=rebases.size();
		convertClassSection("__objc_classlist");
		convertClassSection("__objc_nlclslist");
		convertCategories("__objc_catlist");
		convertCategories("__objc_nlcatlist");
		convertProtocols();
		trace(@"expanded %lx method lists; rebases %lx -> %lx; used %llx bytes",expandedLists.size(),originalRebases,rebases.size(),allocationCursor);
		writeRebases();
	}
};

void convertLegacyObjCImage(NSString* inputPath,NSString* outputPath)
{
	NSMutableData* data=[NSMutableData dataWithContentsOfFile:inputPath];
	assert(data);
	LegacyObjCConverter converter(data);
	converter.run();
	assert([data writeToFile:outputPath atomically:true]);
	trace(@"wrote legacy Objective-C image %@",outputPath);
}
