"""E015: compare LEDC/PARLIO initialization orders on ESP32-P4.

Plan and report: README.ja.md

Run: uv run --env-file .env pytest \
    e015_p4_parlio_routing_order/e015_p4_parlio_routing_order.py
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E015 v1 git=\S+ probe=esp32p4_parlio target=internal build=[^\r\n]+"
)
MAP = re.compile(
    rb"MAP variant=(\S+) stage=(\S+) lane=(\d+) pin=(\d+) result=(\S+) "
    rb"sig_out=(\d+) input_en=(\d+) output_en=(\d+) oe_periph=(\d+)\r?\n"
)
LANE = re.compile(
    rb"LANE variant=(\S+) run=(\d+) lane=(\d+) pin=(\d+) duty=(\d+) "
    rb"high=(\d+) low=(\d+) edges=(\d+) ratio_ppm=(\d+)\r?\n"
)
VARIANTS = ("ledc-first", "parlio-first")
RUNS = 3
LANES = 8
SAMPLES = 8192
RATIO_TOLERANCE_PPM = 30000


def test_routing_orders(dut):
    """At least one public-API initialization order preserves both paths."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    variant_results = {}
    observations = []
    for variant in VARIANTS:
        dut.expect_exact(f"VARIANT name={variant}", timeout=5)
        dut.expect_exact(
            f"STAGE variant={variant} stage=first result=ESP_OK", timeout=5
        )

        first_maps = []
        for expected_lane in range(LANES):
            match = dut.expect(MAP, timeout=5)
            values = [value.decode() for value in match.groups()]
            assert values[0] == variant and values[1] == "first"
            assert int(values[2]) == expected_lane
            first_maps.append(tuple(values))
        dut.expect_exact(f"DUMP_DONE variant={variant} stage=first", timeout=5)

        dut.expect_exact(
            f"STAGE variant={variant} stage=second result=ESP_OK", timeout=5
        )
        final_maps = []
        for expected_lane in range(LANES):
            match = dut.expect(MAP, timeout=5)
            values = [value.decode() for value in match.groups()]
            assert values[0] == variant and values[1] == "final"
            assert int(values[2]) == expected_lane
            final_maps.append(tuple(values))
        dut.expect_exact(f"DUMP_DONE variant={variant} stage=final", timeout=5)

        for expected_lane in range(LANES):
            dut.expect_exact(
                f"PWM variant={variant} lane={expected_lane} actual_hz=100000",
                timeout=5,
            )

        lane_ok = True
        for expected_run in range(RUNS):
            dut.expect_exact(
                f"CAPTURE variant={variant} run={expected_run} result=ESP_OK",
                timeout=5,
            )
            for expected_lane in range(LANES):
                match = dut.expect(LANE, timeout=5)
                (
                    got_variant,
                    run,
                    lane,
                    pin,
                    duty,
                    high,
                    low,
                    edges,
                    ratio_ppm,
                ) = match.groups()
                got_variant = got_variant.decode()
                run, lane, pin, duty, high, low, edges, ratio_ppm = (
                    int(value)
                    for value in (run, lane, pin, duty, high, low, edges, ratio_ppm)
                )
                assert got_variant == variant
                assert run == expected_run and lane == expected_lane
                expected_ratio = duty * 1_000_000 // 256
                error = abs(ratio_ppm - expected_ratio)
                sample_ok = (
                    high + low == SAMPLES
                    and high > 0
                    and low > 0
                    and edges >= 2
                    and error <= RATIO_TOLERANCE_PPM
                )
                lane_ok &= sample_ok
                observations.append(
                    (variant, run, lane, pin, duty, ratio_ppm, edges, sample_ok)
                )

        dut.expect_exact(f"VARIANT_DONE name={variant}", timeout=5)
        mapping_ok = all(
            int(values[5]) == 126 + lane and int(values[6]) == 1
            for lane, values in enumerate(final_maps)
        )
        variant_results[variant] = mapping_ok and lane_ok
        print(
            f"\nE015 {variant}: mapping_ok={mapping_ok} lane_ok={lane_ok} "
            f"first_sig_out={[row[5] for row in first_maps]} "
            f"final_sig_out={[row[5] for row in final_maps]}"
        )

    dut.expect_exact("DONE", timeout=5)
    for observation in observations:
        print(
            "variant=%s run=%d lane=%d pin=%d duty=%d "
            "ratio_ppm=%d edges=%d ok=%s" % observation
        )
    assert any(variant_results.values()), (
        f"no public-API routing order preserved both paths: {variant_results}"
    )
