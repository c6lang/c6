#!/usr/bin/env python3
"""把 payload.elf 的 PT_LOAD 段摊平成一段从 base 开始的二进制。

引导器只会"把字节搬到 base，然后跳 entry"，所以这里做的事和真装载器一样：
按程序头把每个段放到它的虚拟地址上，中间的空隙补 0。

    flat.py payload.elf payload.bin 0x400000
    -> 打印: <入口地址> <文件字节数> <内存字节数>

（不用 objcopy -O binary：它是按**节**的最低地址当基准的，而 tcc 链接出来的
ELF 头 0x498 字节占着 0x400000..0x400498，会让整段内容整体错位。）
"""
import struct
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit("用法: flat.py <elf> <bin> <base>")
    src, dst, base = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)

    data = open(src, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        sys.exit("%s 不是 ELF64" % src)

    (e_entry,) = struct.unpack_from("<Q", data, 0x18)
    (e_phoff,) = struct.unpack_from("<Q", data, 0x20)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)

    segs = []
    mem_top = 0
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (p_type, _flags, p_offset, p_vaddr, _paddr,
         p_filesz, p_memsz) = struct.unpack_from("<IIQQQQQ", data, off)
        if p_type != 1:                                   # PT_LOAD
            continue
        if p_memsz:
            mem_top = max(mem_top, p_vaddr + p_memsz)
        if p_filesz:
            segs.append((p_vaddr, data[p_offset:p_offset + p_filesz]))

    segs.sort()
    out = bytearray()
    for vaddr, blob in segs:
        if vaddr < base:
            sys.exit("段地址 %#x 低于 base %#x" % (vaddr, base))
        pad = vaddr - base - len(out)
        if pad < 0:
            sys.exit("段重叠 %#x" % vaddr)
        out += bytes(pad) + blob
    open(dst, "wb").write(bytes(out))

    print("%#x %d %d" % (e_entry, len(out), max(mem_top - base, len(out))))


main()
