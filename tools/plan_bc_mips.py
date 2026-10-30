#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Asset-free Xbox BC1/BC2/BC3 mip extents for offline Vita texture planning.

Does not decode textures, manipulate game data, or perform GPU operations.
All sizes are validated before the first output record is returned.
"""
import argparse
import json

MAX_DIM = 4096
MAX_CHAIN_BYTES = 32 * 1024 * 1024


def plan_mips(width: int, height: int, block_bytes: int, levels: int = 0):
    if any(type(v) is not int for v in (width, height, block_bytes, levels)):
        raise ValueError('invalid_type')
    if (width < 1 or height < 1 or width > MAX_DIM or height > MAX_DIM
            or width & (width - 1) or height & (height - 1)
            or block_bytes not in (8, 16) or levels < 0 or levels > 13):
        raise ValueError('invalid_profile')
    full_count = max(width.bit_length(), height.bit_length())
    count = levels or full_count
    if count > full_count:
        raise ValueError('excess_mip_levels')
    entries = []
    offset = 0
    for level in range(count):
        w = max(1, width >> level)
        h = max(1, height >> level)
        size = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block_bytes
        if size > MAX_CHAIN_BYTES - offset:
            raise ValueError('chain_size_limit')
        entries.append({'level': level, 'width': w, 'height': h,
                        'offset': offset, 'size': size})
        offset += size
    return {'block_bytes': block_bytes, 'total_bytes': offset, 'levels': entries,
            'profile': 'asset_free_linear_mip_packing'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--width', type=int, required=True)
    parser.add_argument('--height', type=int, required=True)
    parser.add_argument('--block-bytes', type=int, required=True, choices=(8, 16))
    parser.add_argument('--levels', type=int, default=0,
                        help='0 means all levels down to 1x1')
    args = parser.parse_args(argv)
    try:
        print(json.dumps(plan_mips(args.width, args.height,
                                   args.block_bytes, args.levels), sort_keys=True))
    except ValueError as error:
        print(json.dumps({'status': 'rejected', 'reason': str(error)}, sort_keys=True))
        return 3
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
