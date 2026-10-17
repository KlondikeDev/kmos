# This file has been generated in whole or in part by a Large Language Model
"""Write a small SSDT battery fixture for QEMU: OUTPUT [--low].

These literal firmware methods test the real AML-to-native-query path without
pretending QEMU supplies a physical battery or testing laptop EC hardware.
"""
import struct
import sys
from pathlib import Path

def package(op, body):
    for width in range(1, 5):
        length = len(body) + width
        if length < (64 if width == 1 else 1 << (4 + 8 * (width - 1))):
            if width == 1:
                encoded = bytes([length])
            else:
                encoded = bytes([((width-1) << 6) | (length & 15)])
                encoded += bytes((length >> (4 + 8*i)) & 255 for i in range(width-1))
            return op + encoded + body
    raise ValueError("AML package too large")

def integer(value):
    return bytes([value]) if value in (0, 1) else b'\x0c' + struct.pack('<I', value)

def string(value):
    return b'\x0d' + value.encode('ascii') + b'\x00'

def name(key, value):
    return b'\x08' + key.encode('ascii') + value

def method(key, result):
    return package(b'\x14', key.encode('ascii') + b'\x00\xa4' + result)

def values(items):
    return package(b'\x12', bytes([len(items)]) + b''.join(items))

low = '--low' in sys.argv[2:]
bif = [integer(v) for v in [0, 60000, 50000, 1, 12000, 5000, 1000, 1, 1]]
bif += [string(v) for v in ['Test battery', '0001', 'LION', 'Kunix tests']]
bst = [integer(v) for v in [5 if low else 1, 10000, 2500 if low else 25000, 12000]]
battery = package(b'\x5b\x82', b'BAT0' + name('_HID', string('PNP0C0A')) +
                  name('_STA', integer(31)) + method('_BIF', values(bif)) + method('_BST', values(bst)))
adapter = package(b'\x5b\x82', b'AC00' + name('_HID', string('ACPI0003')) +
                  name('_STA', integer(15)) + method('_PSR', integer(0)))
body = package(b'\x10', b'\\_SB_' + battery + adapter)
header = struct.pack('<4sIBB6s8sI4sI', b'SSDT', 36+len(body), 2, 0, b'KUNIX ', b'BATTEST ', 1, b'KNIX', 1)
table = bytearray(header + body)
table[9] = (-sum(table)) & 255
assert len(table) == struct.unpack_from('<I', table, 4)[0] and sum(table) % 256 == 0
Path(sys.argv[1]).write_bytes(table)
