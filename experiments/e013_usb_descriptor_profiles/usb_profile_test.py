#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect and exercise E013's HID, USBVendor, and CDC interfaces."""

from __future__ import annotations

import argparse
import json
import os
import platform
import secrets
import sys
from pathlib import Path
from typing import Any

TEST_VID = 0x1209
TEST_PID = 0x0001
TEST_PRODUCT = "OEP USB Profile Test"
HID_REPORT_ID = 6
HID_PAYLOAD_SIZE = 63
PROFILE_REVISION = {"a": 0x0001, "b": 0x0002}


class TestFailure(RuntimeError):
    pass


def _require(module_name: str, install_name: str | None = None):
    try:
        return __import__(module_name, fromlist=["*"])
    except ImportError as error:
        package = install_name or module_name
        raise SystemExit(f"missing Python dependency: {package}; run 'uv sync'") from error


def _libusb_backend():
    usb_backend = _require("usb.backend.libusb1", "pyusb")
    try:
        libusb_package = _require("libusb_package", "libusb-package")
    except SystemExit:
        return usb_backend.get_backend()
    return usb_backend.get_backend(find_library=libusb_package.find_library)


def _safe_usb_string(device, index: int) -> str | None:
    if not index:
        return None
    try:
        usb_util = _require("usb.util", "pyusb")
        return usb_util.get_string(device, index)
    except Exception:
        return None


def _usb_devices(serial_number: str | None = None) -> list[Any]:
    usb_core = _require("usb.core", "pyusb")
    backend = _libusb_backend()
    if backend is None:
        raise TestFailure("libusb backend is unavailable")
    found = list(
        usb_core.find(
            find_all=True,
            idVendor=TEST_VID,
            idProduct=TEST_PID,
            backend=backend,
        )
        or []
    )
    devices = []
    for device in found:
        product = _safe_usb_string(device, device.iProduct)
        serial = _safe_usb_string(device, device.iSerialNumber)
        if product not in (None, TEST_PRODUCT):
            continue
        if serial_number is not None and serial != serial_number:
            continue
        devices.append(device)
    return devices


def _hid_records(serial_number: str | None = None) -> list[dict[str, Any]]:
    hid = _require("hid", "hidapi")
    records = []
    for item in hid.enumerate(TEST_VID, TEST_PID):
        if item.get("product_string") != TEST_PRODUCT:
            continue
        if serial_number is not None and item.get("serial_number") != serial_number:
            continue
        records.append(item)
    return records


def _cdc_records(serial_number: str | None = None) -> list[Any]:
    list_ports = _require("serial.tools.list_ports", "pyserial")
    records = []
    for port in list_ports.comports():
        if port.vid != TEST_VID or port.pid != TEST_PID:
            continue
        if port.product not in (None, TEST_PRODUCT):
            continue
        if serial_number is not None and port.serial_number != serial_number:
            continue
        records.append(port)
    return records


def _device_snapshot(device) -> dict[str, Any]:
    result: dict[str, Any] = {
        "bus": getattr(device, "bus", None),
        "address": getattr(device, "address", None),
        "vid": f"{device.idVendor:04x}",
        "pid": f"{device.idProduct:04x}",
        "bcd_device": f"{device.bcdDevice:04x}",
        "bcd_usb": f"{device.bcdUSB:04x}",
        "device_class": device.bDeviceClass,
        "device_subclass": device.bDeviceSubClass,
        "device_protocol": device.bDeviceProtocol,
        "manufacturer": _safe_usb_string(device, device.iManufacturer),
        "product": _safe_usb_string(device, device.iProduct),
        "serial_number": _safe_usb_string(device, device.iSerialNumber),
        "configurations": [],
    }
    try:
        for configuration in device:
            config = {
                "value": configuration.bConfigurationValue,
                "attributes": configuration.bmAttributes,
                "max_power": configuration.bMaxPower,
                "interfaces": [],
            }
            for interface in configuration:
                config["interfaces"].append(
                    {
                        "number": interface.bInterfaceNumber,
                        "alternate": interface.bAlternateSetting,
                        "class": interface.bInterfaceClass,
                        "subclass": interface.bInterfaceSubClass,
                        "protocol": interface.bInterfaceProtocol,
                        "endpoints": [
                            {
                                "address": f"0x{endpoint.bEndpointAddress:02x}",
                                "attributes": endpoint.bmAttributes,
                                "max_packet_size": endpoint.wMaxPacketSize,
                                "interval": endpoint.bInterval,
                            }
                            for endpoint in interface
                        ],
                    }
                )
            result["configurations"].append(config)
    except Exception as error:
        result["descriptor_error"] = repr(error)
    return result


def snapshot(serial_number: str | None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "machine": platform.machine(),
            "python": sys.version,
        },
        "filter": {
            "vid": f"{TEST_VID:04x}",
            "pid": f"{TEST_PID:04x}",
            "product": TEST_PRODUCT,
            "serial_number": serial_number,
        },
        "usb": [],
        "hid": [],
        "cdc": [],
        "errors": [],
    }
    try:
        result["usb"] = [_device_snapshot(device) for device in _usb_devices(serial_number)]
    except Exception as error:
        result["errors"].append({"source": "pyusb", "error": repr(error)})
    try:
        result["hid"] = [
            {
                "path": os.fsdecode(item["path"]),
                "vendor_id": f"{item['vendor_id']:04x}",
                "product_id": f"{item['product_id']:04x}",
                "release_number": f"{item.get('release_number', 0):04x}",
                "serial_number": item.get("serial_number"),
                "manufacturer_string": item.get("manufacturer_string"),
                "product_string": item.get("product_string"),
                "usage_page": item.get("usage_page"),
                "usage": item.get("usage"),
                "interface_number": item.get("interface_number"),
            }
            for item in _hid_records(serial_number)
        ]
    except Exception as error:
        result["errors"].append({"source": "hidapi", "error": repr(error)})
    try:
        result["cdc"] = [
            {
                "device": port.device,
                "name": port.name,
                "description": port.description,
                "hwid": port.hwid,
                "serial_number": port.serial_number,
                "location": port.location,
                "manufacturer": port.manufacturer,
                "product": port.product,
            }
            for port in _cdc_records(serial_number)
        ]
    except Exception as error:
        result["errors"].append({"source": "pyserial", "error": repr(error)})
    return result


def _only(items: list[Any], description: str) -> Any:
    if len(items) != 1:
        raise TestFailure(f"expected exactly one {description}, found {len(items)}")
    return items[0]


def test_hid(serial_number: str | None) -> dict[str, Any]:
    hid = _require("hid", "hidapi")
    record = _only(_hid_records(serial_number), "matching HID interface")
    payload = ("OEP-HID-" + secrets.token_hex(8)).encode("ascii")
    padded = payload.ljust(HID_PAYLOAD_SIZE, b"\0")
    handle = hid.device()
    try:
        handle.open_path(record["path"])
        written = handle.write(bytes([HID_REPORT_ID]) + padded)
        received = bytes(handle.read(HID_PAYLOAD_SIZE + 1, timeout_ms=3000))
    finally:
        handle.close()
    if written <= 0:
        raise TestFailure("HID write returned no bytes")
    if len(received) == HID_PAYLOAD_SIZE + 1 and received[0] == HID_REPORT_ID:
        received = received[1:]
    if received != padded:
        raise TestFailure(f"HID echo mismatch: sent={padded!r}, received={received!r}")
    return {"transport": "hid", "sent": payload.decode(), "bytes": len(padded)}


def _vendor_interface(device):
    usb_util = _require("usb.util", "pyusb")
    configuration = device.get_active_configuration()
    interface = usb_util.find_descriptor(
        configuration,
        custom_match=lambda item: item.bInterfaceClass == 0xFF,
    )
    if interface is None:
        raise TestFailure("vendor-specific interface was not found")
    endpoint_out = usb_util.find_descriptor(
        interface,
        custom_match=lambda ep: usb_util.endpoint_direction(ep.bEndpointAddress)
        == usb_util.ENDPOINT_OUT,
    )
    endpoint_in = usb_util.find_descriptor(
        interface,
        custom_match=lambda ep: usb_util.endpoint_direction(ep.bEndpointAddress)
        == usb_util.ENDPOINT_IN,
    )
    if endpoint_out is None or endpoint_in is None:
        raise TestFailure("vendor bulk IN/OUT endpoints were not found")
    return interface, endpoint_out, endpoint_in


def test_vendor(serial_number: str | None) -> dict[str, Any]:
    usb_util = _require("usb.util", "pyusb")
    device = _only(_usb_devices(serial_number), "matching USB device")
    interface, endpoint_out, endpoint_in = _vendor_interface(device)
    number = interface.bInterfaceNumber
    detached = False
    try:
        try:
            if device.is_kernel_driver_active(number):
                device.detach_kernel_driver(number)
                detached = True
        except (NotImplementedError, AttributeError):
            pass
        usb_util.claim_interface(device, number)
        payload = ("OEP-VENDOR-" + secrets.token_hex(8)).encode("ascii")
        endpoint_out.write(payload, timeout=3000)
        received = bytes(endpoint_in.read(64, timeout=3000))
        if received != payload:
            raise TestFailure(
                f"Vendor echo mismatch: sent={payload!r}, received={received!r}"
            )
    finally:
        try:
            usb_util.release_interface(device, number)
        except Exception:
            pass
        usb_util.dispose_resources(device)
        if detached:
            try:
                device.attach_kernel_driver(number)
            except Exception:
                pass
    return {"transport": "vendor", "sent": payload.decode(), "bytes": len(payload)}


def test_cdc(serial_number: str | None, explicit_port: str | None) -> dict[str, Any]:
    serial_module = _require("serial", "pyserial")
    if explicit_port is None:
        port = _only(_cdc_records(serial_number), "matching CDC port").device
    else:
        port = explicit_port
    payload = ("OEP-CDC-" + secrets.token_hex(8) + "\n").encode("ascii")
    with serial_module.Serial(
        port=port,
        baudrate=115200,
        timeout=3,
        write_timeout=3,
    ) as connection:
        connection.reset_input_buffer()
        connection.write(payload)
        connection.flush()
        received = connection.read(len(payload))
    if received != payload:
        raise TestFailure(f"CDC echo mismatch: sent={payload!r}, received={received!r}")
    return {
        "transport": "cdc",
        "port": port,
        "sent": payload.decode().rstrip(),
        "bytes": len(payload),
    }


def validate_profile(report: dict[str, Any], profile_name: str) -> None:
    expected_revision = PROFILE_REVISION[profile_name]
    revisions = {
        int(record["release_number"], 16)
        for record in report["hid"]
        if record.get("release_number")
    }
    revisions.update(
        int(record["bcd_device"], 16)
        for record in report["usb"]
        if record.get("bcd_device")
    )
    if not revisions:
        raise TestFailure("could not determine bcdDevice from HID or USB descriptors")
    if revisions != {expected_revision}:
        raise TestFailure(
            f"expected bcdDevice {expected_revision:04x}, found "
            f"{sorted(f'{item:04x}' for item in revisions)}"
        )
    if not report["hid"]:
        raise TestFailure("HID interface is missing")
    if profile_name == "a" and report["cdc"]:
        raise TestFailure("Profile A unexpectedly exposes a CDC port")
    if profile_name == "b" and not report["cdc"]:
        raise TestFailure("Profile B CDC port is missing")

    interface_classes = {
        interface["class"]
        for device in report["usb"]
        for configuration in device.get("configurations", [])
        for interface in configuration.get("interfaces", [])
    }
    if interface_classes:
        if profile_name == "a" and interface_classes != {0x03}:
            raise TestFailure(
                "Profile A must expose only HID interfaces; found classes "
                f"{sorted(f'0x{item:02x}' for item in interface_classes)}"
            )
        if profile_name == "b":
            required = {0x02, 0x03, 0xFF}
            missing = required - interface_classes
            if missing:
                raise TestFailure(
                    "Profile B is missing USB interface classes "
                    f"{sorted(f'0x{item:02x}' for item in missing)}"
                )


def write_json(data: dict[str, Any], output: Path | None) -> None:
    encoded = json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True)
    print(encoded)
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", help="select one ESP32-S3 USB serial number")
    parser.add_argument("--output", type=Path, help="also write JSON to this path")
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("--expect-profile", choices=("a", "b"))

    test_parser = subparsers.add_parser("test")
    test_parser.add_argument("--profile", choices=("a", "b"), required=True)
    test_parser.add_argument("--cdc-port", help="override CDC port auto-detection")

    args = parser.parse_args()
    report = snapshot(args.serial)
    if args.command == "inspect":
        if args.expect_profile is not None:
            validate_profile(report, args.expect_profile)
        write_json(report, args.output)
        return

    validate_profile(report, args.profile)
    results = [test_hid(args.serial)]
    if args.profile == "b":
        results.append(test_vendor(args.serial))
        results.append(test_cdc(args.serial, args.cdc_port))
    report["echo_tests"] = results
    report["result"] = "pass"
    write_json(report, args.output)


if __name__ == "__main__":
    try:
        main()
    except TestFailure as error:
        print(f"E013 FAIL: {error}", file=sys.stderr)
        raise SystemExit(1) from error
