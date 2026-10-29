#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare authored C-test XBE fixtures with the pinned PC parser; no retail IO."""
import sys
import struct
from prepare_upstream import ROOT
sys.path.insert(0,str(ROOT/'external/pc/tools/compiler'))
from xbe_image import XBEParser, XBEError
for kind in ('retail','debug'):
    parser=XBEParser(ROOT/f'build/guest/synthetic-{kind}.xbe')
    x=parser.parse()
    assert x.header.entry_point==0x13500 and x.header.kernel_thunk_addr==0x88000
    assert x.header.is_debug==(kind=='debug')
    assert len(x.sections)==12 and x.sections[0].raw_size==68 and x.sections[0].virtual_size==64
    assert x.sections[8].virtual_addr==0x13200 and x.sections[9].virtual_addr==0x132a0
    assert parser.at(0x13200,11)==bytes.fromhex("8b4424048b542408c20800")
    assert parser.at(0x132a0,3)==bytes.fromhex("f7f1c3")
    assert x.sections[10].virtual_addr==0x13900 and x.sections[11].virtual_addr==0x13a00
    assert parser.at(0x13900,5)==bytes.fromhex("bea0000900")
    assert parser.at(0x139a0,4)==bytes.fromhex("8b442404")
    assert parser.at(0x13a00,3)==bytes.fromhex("ff16c3")
    assert x.tls.raw_data_start==0x8c080 and x.tls.raw_data_end==0x8c094
    assert x.tls.zero_fill_size==5 and x.tls.tls_index_addr==0x8c020 and x.tls.callbacks_addr==0x8c060
    assert [item.ordinal for item in x.kernel_imports]==[104,320,258,187]
    raw=parser.at(x.tls.raw_data_start,x.tls.raw_data_end-x.tls.raw_data_start)
    total=((len(raw)+x.tls.zero_fill_size+15)&~15)+4
    assert total==36 and (-(total//4)&0xffffffff)==0xfffffff7
    state=(ROOT/f'build/guest/tls-{kind}-state.bin').read_bytes()
    tib,block,index,thread=struct.unpack_from('<4I',state)
    assert tib==0x89000 and block==total and thread==0x8a000
    assert index==struct.unpack('<I',parser.at(x.tls.tls_index_addr,4))[0]  # PC title-owned index
    body=state[16:]; words=lambda offset: struct.unpack_from('<I',body,offset)[0]
    assert words(0)==0xffffffff and words(4)==tib+64+total and words(0x28)==thread
    assert words(64)==tib+64+total and body[68:64+len(raw)]==raw[4:]
    assert body[64+len(raw):64+total+64]==bytes(total+64-len(raw))
    object_offset=64+total+64
    assert words(object_offset+0x28)==tib+64 and words(object_offset+0x12c)==41
for kind in ('import','tls'):
    try:
        XBEParser(ROOT/f'build/guest/synthetic-bss-{kind}.xbe').parse()
    except XBEError:
        pass
    else:
        raise AssertionError('PC parser accepted non-file-backed metadata')
for kind in ('reversed','truncated'):
    try:
        XBEParser(ROOT/f'build/guest/synthetic-tls-{kind}.xbe').parse()
    except XBEError:
        pass
    else:
        raise AssertionError('PC parser accepted invalid TLS directory')
print('pinned-pc-tls-oracle: layout/index/zero-fill=pass malformed=pass')
print('pinned-pc-xbe-oracle: retail,debug entry/sections/imports=pass malformed-metadata=pass')
