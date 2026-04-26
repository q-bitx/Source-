#!/usr/bin/env python3
import sys

def read_varint(b, o):
    x = b[o]
    if x < 0xfd:
        return x, o + 1
    if x == 0xfd:
        return int.from_bytes(b[o+1:o+3], "little"), o + 3
    if x == 0xfe:
        return int.from_bytes(b[o+1:o+5], "little"), o + 5
    return int.from_bytes(b[o+1:o+9], "little"), o + 9

def write_varint(x):
    if x < 0xfd:
        return bytes([x])
    if x <= 0xffff:
        return b"\xfd" + x.to_bytes(2, "little")
    if x <= 0xffffffff:
        return b"\xfe" + x.to_bytes(4, "little")
    return b"\xff" + x.to_bytes(8, "little")

raw = sys.argv[1].strip()
mode = sys.argv[2].strip()
b = bytes.fromhex(raw)

# version
o = 4

# segwit marker/flag
if b[o:o+2] != b"\x00\x01":
    raise SystemExit("not segwit tx")
o += 2

vin_count, o = read_varint(b, o)
if vin_count != 1:
    raise SystemExit("script supports 1 input only")

vin_start = o
# prevout 36
o += 36
script_len, script_len_pos_end = read_varint(b, o)
script_len_pos = o
o = script_len_pos_end
script_start = o
script_end = o + script_len
o = script_end
# sequence
o += 4
vin_end = o

vout_count, o = read_varint(b, o)
for _ in range(vout_count):
    o += 8
    sl, o = read_varint(b, o)
    o += sl
witness_start = o

stack_count, o2 = read_varint(b, o)
items = []
for _ in range(stack_count):
    ln, o2 = read_varint(b, o2)
    items.append([o2, ln])
    o2 += ln
witness_end = o2
tail = b[witness_end:]

if mode == "badsig":
    if stack_count < 1 or items[0][1] == 0:
        raise SystemExit("no sig")
    m = bytearray(b)
    m[items[0][0]] ^= 1
    print(m.hex())
elif mode == "badpubkey":
    if stack_count < 2 or items[1][1] == 0:
        raise SystemExit("no pubkey")
    m = bytearray(b)
    m[items[1][0]] ^= 1
    print(m.hex())
elif mode == "emptywitness":
    # Valid serialization, invalid witness: keep only first witness item.
    # witness_start points to original stack count.
    first_start, first_len = items[0]
    first_item = write_varint(first_len) + b[first_start:first_start + first_len]
    print((b[:witness_start] + b"\x01" + first_item + b[witness_end:]).hex())
elif mode == "nonemptyscriptsig":
    # replace empty scriptSig with push 0x01
    if script_len != 0:
        raise SystemExit("scriptSig already non-empty")
    new_vin = b[vin_start:script_len_pos] + b"\x01\x51" + b[script_end:vin_end]
    print((b[:vin_start] + new_vin + b[vin_end:]).hex())
else:
    raise SystemExit("mode must be badsig|badpubkey|emptywitness|nonemptyscriptsig")
