"""V30 durable journal storage contract.

The implementation uses only append/read/flush operations also available from
ESP32 Arduino FS File objects (SPIFFS/LittleFS). It never formats or truncates
an existing journal. Missing, corrupt, and partial-tail states are distinct.
"""
from __future__ import annotations
import os, struct, zlib
from pathlib import Path
MAGIC=b"VJ01"; VERSION=1; HEADER=struct.Struct("<4sB3xQI")
class JournalError(Exception): pass
class JournalMissing(JournalError): pass
class JournalCorrupt(JournalError): pass
class JournalPartial(JournalError): pass
class JournalFull(JournalError): pass
class V30JournalStorage:
    def __init__(self,path,max_bytes=0x20000):
        self.path=Path(path); self.max_bytes=max_bytes
        if max_bytes<=HEADER.size: raise ValueError("capacity too small")
    def _scan(self):
        if not self.path.exists(): raise JournalMissing(str(self.path))
        data=self.path.read_bytes(); pos=0; records=[]
        while pos<len(data):
            if len(data)-pos<HEADER.size:
                raise JournalPartial("truncated record header") if data[pos:pos+4] == MAGIC else JournalCorrupt("invalid journal prefix")
            magic,version,seq,length=HEADER.unpack_from(data,pos)
            if magic!=MAGIC or version!=VERSION or length>self.max_bytes: raise JournalCorrupt("invalid record header")
            end=pos+HEADER.size+length+4
            if end>len(data): raise JournalPartial("truncated record payload")
            payload=data[pos+HEADER.size:pos+HEADER.size+length]
            crc,=struct.unpack_from("<I",data,pos+HEADER.size+length)
            if zlib.crc32(data[pos:pos+HEADER.size]+payload)&0xffffffff != crc: raise JournalCorrupt("record CRC mismatch")
            records.append((seq,payload)); pos=end
        return records
    def open(self):
        self._scan(); return self
    def append(self,sequence,payload):
        if not isinstance(sequence,int) or sequence<0 or sequence>0xffffffffffffffff: raise ValueError("sequence out of range")
        payload=bytes(payload); record=HEADER.pack(MAGIC,VERSION,sequence,len(payload))+payload
        record+=struct.pack("<I",zlib.crc32(record)&0xffffffff)
        current=self.path.stat().st_size if self.path.exists() else 0
        if current+len(record)>self.max_bytes: raise JournalFull("journal capacity exhausted")
        self.path.parent.mkdir(parents=True,exist_ok=True)
        with self.path.open("ab") as f:
            f.write(record); f.flush(); os.fsync(f.fileno())
    def read(self):
        return list(self._scan())
    def flush(self):
        if not self.path.exists(): raise JournalMissing(str(self.path))
        with self.path.open("rb") as f: os.fsync(f.fileno())
