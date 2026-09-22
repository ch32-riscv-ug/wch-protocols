"""E160: OEP frame round trip and throughput over the P4 HS vendor bulk interface (E155 over HS).

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e160_p4_hs_bulk_roundtrip/e160_p4_hs_bulk_roundtrip.py -s --clean
The bulk device must be attached to WSL: usbipd.exe attach --wsl --busid <busid of 303a:4021>.
"""

import re
import struct
import subprocess
import threading
import time

import pytest
import usb.core
import usb.util

VID, PID, SERIAL = 0x303A, 0x4021, "e104-p4-windows-v1"
SIZES = [8, 64, 512, 4096]
DEPTHS = [1, 4, 16]


def _frames_for(size):
    return 64 if size == 4096 else 128 if size == 1024 else 256 if size == 512 else 512


def _attach_if_needed():
    """After a reflash the device re-enumerates on Windows; re-attach it by bus id (bound already)."""
    for attempt in range(10):
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None:
            return dev
        try:
            state = subprocess.run(["usbipd.exe", "state"], capture_output=True, text=True, timeout=20).stdout
            import json
            for d in json.loads(state).get("Devices", []):
                if "4021" in (d.get("InstanceId") or "").upper() and d.get("BusId"):
                    subprocess.run(["usbipd.exe", "attach", "--wsl", "--busid", d["BusId"]], capture_output=True, text=True, timeout=30)
        except Exception as e:  # noqa: BLE001
            print("attach attempt failed:", e)
        time.sleep(1.0)
    pytest.skip("303a:4021 not attached to this host")


def _open():
    dev = _attach_if_needed()
    try:
        dev.get_active_configuration()
    except usb.core.USBError:
        dev.set_configuration()
    cfg = dev.get_active_configuration()
    intf = next(i for i in cfg if usb.util.find_descriptor(i, custom_match=lambda e: usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK) is not None)
    usb.util.claim_interface(dev, intf.bInterfaceNumber)
    ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK and usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
    ep_out = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK and usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
    return dev, intf, ep_in, ep_out


class Echo:
    def __init__(self, ep_in):
        self.ep_in = ep_in
        self.buf = bytearray()
        self.received = 0
        self.mismatch = 0
        self.expected = []
        self.cond = threading.Condition()
        self.stop = False
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        while not self.stop:
            try:
                data = self.ep_in.read(65536, timeout=50)
            except usb.core.USBTimeoutError:
                continue
            except usb.core.USBError:
                if self.stop:
                    return
                continue
            if not len(data):
                continue
            self.buf += bytes(data)
            while len(self.buf) >= 2:
                length = self.buf[0] | (self.buf[1] << 8)
                if len(self.buf) < length + 2:
                    break
                frame = bytes(self.buf[:length + 2])
                del self.buf[:length + 2]
                with self.cond:
                    idx = self.received
                    self.received += 1
                    if idx >= len(self.expected) or self.expected[idx] != frame:
                        self.mismatch += 1
                    self.cond.notify_all()

    def run(self, ep_out, size, depth, batch=1):
        """batch > 1: host coalesces `batch` frames into one bulk write (one URB) - variant C."""
        n = _frames_for(size)
        frames = []
        for seq in range(n):
            payload = struct.pack("<H", seq) + bytes((seq + i) & 0xFF for i in range(size - 2))
            frames.append(struct.pack("<H", size) + payload)
        with self.cond:
            self.expected = frames; self.received = 0; self.mismatch = 0; self.buf.clear()
        stalled = False
        t0 = time.perf_counter()
        sent = 0
        while sent < n and not stalled:
            with self.cond:
                if not self.cond.wait_for(lambda: sent - self.received < depth, timeout=5):
                    stalled = True
                    break
            try:
                chunk = b"".join(frames[sent:sent + batch])
                ep_out.write(chunk, timeout=2000)
            except usb.core.USBError:
                stalled = True
                break
            sent += len(frames[sent:sent + batch])
        with self.cond:
            self.cond.wait_for(lambda: self.received >= sent, timeout=5 if stalled else 20)
        t1 = time.perf_counter()
        return n, sent, self.received, self.mismatch, t1 - t0, stalled


def test_p4_hs_bulk_roundtrip(dut):
    dut.write("?")
    m = dut.expect(re.compile(rb"# EXP E160 v1 git=\S+ probe=esp32p4_hs target=none usb_begin=(\d) write_capacity=(\d+) flush_per_frame=(\d)"), timeout=30)
    print(f"\nE160 device: usb_begin={m.group(1).decode()} write_capacity={m.group(2).decode()} flush_per_frame={m.group(3).decode()}")
    time.sleep(1.0)
    dev, intf, ep_in, ep_out = _open()
    print(f"E160 host: bulk IN 0x{ep_in.bEndpointAddress:02x} ({ep_in.wMaxPacketSize} B) OUT 0x{ep_out.bEndpointAddress:02x} ({ep_out.wMaxPacketSize} B) speed={dev.speed}")
    dut.write("?")
    dut.expect(re.compile(rb"ECHO READY mounted=(\d)"), timeout=5)
    # drain anything stale
    try:
        while len(ep_in.read(65536, timeout=50)): pass
    except usb.core.USBError:
        pass
    for size in (8, 512):
        frame = struct.pack("<H", size) + bytes(i & 0xFF for i in range(size))
        samples = []
        for _ in range(100):
            t0 = time.perf_counter()
            ep_out.write(frame, timeout=2000)
            got = bytearray()
            while len(got) < size + 2:
                got += bytes(ep_in.read(65536, timeout=2000))
            samples.append(time.perf_counter() - t0)
            assert bytes(got) == frame, f"reference echo mismatch at size={size}"
        samples.sort()
        print(f"E160 REF size={size} frames=100 median_us={samples[50] * 1e6:.0f} min_us={samples[0] * 1e6:.0f} p95_us={samples[94] * 1e6:.0f}", flush=True)
    echo = Echo(ep_in)
    stalled_at = ""
    try:
        for size in SIZES:
            for depth in DEPTHS:
                if stalled_at:
                    print(f"E160 size={size} depth={depth} skipped after stall at {stalled_at}", flush=True)
                    continue
                n, sent, got, bad, total, stalled = echo.run(ep_out, size, depth)
                us = total * 1e6 / max(got, 1)
                kbs = got * (size + 2) / total / 1000
                print(f"E160 size={size} depth={depth} frames={n} sent={sent} received={got} mismatch={bad} total_s={total:.4f} us_per_frame={us:.0f} kB_s={kbs:.1f} stalled={int(stalled)}", flush=True)
                assert bad == 0
                if stalled:
                    stalled_at = f"size={size} depth={depth}"
        # C: host coalesces `depth` frames per URB (the device handles a byte stream anyway)
        for size in (64, 512, 1024):
            for depth in (4, 16):
                n, sent, got, bad, total, stalled = echo.run(ep_out, size, depth, batch=depth)
                us = total * 1e6 / max(got, 1)
                kbs = got * (size + 2) / total / 1000
                print(f"E160 COALESCED size={size} depth={depth} frames={n} received={got} mismatch={bad} total_s={total:.4f} us_per_frame={us:.0f} kB_s={kbs:.1f} stalled={int(stalled)}", flush=True)
                assert bad == 0 and not stalled
    finally:
        echo.stop = True
        echo.thread.join(timeout=2)
        ep_out.write(b"\x00\x00", timeout=1000)
    m = dut.expect(re.compile(rb"ECHO END frames=(\d+) bytes=(\d+)"), timeout=5)
    print(f"E160 device saw frames={m.group(1).decode()} bytes={m.group(2).decode()}")
    usb.util.dispose_resources(dev)
    assert not stalled_at, stalled_at
