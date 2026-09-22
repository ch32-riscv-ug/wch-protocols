# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""E161: does the UIAPduino bootloader stay only when RCC_RSTSCKR.PINRSTF is set? Clears the flags (RMVF),
runs the E129 boot payload, watches Windows usbipd for 1209:b803; then NRST pulse + payload as the control."""
import struct, subprocess, sys, time
CLIENT = "/home/mt/dev_oep/oep-client-python/src"; sys.path.insert(0, CLIENT)
from oep_client.v0 import codec
from oep_client.v0.__main__ import open_client
from oep_client.v0.flash_image import Target
PORT = sys.argv[1] if len(sys.argv) > 1 else "/run/board-identify/by-id/esp32-d0wd-v3-0070070d9394"
NAMES = {24: "RMVF", 26: "PINRSTF", 27: "PORRSTF", 28: "SFTRSTF", 29: "IWDGRSTF"}
def usb():
    out = subprocess.run(["usbipd.exe", "list"], capture_output=True, text=True).stdout
    return [l.strip()[:40] for l in out.splitlines() if "1209" in l or "0000:0002" in l]
def watch(sec):
    for i in range(sec):
        time.sleep(1.0); seen = usb()
        if any("1209" in s for s in seen): print(f"  +{i+1} s HID appeared:", seen); return True
    print(f"  no 1209 within {sec} s; last:", usb()); return False
c = open_client(PORT, 3.0); t = Target(c)
def flags():
    t.control.halt(); v = t.memory.read_word(0x40021024); return "[" + " ".join(n for b, n in NAMES.items() if v >> b & 1) + "]"
print("start:", flags(), usb())
t.control.halt(); v = t.memory.read_word(0x40021024)
c.call(t.memory.function, codec.TARGET_MEMORY_OP_WRITE, codec.TargetMemoryWriteRequest(address=0x40021024, data=struct.pack("<I", v | 1 << 24)).pack()).expect_success("rmvf")
print("after RMVF:", flags())
ok, _ = t.control.reset_report(1); print("boot payload without PINRSTF ->", ok); a = watch(8); print("   flags:", flags())
ok, _ = t.control.reset_report(3); time.sleep(0.5); ok, _ = t.control.reset_report(1); print("NRST pulse + boot payload ->", ok); b = watch(8)
print("RESULT: without PINRSTF ->", a, "; with PINRSTF ->", b)
