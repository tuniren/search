"""Verify published portable ZIPs and start each edition with only Windows directories on PATH."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZipFile

from package import validate_layout


def verify(directory: Path) -> None:
    manifest = json.loads((directory / "update.json").read_text(encoding="utf-8"))
    packages = manifest["packages"]
    if len(packages) != 2 or {item["variant"] for item in packages} != {"with-runtime", "no-runtime"}:
        raise ValueError("Expected exactly one portable package of each runtime edition")
    for item in packages:
        package = directory / item["name"]
        if package.parent.resolve() != directory.resolve():
            raise ValueError("Package name must not escape its output directory")
        digest = hashlib.sha256()
        with package.open("rb") as stream:
            for chunk in iter(lambda: stream.read(65536), b""):
                digest.update(chunk)
        if package.stat().st_size != item["size"] or digest.hexdigest() != item["sha256"]:
            raise ValueError(f"Size or SHA-256 mismatch: {package.name}")
        with tempfile.TemporaryDirectory(prefix="search-portable-", dir=directory) as scratch:
            scratch = Path(scratch).resolve()
            if not scratch.is_relative_to(directory):
                raise ValueError("Package verification directory is outside the selected output directory")
            prefix = package.stem + "/"
            with ZipFile(package) as archive:
                if archive.testzip():
                    raise ValueError(f"Corrupt ZIP: {package.name}")
                for entry in archive.infolist():
                    if not entry.filename.startswith(prefix):
                        raise ValueError("Unexpected ZIP root")
                    relative = Path(entry.filename[len(prefix):])
                    target = scratch / relative
                    if (entry.is_dir() or "\\" in entry.filename or relative.is_absolute()
                            or ".." in relative.parts or not target.resolve().is_relative_to(scratch)):
                        raise ValueError("Unexpected ZIP path")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.read(entry))
            metadata = {"version": manifest["version"], "architecture": item["architecture"]}
            validate_layout(scratch, metadata, item["variant"])
            if os.name != "nt":
                raise RuntimeError("Portable startup verification must run on Windows")
            windows = Path(os.environ["SystemRoot"])
            environment = {key: value for key, value in os.environ.items()
                           if not key.upper().startswith(("QT_", "QML"))}
            environment["PATH"] = os.pathsep.join(map(str, (windows / "System32", windows, windows / "System32/Wbem")))
            # Disable loader error dialogs: missing dependencies must fail CI, never hang it.
            import ctypes
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            previous = kernel32.SetErrorMode(0x0001 | 0x8000)
            try:
                result = subprocess.run([scratch / "search.exe", "--version"], cwd=scratch,
                    env=environment, capture_output=True, text=True, encoding="utf-8", timeout=30)
            finally:
                kernel32.SetErrorMode(previous)
            expected = "search " + manifest["version"]
            if result.returncode or result.stdout.strip() != expected:
                raise RuntimeError(f"{package.name}: isolated startup failed (0x{result.returncode & 0xffffffff:08x}); "
                                   f"stdout={result.stdout!r}, stderr={result.stderr!r}")
            print(f"Verified {package.name}: layout, SHA-256, plugins and isolated startup")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packages-dir", type=Path, required=True)
    arguments = parser.parse_args()
    verify(arguments.packages_dir.resolve())
