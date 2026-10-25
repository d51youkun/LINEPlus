#!/usr/bin/env python3
"""Read-only IPA/Mach-O inventory; never executes the supplied application."""
import argparse
import hashlib
import json
import plistlib
import re
import struct
import zipfile
from pathlib import Path


def inspect(path):
    with zipfile.ZipFile(path) as archive:
        info_path = next(n for n in archive.namelist() if re.fullmatch(r"Payload/[^/]+/Info.plist", n))
        info = plistlib.loads(archive.read(info_path))
        data = archive.read(info_path.rsplit('/', 1)[0] + '/' + info['CFBundleExecutable'])
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError('Expected thin little-endian 64-bit Mach-O')
    count, size = struct.unpack_from('<II', data, 16)
    commands, sections = [], []
    offset = 32
    for _ in range(count):
        cmd, length = struct.unpack_from('<II', data, offset)
        item = {'command': hex(cmd), 'size': length}
        if cmd == 0x19:
            name, vm, vmsize, fileoff, filesize, _, _, nsects, _ = struct.unpack_from('<16sQQQQiiII', data, offset + 8)
            item.update(name=name.rstrip(b'\0').decode(), vm=vm, fileoff=fileoff, filesize=filesize)
            for i in range(nsects):
                pos = offset + 72 + i * 80
                sect, seg, addr, sz, off = struct.unpack_from('<16s16sQQI', data, pos)
                sections.append({'name': sect.rstrip(b'\0').decode(), 'segment': seg.rstrip(b'\0').decode(), 'addr': addr, 'size': sz, 'offset': off})
        elif cmd == 0x2c:
            item['cryptoff'], item['cryptsize'], item['cryptid'] = struct.unpack_from('<III', data, offset + 8)
        elif cmd in (0xc, 0x80000018, 0x8000001c):
            start = struct.unpack_from('<I', data, offset + 8)[0]
            item['name'] = data[offset + start:offset + length].split(b'\0')[0].decode()
        commands.append(item)
        offset += length
    strings = {}
    for section in sections:
        if section['name'] in ('__objc_methname', '__objc_classname', '__cstring', '__swift5_reflstr'):
            blob = data[section['offset']:section['offset'] + section['size']]
            strings[section['name']] = [s.decode('utf-8', errors='replace') for s in blob.split(b'\0') if s]
    segments = [c for c in commands if 'vm' in c]
    base = next(c['vm'] for c in segments if c['name'] == '__TEXT')
    def canonical(address):
        if address >> 48:
            return base + (address & 0xfffffffff)
        return address
    def position(address):
        address = canonical(address)
        for segment in segments:
            if segment['vm'] <= address < segment['vm'] + segment['filesize']:
                return segment['fileoff'] + address - segment['vm']
        raise ValueError(hex(address))
    def pointer(address):
        return canonical(struct.unpack_from('<Q', data, position(address))[0])
    def text(address):
        pos = position(address)
        return data[pos:data.index(b'\0', pos)].decode('utf-8', errors='replace')
    def methods(address):
        if not address:
            return []
        pos = position(address)
        flags, count = struct.unpack_from('<II', data, pos)
        stride = flags & 0xffff
        result = []
        for i in range(count):
            entry = address + 8 + i * stride
            ep = position(entry)
            if flags & 0x80000000:
                nr, tr, ir = struct.unpack_from('<iii', data, ep)
                name = entry + nr
                if not flags & 0x40000000:
                    name = pointer(name)
                typ = entry + 4 + tr
                imp = entry + 8 + ir
            else:
                name, typ, imp = struct.unpack_from('<QQQ', data, ep)
            result.append({'selector': text(name), 'types': text(typ), 'imp': hex(imp)})
        return result
    classes = []
    for section in sections:
        if section['name'] != '__objc_classlist':
            continue
        for pos in range(section['offset'], section['offset'] + section['size'], 8):
            try:
                address = canonical(struct.unpack_from('<Q', data, pos)[0])
                cp = position(address)
                isa, _, _, _, ro = [canonical(v) for v in struct.unpack_from('<QQQQQ', data, cp)]
                rp = position(ro & ~7)
                name, ml = struct.unpack_from('<QQ', data, rp + 24)
                meta_ro = pointer(isa + 32) & ~7
                classes.append({'name': text(name), 'methods': methods(ml), 'class_methods': methods(pointer(meta_ro + 32))})
            except (ValueError, struct.error):
                continue
    return {
        'version': info['CFBundleShortVersionString'], 'build': info['CFBundleVersion'],
        'minimum_os': info['MinimumOSVersion'], 'bundle_id': info['CFBundleIdentifier'],
        'executable_sha256': hashlib.sha256(data).hexdigest(),
        'commands': commands, 'sections': sections, 'strings': strings, 'classes': classes,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('ipa')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.ipa)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k not in ('commands', 'sections', 'strings', 'classes')}, indent=2))
    print('Encryption:', [c for c in result['commands'] if 'cryptid' in c])
    print('String inventory:', {k: len(v) for k, v in result['strings'].items()})
    print('Objective-C classes:', len(result['classes']))
