"""Small build-time FAT32 editor for generated firmware copies, never mounts.

Supports regular files/directories and ASCII long names. Geometry and chains
are checked; allocation changes every FAT mirror and invalidates FSInfo hints.
No original image is written by this module.
"""
import re
import struct

class FAT32:
    def __init__(self, data):
        self.data=data
        self.sector=struct.unpack_from('<H',data,11)[0]
        self.cluster=data[13]*self.sector
        self.reserved=struct.unpack_from('<H',data,14)[0]
        self.fats=data[16];self.fat_size=struct.unpack_from('<I',data,36)[0]*self.sector
        self.fat=self.reserved*self.sector
        self.start=self.fat+self.fats*self.fat_size
        self.root=struct.unpack_from('<I',data,44)[0]
        sectors=struct.unpack_from('<I',data,32)[0]
        if (self.sector!=512 or not self.cluster or self.cluster&(self.cluster-1) or
                not self.fats or not self.fat_size or self.start>=sectors*self.sector or
                sectors*self.sector>len(data)):
            raise ValueError('unsupported FAT32 geometry')
        self.limit=2+(sectors*self.sector-self.start)//self.cluster
        if self.limit*4>self.fat_size:raise ValueError('FAT is too short')
        self.free=[c for c in range(2,self.limit) if not self.next(c)]

    def next(self, cluster):
        if not 2<=cluster<self.limit:raise ValueError('cluster outside FAT32 data area')
        return struct.unpack_from('<I',self.data,self.fat+cluster*4)[0]&0x0fffffff

    def link(self, cluster, value):
        for mirror in range(self.fats):
            struct.pack_into('<I',self.data,self.fat+mirror*self.fat_size+cluster*4,value)

    def offset(self, cluster):return self.start+(cluster-2)*self.cluster

    def chain(self, cluster):
        result=[];seen=set()
        while cluster<0x0ffffff8:
            if cluster in seen:raise ValueError('FAT32 chain cycle')
            seen.add(cluster);result.append(cluster);cluster=self.next(cluster)
        return result

    def allocate(self, count):
        if count>len(self.free):raise ValueError('generated EFI image has insufficient free clusters')
        result=self.free[:count];del self.free[:count]
        for index,c in enumerate(result):
            self.link(c,result[index+1] if index+1<count else 0x0fffffff)
            off=self.offset(c);self.data[off:off+self.cluster]=bytes(self.cluster)
        # Free-count/next-free are only hints. Mark unknown rather than retain
        # stale values; do the same for the validated backup FSInfo sector.
        fsinfo=struct.unpack_from('<H',self.data,48)[0]
        backup=struct.unpack_from('<H',self.data,50)[0]
        for sector in {fsinfo,backup+fsinfo}:
            off=sector*self.sector
            if 0<sector<self.reserved and self.data[off:off+4]==b'RRaA' and self.data[off+484:off+488]==b'rrAa':
                struct.pack_into('<II',self.data,off+488,0xffffffff,0xffffffff)
        return result

    def slots(self, directory):
        return [off for c in self.chain(directory)
                for off in range(self.offset(c),self.offset(c)+self.cluster,32)]

    def entries(self, directory):
        long={}
        for off in self.slots(directory):
            item=self.data[off:off+32]
            if not item[0]:break
            if item[0]==0xe5:long={};continue
            if item[11]==15:
                long[item[0]&31]=bytes(item[1:11]+item[14:26]+item[28:32]).decode('utf-16le');continue
            if item[11]&8:long={};continue
            name=''.join(long[k] for k in sorted(long)).split('\0')[0].rstrip('\uffff')
            if not name:
                name=bytes(item[:8]).decode('ascii').rstrip()
                extension=bytes(item[8:11]).decode('ascii').rstrip()
                if extension:name+='.'+extension
            long={}
            first=struct.unpack_from('<H',item,20)[0]<<16|struct.unpack_from('<H',item,26)[0]
            yield name,off,first,struct.unpack_from('<I',item,28)[0],item[11],bytes(item[:11])

    def find(self, directory, name):
        found=[e for e in self.entries(directory) if e[0].lower()==name.lower()]
        if len(found)>1:raise ValueError('ambiguous EFI path')
        return found[0] if found else None

    def directory(self, parts, create=False):
        current=self.root
        for name in parts:
            entry=self.find(current,name)
            if not entry:
                if not create:raise ValueError('missing EFI directory: '+name)
                first=self.allocate(1)[0]
                for index,(dot,parent) in enumerate((('.',first),('..',0 if current==self.root else current))):
                    raw=bytearray(32);raw[:11]=dot.encode().ljust(11,b' ');raw[11]=16
                    struct.pack_into('<H',raw,20,parent>>16);struct.pack_into('<H',raw,26,parent&65535)
                    off=self.offset(first)+index*32;self.data[off:off+32]=raw
                self.add_entry(current,name,first,0,16);entry=self.find(current,name)
            if not entry[4]&16:raise ValueError('non-directory in EFI path')
            current=entry[2]
        return current

    def parts(self,path):
        parts=path.split('/')
        if not parts or any(not p or p in ('.','..') or not p.isascii() or len(p)>240 for p in parts):
            raise ValueError('invalid EFI path')
        return parts

    def add_entry(self,directory,name,first,size,attributes):
        used={e[5] for e in self.entries(directory)}
        stem,separator,extension=name.rpartition('.')
        if not separator:stem,extension=name,''
        stem=re.sub('[^A-Z0-9_]','',stem.upper()) or 'FILE'
        extension=re.sub('[^A-Z0-9_]','',extension.upper())[:3]
        for number in range(1,10000):
            suffix='~'+str(number);short=(stem[:8-len(suffix)]+suffix).ljust(8)+extension.ljust(3)
            short=short.encode('ascii')
            if short not in used:break
        else:raise ValueError('EFI short-name space exhausted')
        checksum=0
        for byte in short:checksum=(((checksum&1)<<7)+(checksum>>1)+byte)&255
        units=name.encode('utf-16le')+b'\0\0';units+=b'\xff\xff'*((-len(units)//2)%13)
        records=[]
        for index in reversed(range(len(units)//26)):
            raw=bytearray(32);raw[0]=index+1|(0x40 if index==len(units)//26-1 else 0)
            raw[11]=15;raw[13]=checksum;chunk=units[index*26:(index+1)*26]
            raw[1:11]=chunk[:10];raw[14:26]=chunk[10:22];raw[28:32]=chunk[22:]
            records.append(raw)
        raw=bytearray(32);raw[:11]=short;raw[11]=attributes
        struct.pack_into('<H',raw,20,first>>16);struct.pack_into('<H',raw,26,first&65535)
        struct.pack_into('<I',raw,28,size);records.append(raw)
        while True:
            run=[];past_end=False;uses_end=False;slots=self.slots(directory)
            for off in slots:
                if not self.data[off]:past_end=True
                if past_end or self.data[off]==0xe5:
                    run.append(off);uses_end|=past_end
                else:run=[]
                if len(run)==len(records):break
            if len(run)==len(records):break
            last=self.chain(directory)[-1];self.link(last,self.allocate(1)[0])
        for off,record in zip(run,records):self.data[off:off+32]=record
        if uses_end and slots.index(run[-1])+1<len(slots):
            off=slots[slots.index(run[-1])+1];self.data[off:off+32]=bytes(32)

    def read(self,path):
        parts=self.parts(path);entry=self.find(self.directory(parts[:-1]),parts[-1])
        if not entry or entry[4]&24:raise ValueError('missing EFI regular file: '+path)
        return b''.join(self.data[self.offset(c):self.offset(c)+self.cluster]
                        for c in self.chain(entry[2]))[:entry[3]] if entry[3] else b''

    def write(self,path,content,replace=False):
        parts=self.parts(path);parent=self.directory(parts[:-1],create=True);entry=self.find(parent,parts[-1])
        if entry and (not replace or entry[4]&24):raise ValueError('EFI output already exists or is not regular: '+path)
        if len(content)>0xffffffff:raise ValueError('EFI file exceeds FAT32 limit')
        blocks=self.chain(entry[2]) if entry and entry[2] else []
        needed=(len(content)+self.cluster-1)//self.cluster
        extra=self.allocate(max(0,needed-len(blocks)))
        if blocks and extra:self.link(blocks[-1],extra[0])
        blocks+=extra
        for index,c in enumerate(blocks):
            off=self.offset(c);self.data[off:off+self.cluster]=content[index*self.cluster:(index+1)*self.cluster].ljust(self.cluster,b'\0')
        first=blocks[0] if blocks else 0
        if entry:
            struct.pack_into('<H',self.data,entry[1]+20,first>>16)
            struct.pack_into('<H',self.data,entry[1]+26,first&65535)
            struct.pack_into('<I',self.data,entry[1]+28,len(content))
        else:self.add_entry(parent,parts[-1],first,len(content),32)
