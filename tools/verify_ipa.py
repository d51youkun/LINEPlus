#!/usr/bin/env python3
"""Verify that packaging changes only the Mach-O header and adds our library."""
import argparse
import hashlib
import json
import os
import re
import struct
import zipfile
from pathlib import Path

from download_ipa import validate_input


def verify(source, output, library):
    validate_input(output)
    with zipfile.ZipFile(source) as before, zipfile.ZipFile(output) as after:
        if after.testzip() is not None:
            raise ValueError('Output ZIP integrity check failed')
        info_path = next(n for n in before.namelist() if re.fullmatch(r'Payload/[^/]+/Info.plist', n))
        import plistlib
        info = plistlib.loads(before.read(info_path))
        app = info_path.rsplit('/', 1)[0]
        executable = app + '/' + info['CFBundleExecutable']
        added = app + '/Frameworks/LINEPlus.dylib'
        old_names, new_names = set(before.namelist()), set(after.namelist())
        if new_names - old_names != {added}:
            raise ValueError('Unexpected added ZIP entries')
        if any('/_CodeSignature/' not in n and not n.endswith('/embedded.mobileprovision') for n in old_names - new_names):
            raise ValueError('Unexpected removed ZIP entries')
        for name in old_names & new_names:
            if name != executable and before.read(name) != after.read(name):
                raise ValueError(f'Unexpected modification: {name}')
        original, patched = before.read(executable), after.read(executable)
        end = 32 + max(struct.unpack_from('<I', original, 20)[0], struct.unpack_from('<I', patched, 20)[0])
        if original[end:] != patched[end:]:
            raise ValueError('Executable changed outside load-command header')
        if after.read(added) != library.read_bytes():
            raise ValueError('Packaged library differs from the build output')
    with output.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    manifest = json.loads(output.with_suffix('.json').read_text())
    if manifest['output_sha256'] != digest:
        raise ValueError('Manifest checksum mismatch')
    print('PASS: ZIP integrity, unchanged app resources and executable code, injected library and manifest checksum')
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as summary:
            summary.write(
                '## LINEPlus IPA ビルド完了\n\n'
                f'- 出力: `{output.name}`\n'
                f'- SHA-256: `{digest}`\n'
                '- この実行の **Artifacts** からダウンロードしてZIPを展開してください。\n'
                '- IPAは未署名です。SideStore／AltStoreで再署名して導入してください。\n'
                '- 現在の開発プレビューをビルドします。未実装機能と実機未検証の項目はREADMEを参照してください。\n'
            )


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--library', type=Path, required=True)
    args = parser.parse_args()
    verify(args.source, args.output, args.library)
