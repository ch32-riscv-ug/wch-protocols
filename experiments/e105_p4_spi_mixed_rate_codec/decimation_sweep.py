#!/usr/bin/env python3
"""Print the block and bandwidth cost of three raw lanes plus eight slow lanes."""

from codec import Channel, calculate_budget

BASE_RATE = 60_000_000

print("D block_samples block_us wire_MB_s saving_vs_11bit_pct output_block_bytes")
for divisor in (1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4096, 16384, 65536):
    mode = "raw" if divisor == 1 else "any_active"
    budget = calculate_budget(
        BASE_RATE,
        [Channel("raw"), Channel("raw"), Channel("raw")]
        + [Channel(mode, divisor) for _ in range(8)],
    )
    raw_rate = BASE_RATE * 11 / 8
    print(
        f"{divisor} {budget.block_samples} "
        f"{budget.block_samples / BASE_RATE * 1e6:.6f} "
        f"{budget.wire_bytes_per_second / 1e6:.6f} "
        f"{(1 - budget.wire_bytes_per_second / raw_rate) * 100:.6f} "
        f"{budget.wire_bytes_per_block}"
    )
