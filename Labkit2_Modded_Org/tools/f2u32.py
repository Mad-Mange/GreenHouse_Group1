#!/usr/bin/env python3

import sys
import struct

if(len(sys.argv) < 2):
	print('Usage: {} <float>'.format(sys.argv[0]))

value = float(sys.argv[1])
data = struct.pack('<f', value)

print(struct.unpack('<I', data)[0])
