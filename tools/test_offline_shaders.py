#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Failure controls over genuinely source-generated authored GXP, not GPU stubs."""
import copy
import json
from pathlib import Path
import shutil
import tempfile
from unittest.mock import patch
import build_offline_shaders as pipeline


def rejects(callback):
    try:
        callback()
    except (OSError, ValueError):
        return
    raise RuntimeError('Offline shader negative control was accepted')


def main():
    pipeline.verify_bundle()
    manifest = pipeline.locked(pipeline.PROGRAMS)
    for name, record in manifest['programs'].items():
        data = (pipeline.OUTPUT/(name+'.gxp')).read_bytes()
        pipeline.verify_program(name, record, data)
        corrupt = bytearray(data); corrupt[0] ^= 1
        rejects(lambda: pipeline.verify_program(name, record, corrupt))
        wrong = copy.deepcopy(record); wrong['parameters'][0]['components'] = 3
        rejects(lambda: pipeline.verify_program(name, wrong, data))
        wrong_stage = dict(record, stage='fragment' if record['stage']=='vertex' else 'vertex')
        rejects(lambda: pipeline.verify_program(name, wrong_stage, data))
    with tempfile.TemporaryDirectory(dir=pipeline.ROOT/'build') as directory:
        output = Path(directory)/'bundle'; shutil.copytree(pipeline.OUTPUT, output)
        marker = output/'bundle.json'; document = json.loads(marker.read_text())
        document['inputs'] = {}; marker.write_text(json.dumps(document))
        rejects(lambda: pipeline.verify_bundle(output))
        shutil.copyfile(pipeline.OUTPUT/'bundle.json', marker)
        h = output/'stream_texture_v.h'; h.write_text(h.read_text()+'/* changed */\n')
        rejects(lambda: pipeline.verify_bundle(output))
        with patch.object(pipeline, 'ROOT', Path(directory)):
            source = Path(directory)/'graphics/shaders/authored.cg'; source.parent.mkdir(parents=True)
            source.write_bytes(b'authored source')
            record = {'source':'graphics/shaders/authored.cg','source_sha256':pipeline.sha(source.read_bytes())}
            pipeline.source_bytes(record)
            source.write_bytes(b'changed'); rejects(lambda: pipeline.source_bytes(record))
    print('Source-generated shader corruption/stage/interface/staleness/header/source negative controls pass; GPU NOT RUN')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
