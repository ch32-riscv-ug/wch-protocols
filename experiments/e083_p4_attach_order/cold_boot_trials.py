"""E083 host side: hard-reset the board repeatedly and read the first capture."""
import subprocess, sys, time, serial
ET = "/home/mt/.arduino15/internal/esp32_esptool_py_5.3.1_99ef3036ba521408/esptool"
PORT = "/run/board-identify/by-id/esp32-p4-30eda0e31478"
n = int(sys.argv[1])

def hard_reset():
    subprocess.run([ET, "--chip", "esp32p4", "--port", "/dev/ttyACM0", "--after", "hard-reset", "flash-id"],
                   capture_output=True, timeout=120)

def read_boot(timeout=25):
    for _ in range(50):
        try:
            p = serial.Serial(PORT, 115200, timeout=2); break
        except Exception:
            time.sleep(0.5)
    else:
        return None
    end = time.time() + timeout
    try:
        while time.time() < end:
            line = p.readline()
            if line.startswith(b"BOOT"):
                return line.decode(errors="replace").strip()
    finally:
        p.close()
    return None

dead = 0
seen = 0
lanes_dead = {}
for i in range(n):
    hard_reset()
    time.sleep(1.5)
    boot = read_boot()
    if boot is None:
        print(f"  {i+1}: no BOOT line"); continue
    seen += 1
    fields = dict(x.split("=", 1) for x in boot.split()[1:])
    mark = ""
    if fields["dead"] != "0x00":
        dead += 1
        lanes_dead[fields["dead"]] = lanes_dead.get(fields["dead"], 0) + 1
        mark = "  ***"
    print(f"  {i+1}: dead={fields['dead']} overflow={fields['overflow']} edges={fields['edges']}{mark}")
print(f"cold boots={seen} dead={dead} {lanes_dead}")
