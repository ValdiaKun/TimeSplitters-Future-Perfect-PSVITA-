#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install the hash-locked Linux x86_64 SDK into ignored .cache/toolchains.

Existing SDK trees are never overwritten. Download hashes are checked before
extraction. Package release URLs may move; changed bytes fail closed.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
from prepare_upstream import ROOT

def install():
    lock = json.loads((ROOT / 'sources.lock.json').read_text())['vitasdk']
    parent = ROOT / '.cache/toolchains'
    destination = parent / 'vitasdk'
    receipt = parent / 'sdk-receipt.json'
    if destination.exists():
        if not receipt.exists() or json.loads(receipt.read_text()) != lock:
            raise RuntimeError(f'Existing SDK has no matching receipt; preserve and inspect {destination}')
        print(destination)
        return
    parent.mkdir(parents=True, exist_ok=True)
    staging = parent / 'sdk-staging'
    staging.mkdir()  # A failed staging tree needs inspection, never overwrite it.
    for index, entry in enumerate([lock, *lock['packages']]):
        archive = parent / ('sdk.tar.bz2' if index == 0 else entry['name'] + '.tar.xz')
        if not archive.exists():
            # curl honors the session's proxy and CA configuration.
            subprocess.run(['curl', '--fail', '--location', '--retry', '2',
                            '--output', str(archive), entry['url']], check=True)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError(f'Checksum mismatch; refusing to extract {archive}')
        with tarfile.open(archive) as stream:
            stream.extractall(staging if index == 0 else staging / 'vitasdk/arm-vita-eabi', filter='data')
    (staging / 'vitasdk').rename(destination)
    staging.rmdir()
    receipt.write_text(json.dumps(lock, indent=2) + '\n')
    print(destination)

if __name__ == '__main__':
    install()
