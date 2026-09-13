"""Reference codec for 3 fast SPI lanes plus one 8:1 CS lane."""

from __future__ import annotations

from dataclasses import dataclass
from math import lcm

BLOCK_SAMPLES = 64
RAW_BLOCK_BYTES = 32
WIRE_BLOCK_BYTES = 25


@dataclass(frozen=True)
class Channel:
    mode: str
    decimation: int = 1

    @property
    def bits_per_value(self) -> int:
        if self.mode in ("raw", "decimate_hold", "any_active"):
            return 1
        if self.mode == "edge_latch":
            return 2  # bucket-end level plus whether an active edge occurred
        raise ValueError(f"unknown channel mode: {self.mode}")


@dataclass(frozen=True)
class Budget:
    block_samples: int
    wire_bytes_per_block: int
    wire_bytes_per_second: float


def calculate_budget(base_rate_hz: int, channels: list[Channel]) -> Budget:
    if base_rate_hz <= 0 or not channels:
        raise ValueError("positive base rate and at least one channel are required")
    for channel in channels:
        if channel.decimation <= 0:
            raise ValueError("decimation must be positive")
        if channel.mode == "raw" and channel.decimation != 1:
            raise ValueError("raw channels must use decimation 1")
    block_samples = lcm(*(8 * channel.decimation for channel in channels))
    wire_bits = sum(
        block_samples // channel.decimation * channel.bits_per_value
        for channel in channels
    )
    assert wire_bits % 8 == 0
    wire_bytes = wire_bits // 8
    return Budget(
        block_samples=block_samples,
        wire_bytes_per_block=wire_bytes,
        wire_bytes_per_second=base_rate_hz / block_samples * wire_bytes,
    )


def _samples_from_raw(raw: bytes) -> list[int]:
    if len(raw) % RAW_BLOCK_BYTES:
        raise ValueError("raw byte count must be a multiple of 32")
    samples: list[int] = []
    for value in raw:
        samples.extend((value & 0x0F, (value >> 4) & 0x0F))
    return samples


def _raw_from_samples(samples: list[int]) -> bytes:
    return bytes(samples[i] | (samples[i + 1] << 4) for i in range(0, len(samples), 2))


def encode(raw: bytes, policy: str = "sample0") -> bytes:
    samples = _samples_from_raw(raw)
    output = bytearray(len(samples) // BLOCK_SAMPLES * WIRE_BLOCK_BYTES)
    destination = 0
    for block_start in range(0, len(samples), BLOCK_SAMPLES):
        block = samples[block_start : block_start + BLOCK_SAMPLES]
        accumulator = 0
        bits = 0
        fast = bytearray()
        for sample in block:
            accumulator |= (sample & 0x07) << bits
            bits += 3
            if bits >= 8:
                fast.append(accumulator & 0xFF)
                accumulator >>= 8
                bits -= 8
        assert len(fast) == 24 and bits == 0
        output[destination : destination + 24] = fast

        cs_byte = 0
        for bucket in range(8):
            values = [(block[bucket * 8 + index] >> 3) & 1 for index in range(8)]
            if policy == "sample0":
                cs = values[0]
            elif policy == "active_low_any":
                cs = int(all(values))
            else:
                raise ValueError(f"unknown CS policy: {policy}")
            cs_byte |= cs << bucket
        output[destination + 24] = cs_byte
        destination += WIRE_BLOCK_BYTES
    return bytes(output)


def decode(wire: bytes) -> bytes:
    if len(wire) % WIRE_BLOCK_BYTES:
        raise ValueError("wire byte count must be a multiple of 25")
    samples: list[int] = []
    for offset in range(0, len(wire), WIRE_BLOCK_BYTES):
        fast = wire[offset : offset + 24]
        cs_byte = wire[offset + 24]
        accumulator = 0
        bits = 0
        source = 0
        for index in range(BLOCK_SAMPLES):
            while bits < 3:
                accumulator |= fast[source] << bits
                source += 1
                bits += 8
            value = accumulator & 0x07
            accumulator >>= 3
            bits -= 3
            value |= ((cs_byte >> (index // 8)) & 1) << 3
            samples.append(value)
    return _raw_from_samples(samples)


def wire_bytes_for_samples(sample_count: int) -> int:
    if sample_count <= 0 or sample_count % BLOCK_SAMPLES:
        raise ValueError("sample count must be a positive multiple of 64")
    return sample_count // BLOCK_SAMPLES * WIRE_BLOCK_BYTES


def samples_for_budget(output_budget_bytes: int) -> int:
    return output_budget_bytes // WIRE_BLOCK_BYTES * BLOCK_SAMPLES
