"""Create the Windows portable package with the VC++ runtime included.

Usage: python scripts/package.py --build-dir build/qt-sdk-release
Requires Python 3.10+ and CMake; the application must already be built.
"""

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile


def validate_layout(stage: Path, metadata: dict, variant: str) -> None:
    """Reject incomplete packages before publishing any archive or update manifest."""
    required = {
        "search.exe", "search-updater.exe", "Qt6Core.dll", "Qt6Gui.dll",
        "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Sql.dll", "qt.conf",
        "plugins/platforms/qwindows.dll", "plugins/sqldrivers/qsqlite.dll",
        "plugins/imageformats/qico.dll", "plugins/tls/qschannelbackend.dll",
        "package-info.json", "installed-files.txt",
    }
    runtime = {"vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll",
               "msvcp140_2.dll", "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "concrt140.dll"}
    files = {path.relative_to(stage).as_posix() for path in stage.rglob("*") if path.is_file()}
    missing = (required | (runtime if variant == "with-runtime" else set())) - files
    if missing:
        raise ValueError(f"Invalid portable layout; missing files beside search.exe or plugins: {sorted(missing)}")
    if variant == "no-runtime" and files & runtime:
        raise ValueError("The no-runtime package unexpectedly contains VC++ runtime DLLs")
    inventory = set((stage / "installed-files.txt").read_text(encoding="utf-8").splitlines())
    if files != inventory:
        raise ValueError("Installed file inventory does not match the package contents")
    info = json.loads((stage / "package-info.json").read_text(encoding="utf-8"))
    if any(info.get(key) != metadata[key] for key in ("version", "architecture")) or info.get("variant") != variant:
        raise ValueError("Package identity does not match the requested edition")
    configuration = configparser.ConfigParser()
    configuration.read(stage / "qt.conf", encoding="utf-8")
    prefix = stage / configuration.get("Paths", "Prefix", fallback=".")
    plugins = prefix / configuration.get("Paths", "Plugins", fallback="plugins")
    if prefix.resolve() != stage.resolve() or plugins.resolve() != (stage / "plugins").resolve():
        raise ValueError("qt.conf points outside the portable package's plugin layout")


def install(build: Path, destination: Path, component: str) -> None:
    subprocess.run(
        ["cmake", "--install", str(build), "--prefix", str(destination), "--component", component],
        check=True,
    )


def archive(stage: Path, destination: Path, temporary: Path) -> tuple[int, int]:
    """Publish an archive only after its contents pass a CRC check."""
    files = sorted(path for path in stage.rglob("*") if path.is_file())
    unpacked_bytes = sum(path.stat().st_size for path in files)
    pending = temporary / destination.name
    with ZipFile(pending, "x", ZIP_DEFLATED, compresslevel=9) as package:
        for path in files:
            package.write(path, Path(destination.stem) / path.relative_to(stage))
    with ZipFile(pending) as package:
        damaged = package.testzip()
        if damaged:
            raise RuntimeError(f"Archive verification failed: {damaged}")
    os.replace(pending, destination)
    return destination.stat().st_size, unpacked_bytes


def checksum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def package(build: Path, output: Path) -> None:
    metadata = json.loads((build / "package.json").read_text(encoding="utf-8"))
    if metadata["configuration"] != "Release":
        raise ValueError("Portable packages require a Release build")
    if not (build / f'{metadata["name"]}.exe').is_file():
        raise FileNotFoundError(f'Build {metadata["name"]} before packaging')
    name = f'{metadata["name"]}-{metadata["version"]}-{metadata["architecture"]}'
    output.mkdir(parents=True, exist_ok=True)
    variant = "with-runtime"
    with tempfile.TemporaryDirectory(prefix="search-package-", dir=output) as temporary:
        temporary = Path(temporary).resolve()
        # The temporary directory's automatic recursive cleanup must stay
        # inside the explicitly selected output directory.
        if not temporary.is_relative_to(output):
            raise ValueError("Packaging directory is outside the output directory")
        stage = temporary / "application"
        install(build, stage, "Application")
        application_files = {path.relative_to(stage) for path in stage.rglob("*") if path.is_file()}
        install(build, stage, "Runtime")
        if not application_files.issubset(
            path.relative_to(stage) for path in stage.rglob("*") if path.is_file()
        ):
            raise RuntimeError("Runtime installation changed the application file set")
        destination = output / f"{name}-{variant}.zip"
        validate_layout(stage, metadata, variant)
        compressed, unpacked = archive(stage, destination, temporary)
        packages = [{
            "architecture": metadata["architecture"], "variant": variant,
            "name": destination.name, "size": compressed,
            "sha256": checksum(destination),
        }]
        manifest = temporary / "update.json"
        manifest.write_text(json.dumps({"schema": 1, "version": metadata["version"], "packages": packages},
                                       indent=2) + "\n", encoding="utf-8")
        os.replace(manifest, output / "update.json")
    # Remove only this version's discontinued edition after the replacement is ready.
    (output / f"{name}-no-runtime.zip").unlink(missing_ok=True)
    print("\nPortable package (decimal MB):")
    print(f"  {destination}\n    ZIP {compressed / 1e6:.2f} MB; unpacked {unpacked / 1e6:.2f} MB")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True, help="Configured Release build directory")
    parser.add_argument("--output-dir", type=Path, help="Output folder (default: BUILD_DIR/packages)")
    arguments = parser.parse_args()
    build = arguments.build_dir.resolve()
    output = (arguments.output_dir or build / "packages").resolve()
    try:
        package(build, output)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()
