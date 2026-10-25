import hashlib
import plistlib
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from download_ipa import download, validate_input


class DownloadTests(unittest.TestCase):
    def make_ipa(self, path, version='26.16.0', bundle='jp.naver.line'):
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('Payload/LINE.app/Info.plist', plistlib.dumps({
                'CFBundleIdentifier': bundle, 'CFBundleShortVersionString': version,
                'CFBundleExecutable': 'LINE',
            }))
            archive.writestr('Payload/LINE.app/LINE', b'placeholder')

    def test_accepts_line_and_matching_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'LINE.ipa'
            self.make_ipa(path)
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(validate_input(path, digest), digest)

    def test_rejects_other_app_and_unsupported_version(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'LINE.ipa'
            for kwargs in ({'bundle': 'another.app'}, {'version': '15.0.0'}):
                self.make_ipa(path, **kwargs)
                with self.assertRaises(ValueError):
                    validate_input(path)

    def test_rejects_html_and_checksum_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'LINE.ipa'
            path.write_text('<html>download page</html>')
            with self.assertRaisesRegex(ValueError, 'direct file URL'):
                validate_input(path)
            self.make_ipa(path)
            with self.assertRaisesRegex(ValueError, 'does not match'):
                validate_input(path, '0' * 64)

    def test_url_is_passed_as_one_argument_without_shell(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'LINE.ipa'
            url = 'https://example.com/LINE.ipa?token=a&text=$(touch%20oops)'

            def fake_curl(args, **kwargs):
                self.assertEqual(args[-1], url)
                self.assertNotIn('shell', kwargs)
                self.make_ipa(Path(args[args.index('--output') + 1]))
                from subprocess import CompletedProcess
                return CompletedProcess(args, 0)

            with patch('download_ipa.subprocess.run', side_effect=fake_curl):
                download(url, path)
            self.assertTrue(path.exists())
            self.assertFalse(path.with_suffix('.ipa.part').exists())

    def test_rejects_non_http_url(self):
        with self.assertRaisesRegex(ValueError, 'HTTP'):
            download('file:///etc/passwd', Path('unused.ipa'))


if __name__ == '__main__':
    unittest.main()
