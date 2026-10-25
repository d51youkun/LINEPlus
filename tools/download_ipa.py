#!/usr/bin/env python3
"""Download workflow input without interpolating the URL into a shell script."""
import argparse
import hashlib
import os
import plistlib
import re
import subprocess
import sys
import zipfile
from pathlib import Path
from urllib.parse import urlsplit


def validate_input(path, expected_sha256=''):
    if expected_sha256 and not re.fullmatch(r'[0-9a-fA-F]{64}', expected_sha256):
        raise ValueError('SHA-256 must contain exactly 64 hexadecimal characters')
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if expected_sha256 and digest != expected_sha256.lower():
        raise ValueError('Downloaded IPA SHA-256 does not match the supplied value')
    if not zipfile.is_zipfile(path):
        raise ValueError('URL did not return an IPA/ZIP; use a direct file URL, not a download page')
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('IPA contains duplicate ZIP entries')
        infos = [n for n in names if re.fullmatch(r'Payload/[^/]+/Info.plist', n)]
        if len(infos) != 1:
            raise ValueError('Expected one main application in the IPA')
        info = plistlib.loads(archive.read(infos[0]))
        if info.get('CFBundleIdentifier') != 'jp.naver.line':
            raise ValueError('Expected LINE IPA with bundle identifier jp.naver.line')
        if info.get('CFBundleShortVersionString') != '26.16.0':
            raise ValueError('This build currently supports only LINE 26.16.0')
        executable = info.get('CFBundleExecutable', '')
        if not executable or '/' in executable or executable in ('.', '..'):
            raise ValueError('Invalid CFBundleExecutable')
        if infos[0].rsplit('/', 1)[0] + '/' + executable not in names:
            raise ValueError('Main executable is missing')
    print(f'Validated LINE 26.16.0 IPA: {path.stat().st_size} bytes; SHA-256: {digest}')
    return digest


def download(url, destination, expected_sha256=''):
    parsed = urlsplit(url)
    if parsed.scheme not in ('http', 'https') or not parsed.hostname:
        raise ValueError('IPA_URL must be an HTTP(S) direct download URL')
    if any(ord(char) < 32 for char in url):
        raise ValueError('URL must not contain control characters')
    if expected_sha256 and not re.fullmatch(r'[0-9a-fA-F]{64}', expected_sha256):
        raise ValueError('SHA-256 must contain exactly 64 hexadecimal characters')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise ValueError('Download destination already exists')
    partial = destination.with_suffix('.ipa.part')
    try:
        result = subprocess.run([
            'curl', '--fail', '--location', '--silent', '--show-error',
            '--proto', '=http,https', '--proto-redir', '=http,https',
            '--retry', '3', '--connect-timeout', '30', '--max-time', '900',
            '--output', str(partial), '--url', url,
        ], capture_output=True, text=True)
        if result.returncode:
            # curl diagnostics can contain signed URLs; avoid printing them.
            raise ValueError(f'IPA download failed (curl exit {result.returncode}); check URL availability and expiry')
        validate_input(partial, expected_sha256)
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        download(os.environ.get('IPA_URL', '').strip(), args.output, os.environ.get('IPA_SHA256', '').strip())
    except (ValueError, OSError, zipfile.BadZipFile, plistlib.InvalidFileException) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
