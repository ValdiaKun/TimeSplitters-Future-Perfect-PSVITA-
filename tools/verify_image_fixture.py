#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare authored C-test XBE fixtures with the pinned PC parser; no retail IO."""
import sys
from prepare_upstream import ROOT
sys.path.insert(0,str(ROOT/'external/pc/tools/compiler'))
from xbe_image import XBEParser
for kind in ('retail','debug'):
    x=XBEParser(ROOT/f'build/guest/synthetic-{kind}.xbe').parse()
    assert x.header.entry_point==0x13400 and x.header.kernel_thunk_addr==0x88000
    assert x.header.is_debug==(kind=='debug')
    assert len(x.sections)==6 and x.sections[0].raw_size==68 and x.sections[0].virtual_size==64
    assert [item.ordinal for item in x.kernel_imports]==[104,320,258]
print('pinned-pc-xbe-oracle: retail,debug entry/sections/imports=pass')
