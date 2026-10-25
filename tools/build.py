#!/usr/bin/env python3
"""Build the standalone arm64 iOS extension with the locally installed Xcode."""
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build():
    env = os.environ.copy()
    if not env.get('DEVELOPER_DIR'):
        selected = subprocess.check_output(['xcode-select', '-p'], text=True).strip()
        if '/CommandLineTools' in selected:
            selected = '/Applications/Xcode.app/Contents/Developer'
        env['DEVELOPER_DIR'] = selected
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], env=env, text=True).strip()
    output = ROOT / 'build' / 'LINEPlus.dylib'
    output.parent.mkdir(exist_ok=True)
    subprocess.run([
        'xcrun', '--sdk', 'iphoneos', 'clang', '-target', 'arm64-apple-ios18.0',
        '-isysroot', sdk, '-fobjc-arc', '-fblocks', '-dynamiclib', '-O2',
        '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
        '-framework', 'UIKit', '-framework', 'Foundation', '-framework', 'QuartzCore', '-framework', 'ImageIO', '-framework', 'CoreGraphics',
        '-install_name', '@executable_path/Frameworks/LINEPlus.dylib',
        str(ROOT / 'Sources' / 'LINEPlus.m'), '-o', str(output),
    ], check=True, env=env)
    subprocess.run(['codesign', '--force', '--sign', '-', str(output)], check=True)
    print(f'Built: {output}')
    return output


if __name__ == '__main__':
    build()
