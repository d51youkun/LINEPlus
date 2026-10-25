#!/usr/bin/env python3
"""Inject the independently built library into a copy; preserve the source IPA."""
import argparse
import hashlib
import json
import plistlib
import struct
import zipfile
from pathlib import Path

LOAD_PATH = b'@executable_path/Frameworks/LINEPlus.dylib\0'


def inject(data):
    data = bytearray(data)
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError('Expected arm64 thin Mach-O')
    cpu, = struct.unpack_from('<I', data, 4)
    if cpu != 0x100000c:
        raise ValueError('Expected arm64 CPU')
    count, size = struct.unpack_from('<II', data, 16)
    commands = []
    pos = 32
    first_section = len(data)
    for _ in range(count):
        command, length = struct.unpack_from('<II', data, pos)
        if length < 8 or pos + length > 32 + size:
            raise ValueError('Invalid Mach-O load command')
        if command == 0x2c and struct.unpack_from('<I', data, pos + 16)[0] != 0:
            raise ValueError('Encrypted executable; a decrypted source IPA is required')
        if command == 0xc and LOAD_PATH in data[pos:pos + length]:
            raise ValueError('LINEPlus already injected')
        if command == 0x19:
            sections, = struct.unpack_from('<I', data, pos + 64)
            for i in range(sections):
                off, = struct.unpack_from('<I', data, pos + 72 + 80 * i + 48)
                if off:
                    first_section = min(first_section, off)
        if command != 0x1d:  # original code signature is invalid after modification
            commands.append(bytes(data[pos:pos + length]))
        pos += length
    length = (24 + len(LOAD_PATH) + 7) & ~7
    command = struct.pack('<6I', 0xc, length, 24, 0, 0x10000, 0x10000) + LOAD_PATH
    commands.append(command.ljust(length, b'\0'))
    blob = b''.join(commands)
    end = 32 + len(blob)
    if end > first_section:
        raise ValueError('Insufficient Mach-O header padding')
    if end > 32 + size and any(data[32 + size:end]):
        raise ValueError('Would overwrite nonzero header padding')
    data[32:max(end, 32 + size)] = blob.ljust(max(len(blob), size), b'\0')
    struct.pack_into('<II', data, 16, len(commands), len(blob))
    return bytes(data)


def package(source, library, destination):
    if source.resolve() == destination.resolve():
        raise ValueError('Output must differ from input')
    if destination.exists():
        raise ValueError(f'Output already exists: {destination}')
    with zipfile.ZipFile(source) as archive:
        info_name = next(n for n in archive.namelist() if n.startswith('Payload/') and n.count('/') == 2 and n.endswith('/Info.plist'))
        info = plistlib.loads(archive.read(info_name))
        if info.get('CFBundleIdentifier') != 'jp.naver.line':
            raise ValueError('Expected the original LINE bundle identifier jp.naver.line')
        if info['CFBundleShortVersionString'] != '26.16.0':
            raise ValueError('Only LINE 26.16.0 is supported')
        app = info_name.rsplit('/', 1)[0]
        executable = app + '/' + info['CFBundleExecutable']
        original = archive.read(executable)
        patched = inject(original)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for entry in archive.infolist():
                if '/_CodeSignature/' in entry.filename or entry.filename.endswith('/embedded.mobileprovision'):
                    continue
                output.writestr(entry, patched if entry.filename == executable else archive.read(entry))
            output.write(library, app + '/Frameworks/LINEPlus.dylib')
    manifest = {
        'input': str(source), 'output': str(destination), 'line_version': info['CFBundleShortVersionString'],
        'original_executable_sha256': hashlib.sha256(original).hexdigest(),
        'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
        'output_sha256': hashlib.file_digest(destination.open('rb'), 'sha256').hexdigest(),
        'signing': 'unsigned; must be re-signed by SideStore/AltStore',
        'status': 'development preview; not all requested features are implemented; no device validation',
    }
    destination.with_suffix('.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2))
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('ipa', type=Path)
    parser.add_argument('--library', type=Path, default=Path('build/LINEPlus.dylib'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package(args.ipa, args.library, args.output)
