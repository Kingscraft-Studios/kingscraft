#!/usr/bin/env python3
import sys
import struct

REGION_MAGIC = b'KCR\x01'
CHUNK_MAGIC = b'KCH\x01'
REGION_SIZE = 32
CHUNK_STRIDE = REGION_SIZE * REGION_SIZE
REGION_HEADER_SIZE = 32
DIR_ENTRY_SIZE = 24
CHUNK_HEADER_SIZE = 22

def read_u8(b, off):
    return b[off], off + 1

def read_u16(b, off):
    return struct.unpack('<H', b[off:off+2])[0], off + 2

def read_u32(b, off):
    return struct.unpack('<I', b[off:off+4])[0], off + 4

def read_u64(b, off):
    return struct.unpack('<Q', b[off:off+8])[0], off + 8

def read_i32(b, off):
    return struct.unpack('<i', b[off:off+4])[0], off + 4

def main():
    if len(sys.argv) < 2:
        print("Usage: kcr_to_text.py <file.kcr> [output.txt]")
        sys.exit(1)
    
    path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else path + ".txt"
    
    with open(path, 'rb') as f:
        data = f.read()
    
    lines = []
    lines.append(f"# KCR Dump: {path}")
    lines.append(f"# File size: {len(data)} bytes")
    lines.append("")
    
    off = 0
    magic = data[off:off+4]; off += 4
    lines.append(f"[HEADER] magic={magic}")
    version, off = read_u16(data, off)
    lines.append(f"[HEADER] version={version}")
    rx, off = read_i32(data, off)
    rz, off = read_i32(data, off)
    lines.append(f"[HEADER] regionX={rx} regionZ={rz}")
    chunkCount, off = read_u32(data, off)
    lines.append(f"[HEADER] chunkCount={chunkCount}")
    off = REGION_HEADER_SIZE
    lines.append("")
    
    lines.append("[DIRECTORY]")
    for i in range(CHUNK_STRIDE):
        offset, off = read_u64(data, off)
        size, off = read_u32(data, off)
        rawSize, off = read_u32(data, off)
        flags, off = read_u8(data, off)
        stored_crc, off = read_u32(data, off)
        off += 3
        if flags & 0x01:
            lx = i % REGION_SIZE
            lz = i // REGION_SIZE
            cx = rx * REGION_SIZE + lx
            cz = rz * REGION_SIZE + lz
            lines.append(f"  slot={i:4d} (cx={cx:4d},cz={cz:4d}) offset={offset:10d} size={size:8d} rawSize={rawSize:8d} flags=0x{flags:02X} crc={stored_crc:08X}")
    lines.append("")
    
    lines.append("[RECORDS]")
    off_dir = REGION_HEADER_SIZE
    for i in range(CHUNK_STRIDE):
        offset, off_dir2 = read_u64(data, off_dir)
        size, off_dir2 = read_u32(data, off_dir2)
        rawSize, off_dir2 = read_u32(data, off_dir2)
        flags, off_dir2 = read_u8(data, off_dir2)
        stored_crc, off_dir2 = read_u32(data, off_dir2)
        off_dir = off_dir2 + 3
        if flags & 0x01 and size > 0 and offset > 0:
            lx = i % REGION_SIZE
            lz = i // REGION_SIZE
            cx = rx * REGION_SIZE + lx
            cz = rz * REGION_SIZE + lz
            lines.append(f"--- Record slot={i} (cx={cx},cz={cz}) at offset={offset} size={size} ---")
            if offset + size <= len(data):
                rec = data[offset:offset+size]
                if rec[:4] == CHUNK_MAGIC:
                    roff = 4
                    cver, roff = read_u16(rec, roff)
                    ccx, roff = read_i32(rec, roff)
                    ccz, roff = read_i32(rec, roff)
                    cstate, roff = read_u8(rec, roff)
                    cflags, roff = read_u8(rec, roff)
                    pver, roff = read_u16(rec, roff)
                    psize, roff = read_u32(rec, roff)
                    lines.append(f"  KCH: ver={cver} chunk=({ccx},{ccz}) state={cstate} flags=0x{cflags:02X} pver={pver} psize={psize}")
                    payload = rec[roff:roff+psize]
                    lines.append(f"  PAYLOAD ({len(payload)} bytes):")
                    if len(payload) >= 10:
                        pmagic = struct.unpack('<I', payload[0:4])[0]
                        if pmagic == 0x4B435342:
                            pver2 = struct.unpack('<H', payload[4:6])[0]
                            pcount = struct.unpack('<I', payload[6:10])[0]
                            lines.append(f"    [BINARY SPARSE] magic=KCSB version={pver2} count={pcount}")
                            poff = 10
                            for j in range(pcount):
                                if poff + 12 > len(payload):
                                    break
                                idx = struct.unpack('<I', payload[poff:poff+4])[0]
                                bid = struct.unpack('<Q', payload[poff+4:poff+12])[0]
                                poff += 12
                                lines.append(f"    entry[{j}]: idx={idx} block={bid:016x}")
                            continue
                    # Try hex dump
                    lines.append(f"    {payload[:64].hex()}")
                else:
                    lines.append(f"  RAW: {rec[:32].hex()}")
            lines.append("")
    
    with open(out_path, 'w') as f:
        f.write('\n'.join(lines))
    
    print(f"Wrote {out_path}")

if __name__ == '__main__':
    main()
