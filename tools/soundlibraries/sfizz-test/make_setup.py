# a Vst3Plugin setup (vst3plugin.cpp "MSV3" format) holding sfizz's state with an SFZ loaded,
# as if loaded in sfizz's editor and saved from View > Sound Library
import struct, sys
sfz, out = sys.argv[1], sys.argv[2]
def str8(s): b=s.encode()+b'\0'; return struct.pack('<i',len(b))+b
comp = (struct.pack('<Q',5) + str8(sfz) + struct.pack('<f',0.0) + struct.pack('<iii',64,0,8192)
        + str8('') + struct.pack('<i',60) + struct.pack('<ff',440.0,0.0) + struct.pack('<ii',2,1)
        + struct.pack('<ii',10,3) + struct.pack('<h',0) + struct.pack('<i',-1) + struct.pack('<I',0))
name='sfizz'.encode('utf-16-be')
data = b'MSV3' + struct.pack('>I',1) + struct.pack('>I',len(name)) + name + struct.pack('>I',len(comp)) + comp + struct.pack('>I',0)
open(out,'wb').write(data)
