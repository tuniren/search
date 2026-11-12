"""Create both Windows portable editions from CMake installation components.

Usage: python scripts/package.py --build-dir build/qt-sdk-release
Requires Python 3.10+ and CMake; the application must already be built.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile


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
    sizes = []
    packages = []
    with tempfile.TemporaryDirectory(prefix="search-package-", dir=output) as temporary:
        temporary = Path(temporary).resolve()
        # The temporary directory's automatic recursive cleanup must stay
        # inside the explicitly selected output directory.
        if not temporary.is_relative_to(output):
            raise ValueError("Packaging directory is outside the output directory")
        stage = temporary / "application"
        install(build, stage, "Application")
        application_files = {path.relative_to(stage) for path in stage.rglob("*") if path.is_file()}
        for variant in ("no-runtime", "with-runtime"):
            if variant == "with-runtime":
                install(build, stage, "Runtime")
                if not any(path.name.lower() == "vcruntime140.dll" for path in stage.iterdir()):
                    raise RuntimeError("The Runtime component did not supply the MSVC runtime DLLs")
                if not application_files.issubset(
                    path.relative_to(stage) for path in stage.rglob("*") if path.is_file()
                ):
                    raise RuntimeError("Runtime installation changed the application file set")
            destination = output / f"{name}-{variant}.zip"
            compressed, unpacked = archive(stage, destination, temporary)
            sizes.append((destination, compressed, unpacked))
            packages.append({
                "architecture": metadata["architecture"], "variant": variant,
                "name": destination.name, "size": compressed,
                "sha256": checksum(destination),
            })
        manifest = temporary / "update.json"
        manifest.write_text(json.dumps({"schema": 1, "version": metadata["version"], "packages": packages},
                                       indent=2) + "\n", encoding="utf-8")
        os.replace(manifest, output / "update.json")
    print("\nPortable packages (decimal MB):")
    for destination, compressed, unpacked in sizes:
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
