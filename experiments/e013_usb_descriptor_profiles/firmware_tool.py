#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and upload the two ESP32-S3 USB descriptor test profiles."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
FQBN = (
    "esp32:esp32:esp32s3:"
    "USBMode=default,CDCOnBoot=default,UploadMode=default"
)
PROFILES = {
    "a": (1, "profile_a"),
    "b": (2, "profile_b"),
}


def arduino_cli() -> str:
    executable = shutil.which("arduino-cli")
    if executable is None:
        raise SystemExit("arduino-cli was not found in PATH")
    return executable


def build(profile: str) -> Path:
    number, directory = PROFILES[profile]
    build_path = HERE / "build" / directory
    subprocess.run(
        [
            arduino_cli(),
            "compile",
            "--fqbn",
            FQBN,
            "--warnings",
            "all",
            "--build-property",
            f"compiler.cpp.extra_flags=-DOEP_USB_TEST_PROFILE={number}",
            "--build-path",
            str(build_path),
            str(HERE),
        ],
        check=True,
    )
    return build_path


def upload(profile: str, port: str) -> None:
    build_path = build(profile)
    subprocess.run(
        [
            arduino_cli(),
            "upload",
            "--fqbn",
            FQBN,
            "--port",
            port,
            "--input-dir",
            str(build_path),
        ],
        check=True,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser("build")
    build_parser.add_argument("--profile", choices=("a", "b", "all"), default="all")

    upload_parser = subparsers.add_parser("upload")
    upload_parser.add_argument("--profile", choices=("a", "b"), required=True)
    upload_parser.add_argument("--port", required=True, help="UART/USB-Serial upload port")

    args = parser.parse_args()
    if args.command == "build":
        selected = PROFILES if args.profile == "all" else (args.profile,)
        for profile in selected:
            build(profile)
    else:
        upload(args.profile, args.port)


if __name__ == "__main__":
    main()

