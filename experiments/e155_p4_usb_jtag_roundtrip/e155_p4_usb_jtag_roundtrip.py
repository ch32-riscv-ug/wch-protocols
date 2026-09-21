"""E155: USB-Serial/JTAG round trip and throughput versus frame size and in-flight depth.

Plan and report: README.ja.md
Run:  uv run --env-file .env pytest e155_p4_usb_jtag_roundtrip/e155_p4_usb_jtag_roundtrip.py -s --clean
"""

import re
import struct
import threading
import time

SIZES = [8, 64, 512, 4096]
DEPTHS = [1, 4, 16]


def _frames_for(size):
    return 64 if size == 4096 else 256 if size == 512 else 512


class Echo:
    """Reader thread parses echoed frames; the sender waits on a condition, not a spin."""

    def __init__(self, ser):
        self.ser = ser
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
            # read(1) returns on the first byte instead of waiting out the timeout.
            data = self.ser.read(1)
            if not data:
                continue
            waiting = self.ser.in_waiting
            if waiting:
                data += self.ser.read(waiting)
            self.buf += data
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

    def run(self, size, depth):
        n = _frames_for(size)
        frames = []
        for seq in range(n):
            payload = struct.pack("<H", seq) + bytes((seq + i) & 0xFF for i in range(size - 2))
            frames.append(struct.pack("<H", size) + payload)
        with self.cond:
            self.expected = frames
            self.received = 0
            self.mismatch = 0
            self.buf.clear()
        stalled = False
        t0 = time.perf_counter()
        sent = 0
        while sent < n and not stalled:
            with self.cond:
                if not self.cond.wait_for(lambda: sent - self.received < depth, timeout=5):
                    stalled = True
                    break
            try:
                self.ser.write(frames[sent])
            except Exception:  # pyserial SerialTimeoutException: device stopped accepting
                stalled = True
                break
            sent += 1
        with self.cond:
            self.cond.wait_for(lambda: self.received >= sent, timeout=5 if stalled else 20)
        t1 = time.perf_counter()
        return n, sent, self.received, self.mismatch, t1 - t0, stalled


def test_p4_usb_jtag_roundtrip(dut):
    dut.write("?")
    dut.expect(re.compile(rb"# EXP E155 v1 git=\S+ probe=esp32p4_x035 target=none rx=8192 tx=8192"), timeout=30)
    dut.expect_exact("ECHO READY", timeout=5)
    # Opening or closing the USB-Serial/JTAG port resets the P4, so the binary
    # phase reuses the harness's own pyserial handle and only pauses its reader.
    thread = dut.serial._redirect_thread
    thread.stop_reading()
    time.sleep(0.2)
    ser = dut.serial.proc
    ser.timeout = 0.02
    ser.write_timeout = 2.0
    ser.reset_input_buffer()
    print("E155 binary phase start", flush=True)
    # Reference: synchronous write -> blocking read in one thread, no reader
    # thread. This is the round trip the transport itself imposes.
    for size in (8, 512):
        frame = struct.pack("<H", size) + bytes(i & 0xFF for i in range(size))
        ser.timeout = 2.0
        samples = []
        for _ in range(100):
            t0 = time.perf_counter()
            ser.write(frame)
            got = ser.read(size + 2)
            samples.append(time.perf_counter() - t0)
            assert got == frame, f"reference echo mismatch at size={size}"
        samples.sort()
        print(f"E155 REF size={size} frames=100 median_us={samples[50] * 1e6:.0f} "
              f"min_us={samples[0] * 1e6:.0f} p95_us={samples[94] * 1e6:.0f}", flush=True)
    ser.timeout = 0.02
    echo = Echo(ser)
    rows = []
    end_line = b""
    stalled_at = ""
    try:
        for size in SIZES:
            for depth in DEPTHS:
                if stalled_at:
                    print(f"E155 size={size} depth={depth} skipped after stall at {stalled_at}", flush=True)
                    continue
                n, sent, got, bad, total, stalled = echo.run(size, depth)
                us = total * 1e6 / max(got, 1)
                kbs = got * (size + 2) / total / 1000
                rows.append((size, depth, n, sent, got, bad, total, us, kbs, stalled))
                print(f"E155 size={size} depth={depth} frames={n} sent={sent} received={got} mismatch={bad} "
                      f"total_s={total:.4f} us_per_frame={us:.0f} kB_s={kbs:.1f} stalled={int(stalled)}", flush=True)
                if stalled:
                    stalled_at = f"size={size} depth={depth}"
    finally:
        echo.stop = True
        echo.thread.join(timeout=2)
        ser.write(b"\x00\x00")
        ser.timeout = 1.0
        end_line = ser.read(200)
        ser.timeout = 0.05
        thread.start_reading()
    print(f"E155 device: {end_line.strip().decode(errors='replace')}", flush=True)
    # A stall leaves the device mid-frame, so ECHO END is only expected without one.
    if not stalled_at:
        assert b"ECHO END" in end_line
    for size, depth, n, sent, got, bad, total, us, kbs, stalled in rows:
        if not stalled:
            assert got == n, f"size={size} depth={depth}: received {got}/{n}"
            assert bad == 0, f"size={size} depth={depth}: {bad} mismatched echoes"
        # A stalled point records how the transport fails (dropped or corrupted
        # echoes); that is the observation, not a harness failure.
