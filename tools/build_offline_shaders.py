#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Experimental public-source Cg->GXP build. No firmware or runtime compiler.

Only locked authored sources/profiles are accepted. Outputs are structurally
validated and deterministic, not hardware-qualified. Network is opt-in --fetch.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import subprocess
import tempfile
import urllib.request
from gxp_validate import parse_gxp

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / 'tools/shader_compiler.lock.json'
DEPENDENCIES = ROOT / 'tools/shader_dependencies.lock.json'
PROGRAMS = ROOT / 'graphics/shaders/programs.lock.json'
PATCH = ROOT / 'patches/open-shacccg-host-compat.patch'
CACHE = ROOT / '.cache/offline-shaders'
OUTPUT = ROOT / 'build/offline-shaders'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def locked(path):
    return json.loads(path.read_text())


def fingerprint():
    files = [COMPILER, DEPENDENCIES, PROGRAMS, PATCH,
             ROOT/'tools/shader_host/main.cpp', ROOT/'tools/shader_host/CMakeLists.txt',
             ROOT/'tools/gxp_validate.py', Path(__file__).resolve()]
    return {str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in files}


def source_bytes(record):
    relative = PurePosixPath(record['source'])
    if relative.is_absolute() or '..' in relative.parts or relative.parts[:2] != ('graphics', 'shaders'):
        raise ValueError('source_outside_authored_directory')
    path = ROOT / str(relative)
    if not path.resolve().is_relative_to((ROOT/'graphics/shaders').resolve()):
        raise ValueError('source_symlink_escape')
    data = path.read_bytes()
    if not 0 < len(data) <= 65536 or sha(data) != record['source_sha256']:
        raise ValueError('authored_source_hash_mismatch')
    return data


def verify_program(name, record, data):
    reflection = parse_gxp(data, record['stage'])
    if len(data) != record['gxp_size'] or sha(data) != record['gxp_sha256']:
        raise ValueError('unexpected_gxp_bytes:' + name)
    if reflection['parameters'] != record['parameters']:
        raise ValueError('unexpected_shader_interface:' + name)
    return reflection


def header(name, data):
    rows = ['    '+','.join('0x%02x' % b for b in data[i:i+16])+',' for i in range(0,len(data),16)]
    return ('/* Generated from locked authored source; hardware acceptance unverified. */\n'
            '_Alignas(16) static const unsigned char '+name+'[] = {\n'+'\n'.join(rows)+'\n};\n').encode()


def verify_bundle(output=OUTPUT):
    manifest = locked(PROGRAMS)
    if manifest['options'] != dict(optimization_level=0, fast_math=0, fast_precision=0, fast_int=0):
        raise ValueError('unsafe_compile_options')
    for record in [*manifest['programs'].values(), *manifest['negative_controls'].values()]:
        source_bytes(record)
    bundle = locked(output/'bundle.json')
    if (bundle.get('inputs') != fingerprint() or bundle.get('quality') != manifest['quality'] or
        bundle.get('compiler_revision') != manifest['compiler_revision'] or
        bundle.get('programs') != list(manifest['programs']) or bundle.get('bundle_version') != 1):
        raise ValueError('stale_shader_bundle')
    for name, record in manifest['programs'].items():
        data = (output/(name+'.gxp')).read_bytes()
        verify_program(name, record, data)
        if (output/(name+'.h')).read_bytes() != header(name, data):
            raise ValueError('generated_header_mismatch:' + name)
    return bundle


def fetch(path, url, expected, size, allow_fetch):
    if path.exists() and not path.is_file():
        raise ValueError('dependency_not_regular_file')
    if path.is_file():
        if path.stat().st_size != size:
            raise ValueError('cached_public_dependency_size_mismatch')
        data = path.read_bytes()
    else:
        if not allow_fetch:
            raise ValueError('missing_public_dependency_rerun_with_fetch')
        request = urllib.request.Request(url, headers={'User-Agent': 'TSFP-offline-source-build'})
        with urllib.request.urlopen(request, timeout=30) as stream:
            data = stream.read(size+1)
        if len(data) != size or sha(data) != expected:
            raise ValueError('download_hash_mismatch')
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(path.name+'.part')
        temporary.write_bytes(data); os.replace(temporary, path)
    if len(data) != size or sha(data) != expected:
        raise ValueError('cached_public_dependency_hash_mismatch')
    return data


def run(command, **kwargs):
    print('+', ' '.join(str(p) for p in command), flush=True)
    subprocess.run([str(p) for p in command], check=True, cwd=ROOT, timeout=300,
                   env={**os.environ, 'LC_ALL': 'C'}, **kwargs)


def build(allow_fetch=False):
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64'):
        raise ValueError('host_profile_requires_linux_amd64')
    compiler, dependencies, programs = locked(COMPILER), locked(DEPENDENCIES), locked(PROGRAMS)
    inputs = fingerprint()
    if (compiler['url'] != 'https://github.com/frangarcj/open-shacccg' or compiler['license'] != 'MIT' or
        not re.fullmatch(r'[0-9a-f]{40}', compiler['revision'])):
        raise ValueError('invalid_public_compiler_pin')
    for name in [*programs['programs'], *programs['negative_controls']]:
        if not re.fullmatch(r'[a-z][a-z0-9_]*', name):
            raise ValueError('invalid_shader_identifier')
    if programs['compiler_revision'] != compiler['revision']:
        raise ValueError('compiler_pin_mismatch')
    if programs['options'] != dict(optimization_level=0, fast_math=0, fast_precision=0, fast_int=0):
        raise ValueError('unsafe_compile_options')
    authored = {record['source']: source_bytes(record) for record in
                [*programs['programs'].values(), *programs['negative_controls'].values()]}
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='shader-stage-', dir=OUTPUT.parent) as temporary:
        stage = Path(temporary); source = stage/'source'; prefix = stage/'prefix'
        shader_sources = stage/'authored'; shader_sources.mkdir()
        for source_name, data in authored.items():
            (shader_sources/Path(source_name).name).write_bytes(data)
        for name, entry in compiler['files'].items():
            relative = PurePosixPath(name)
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('invalid_compiler_source_path')
            data = fetch(CACHE/'source'/compiler['revision']/name,
                'https://raw.githubusercontent.com/frangarcj/open-shacccg/'+compiler['revision']+'/'+name,
                entry['sha256'], entry['size'], allow_fetch)
            target = source/name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
        run(['patch', '--batch', '--fuzz=0', '-p1', '-d', source, '-i', PATCH])
        for package in dependencies['packages']:
            if not package['url'].startswith('https://snapshot.debian.org/archive/debian/20260925T181338Z/'):
                raise ValueError('invalid_public_dependency_url')
            archive = CACHE/'packages'/(package['sha256']+'.deb')
            fetch(archive, package['url'], package['sha256'], package['size'], allow_fetch)
            run(['dpkg-deb', '-x', archive, prefix]) # data only; no package scripts/system install
            if not (prefix/package['license_file']).is_file():
                raise ValueError('dependency_license_missing')
        host = stage/'host'
        run(['cmake', '-S', ROOT/'tools/shader_host', '-B', host, '-G', 'Ninja',
             '-DTSFP_OPENSHACCCG_SOURCE='+str(source), '-DCMAKE_PREFIX_PATH='+str(prefix/'usr'),
             '-DTSFP_SHADER_PREFIX='+str(prefix/'usr'), '-DCMAKE_TOOLCHAIN_FILE=',
             '-DCMAKE_CXX_FLAGS=-O2 -ffp-contract=off'])
        run(['cmake', '--build', host, '--target', 'tsfp_shader_compile', '--parallel', '2'])
        executable = host/'tsfp_shader_compile'; generated = stage/'generated'; generated.mkdir()
        for name, record in programs['programs'].items():
            output = generated/(name+'.gxp')
            run([executable, record['stage'], shader_sources/Path(record['source']).name, output])
            data = output.read_bytes(); verify_program(name, record, data)
            again = generated/(name+'.repeat.gxp')
            run([executable, record['stage'], shader_sources/Path(record['source']).name, again])
            if again.read_bytes() != data:
                raise ValueError('nondeterministic_shader_output')
            (generated/(name+'.h')).write_bytes(header(name, data))
        for name, record in programs['negative_controls'].items():
            output = generated/(name+'.gxp')
            result = subprocess.run([str(executable), record['stage'], str(shader_sources/Path(record['source']).name), str(output)],
                                    cwd=ROOT, timeout=30, capture_output=True, text=True)
            if result.returncode == 0 or output.exists() or 'severity=2' not in result.stderr:
                raise ValueError('shader_negative_control_failed:' + name)
            print('Rejected authored negative control:', name, flush=True)
        # Nothing is published on compile/validation failure. Bundle marker is
        # written last; verify-only rechecks every source, program and header.
        if fingerprint() != inputs:
            raise ValueError('pipeline_inputs_changed_during_build')
        for record in [*programs['programs'].values(), *programs['negative_controls'].values()]:
            source_bytes(record)
        OUTPUT.mkdir(parents=True, exist_ok=True)
        for name in programs['programs']:
            for suffix in ('.gxp', '.h'):
                os.replace(generated/(name+suffix), OUTPUT/(name+suffix))
        bundle = {'bundle_version': 1, 'inputs': inputs, 'quality': programs['quality'],
                  'compiler_revision': compiler['revision'], 'programs': list(programs['programs'])}
        marker = OUTPUT/'bundle.json.part'; marker.write_text(json.dumps(bundle, indent=2)+'\n')
        os.replace(marker, OUTPUT/'bundle.json')
    verify_bundle()
    print('Locked authored GXP compilation/reflection/determinism passed; GPU hardware NOT RUN')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true', help='download hash-locked public source/dev packages only')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args(argv)
    try:
        if args.verify_only:
            verify_bundle()
            print('Shader bundle/source/interface integrity passed; GPU hardware NOT RUN')
        else:
            build(args.fetch)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print('Offline shader pipeline FAILED:', error)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
