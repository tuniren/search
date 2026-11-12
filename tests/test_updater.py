"""Exercise the real native updater with disposable installations and an actual child EXE."""

import argparse
import ctypes
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


parser = argparse.ArgumentParser()
parser.add_argument("--updater", type=Path, required=True)
parser.add_argument("--fixture", type=Path, required=True)
options, remaining = parser.parse_known_args()


class UpdaterTests(unittest.TestCase):
    def setUp(self):
        self.scratch = Path(tempfile.mkdtemp(prefix="search-updater-"))
        self.target = self.scratch / "程序 with spaces"
        self.target.mkdir()
        self.exe = options.fixture.read_bytes()
        self.old = {
            "search.exe": self.exe, "search-updater.exe": b"old helper",
            "package-info.json": b'{"variant":"no-runtime"}',
            "stale.dll": b"obsolete", "plugins/shared.dll": b"old version",
        }
        self.with_inventory(self.old)
        for name, data in self.old.items():
            path = self.target / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (self.target / "personal.txt").write_bytes(b"keep my file")
        self.new = {
            "search.exe": self.exe, "search-updater.exe": b"new helper",
            "package-info.json": b'{"variant":"no-runtime"}',
            "plugins/shared.dll": b"new version", "plugins/new.dll": b"added",
            "empty.txt": b"",
        }
        self.archive = self.scratch / "package.zip"
        self.root = "search-0.9.0-windows-x64-no-runtime"

    @staticmethod
    def with_inventory(files):
        files["installed-files.txt"] = ("\r\n".join(sorted(set(files) | {"installed-files.txt"})) + "\r\n").encode()

    def tearDown(self):
        # A successful fixture remains alive briefly so startup confirmation sees it.
        for attempt in range(30):
            try:
                shutil.rmtree(self.scratch)
                return
            except PermissionError:
                time.sleep(0.1)
        shutil.rmtree(self.scratch)

    def pack(self, extras=None):
        self.with_inventory(self.new)
        with ZipFile(self.archive, "w", ZIP_DEFLATED) as archive:
            for name, data in self.new.items():
                archive.writestr(self.root + "/" + name, data)
            for name, data in extras or []:
                archive.writestr(name, data)

    def run_update(self, checksum=None):
        checksum = checksum or hashlib.sha256(self.archive.read_bytes()).hexdigest()
        parent = subprocess.Popen([self.target / "search.exe", "--wait"])
        try:
            result = subprocess.run([options.updater, "--apply", str(parent.pid), self.target,
                                     self.archive, checksum, self.root, "--quiet"], timeout=40)
        finally:
            parent.wait(timeout=5)
        return result.returncode

    def assert_old_intact(self):
        for name, data in self.old.items():
            self.assertEqual((self.target / name).read_bytes(), data, name)
        self.assertEqual((self.target / "personal.txt").read_bytes(), b"keep my file")
        self.assertFalse((self.target / "plugins/new.dll").exists())

    def test_success_removes_stale_files_and_preserves_personal_files(self):
        self.pack()
        self.assertEqual(self.run_update(), 0)
        for name, data in self.new.items():
            self.assertEqual((self.target / name).read_bytes(), data)
        self.assertFalse((self.target / "stale.dll").exists())
        self.assertFalse((self.target / ".search-update").exists())
        self.assertEqual((self.target / "personal.txt").read_bytes(), b"keep my file")

    def test_hash_mismatch_preserves_original(self):
        self.pack()
        self.assertNotEqual(self.run_update("0" * 64), 0)
        self.assert_old_intact()

    def test_lf_inventory_is_also_supported(self):
        path = self.target / "installed-files.txt"
        path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))
        self.pack()
        self.assertEqual(self.run_update(), 0)

    def test_corrupt_zip_crc_preserves_original(self):
        self.pack()
        with ZipFile(self.archive) as archive:
            entry = archive.getinfo(self.root + "/plugins/shared.dll")
        data = bytearray(self.archive.read_bytes())
        filename_length, extra_length = struct.unpack_from("<HH", data, entry.header_offset + 26)
        payload_start = entry.header_offset + 30 + filename_length + extra_length
        data[payload_start] ^= 0xFF
        self.archive.write_bytes(data)
        self.assertNotEqual(self.run_update(), 0)
        self.assert_old_intact()

    def test_path_traversal_is_rejected(self):
        self.pack([(self.root + "/../escaped.txt", b"escape")])
        self.assertNotEqual(self.run_update(), 0)
        self.assert_old_intact()
        self.assertFalse((self.scratch / "escaped.txt").exists())

    def test_case_collisions_are_rejected(self):
        self.pack([(self.root + "/SEARCH.EXE", self.exe)])
        self.assertNotEqual(self.run_update(), 0)
        self.assert_old_intact()

    def test_symlinks_are_rejected(self):
        self.pack()
        link = ZipInfo(self.root + "/evil")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        with ZipFile(self.archive, "a") as archive:
            archive.writestr(link, "../../somewhere")
        self.assertNotEqual(self.run_update(), 0)
        self.assert_old_intact()

    def test_inventory_mismatch_is_rejected(self):
        self.pack([(self.root + "/unexpected.dll", b"extra")])
        self.assertNotEqual(self.run_update(), 0)
        self.assert_old_intact()

    def test_personal_file_conflict_is_rejected(self):
        self.new["personal.txt"] = b"must not overwrite"
        self.pack()
        self.assertNotEqual(self.run_update(), 0)
        self.assert_old_intact()

    def test_startup_failure_rolls_back(self):
        self.new["startup-fail"] = b"fixture startup failure"
        self.pack()
        self.assertNotEqual(self.run_update(), 0)
        self.assert_old_intact()
        self.assertFalse((self.target / "startup-fail").exists())

    def test_locked_file_rolls_back_partial_backup(self):
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                                        ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
        kernel32.CreateFileW.restype = ctypes.c_void_p
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        locked = kernel32.CreateFileW(str(self.target / "stale.dll"), 0x80000000, 1, None, 3, 0x80, None)
        self.assertNotEqual(locked, ctypes.c_void_p(-1).value)
        try:
            self.pack()
            self.assertNotEqual(self.run_update(), 0)
            self.assert_old_intact()
        finally:
            kernel32.CloseHandle(locked)

    def test_interrupted_install_can_be_recovered(self):
        transaction = self.target / ".search-update"
        backup = transaction / "backup"
        backup.mkdir(parents=True)
        self.with_inventory(self.new)
        (transaction / "old.txt").write_bytes(self.old["installed-files.txt"])
        (transaction / "new.txt").write_bytes(self.new["installed-files.txt"])
        (transaction / "journal-ready").write_text("ready\n")
        for name in self.old:
            path = backup / name
            path.parent.mkdir(parents=True, exist_ok=True)
            (self.target / name).rename(path)
        (transaction / "backup-complete").write_text("ready\n")
        (self.target / "search.exe").write_bytes(self.exe)
        (self.target / "plugins/new.dll").write_bytes(b"half-installed")
        result = subprocess.run([options.updater, "--recover", self.target, "--quiet"], timeout=10)
        self.assertEqual(result.returncode, 0)
        self.assert_old_intact()


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)
