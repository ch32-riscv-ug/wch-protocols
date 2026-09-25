"""Does cutting a LinkE power output unpower the target? Watch the target's UART stream across off / on.

usage: power_check.py <LinkE serial> <tty> [3v3|5v]   (always turns the output back on at the end)
"""
import subprocess, sys, time
import serial

CH = "/home/mt/dev_wch/ch32rv/target/release/ch32rv"
sn, tty = sys.argv[1:3]
rail = sys.argv[3] if len(sys.argv) > 3 else "3v3"


def power(state):
    p = subprocess.run([CH, "--probe", f"serial:{sn}", "--non-interactive", "probe", "power", rail, state],
                       capture_output=True, text=True)
    print(f"  {rail} {state}: rc={p.returncode} {p.stderr.strip()[-200:]}")


def count(ser, secs):
    n = 0; t = time.time()
    while time.time() - t < secs:
        n += len(ser.read(4096))
    return n


ser = serial.Serial(tty, 115200, timeout=0.05)
try:
    print(f"  bytes/1s before: {count(ser, 1.0)}")
    power("off"); ser.reset_input_buffer()
    count(ser, 0.3)  # settle
    print(f"  bytes/1s with the output off: {count(ser, 1.0)}")
finally:
    power("on")
    time.sleep(0.5); ser.reset_input_buffer()
    print(f"  bytes/1s after the output is back on: {count(ser, 1.0)}")
    ser.close()
