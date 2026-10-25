import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_ipa import inject, LOAD_PATH


def image(encrypted=0, padding=320):
    segment = struct.pack('<II16sQQQQiiII', 0x19, 152, b'__TEXT', 0x100000000, 4096, 0, 4096, 7, 5, 1, 0)
    section = struct.pack('<16s16sQQIIIIIIII', b'__text', b'__TEXT', 0x100000000 + padding, 16, padding, 2, 0, 0, 0, 0, 0, 0)
    encryption = struct.pack('<6I', 0x2c, 24, 0, 0, encrypted, 0)
    signature = struct.pack('<4I', 0x1d, 16, 1000, 32)
    commands = segment + section + encryption + signature
    header = struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 2, 3, len(commands), 0, 0)
    return (header + commands).ljust(padding, b'\0') + bytes(range(16)) + bytes(1024)


class InjectionTests(unittest.TestCase):
    def test_preserves_every_byte_after_header_padding(self):
        original = image()
        patched = inject(original)
        self.assertEqual(len(original), len(patched))
        self.assertEqual(original[320:], patched[320:])
        self.assertIn(LOAD_PATH, patched[:320])
        count, size = struct.unpack_from('<II', patched, 16)
        pos, commands = 32, []
        for _ in range(count):
            cmd, length = struct.unpack_from('<II', patched, pos)
            commands.append(cmd)
            pos += length
        self.assertEqual(pos, size + 32)
        self.assertNotIn(0x1d, commands)
        self.assertEqual(commands.count(0xc), 1)

    def test_rejects_encrypted_binary(self):
        with self.assertRaisesRegex(ValueError, 'Encrypted'):
            inject(image(encrypted=1))

    def test_rejects_duplicate_injection(self):
        with self.assertRaisesRegex(ValueError, 'already injected'):
            inject(inject(image()))

    def test_rejects_insufficient_padding(self):
        with self.assertRaisesRegex(ValueError, 'padding'):
            inject(image(padding=224))


if __name__ == '__main__':
    unittest.main()
