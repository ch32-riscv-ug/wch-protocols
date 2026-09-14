"""Reference codecs and budgeting for mixed-rate digital channels."""

from __future__ import annotations

from dataclasses import dataclass
from math import gcd, lcm

BLOCK_SAMPLES = 64
RAW_BLOCK_BYTES = 32
WIRE_BLOCK_BYTES = 25


@dataclass(frozen=True)
class Channel:
    mode: str
    decimation: int = 1
    phase: int = 0
    active_level: int = 0

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
    payload_bits_per_block: int
    padding_bits_per_block: int
    wire_bytes_per_block: int
    wire_bytes_per_second: float


def calculate_budget(
    base_rate_hz: int,
    channels: list[Channel],
    block_samples: int | None = None,
) -> Budget:
    """Calculate a plane-major bitstream budget.

    Channel planes are adjacent at bit granularity.  Only the end of the whole
    block is padded to a byte boundary.  If ``block_samples`` is omitted, pick
    the smallest block that needs no padding.
    """
    if base_rate_hz <= 0 or not channels:
        raise ValueError("positive base rate and at least one channel are required")
    for channel in channels:
        if channel.decimation <= 0:
            raise ValueError("decimation must be positive")
        if channel.mode == "raw" and channel.decimation != 1:
            raise ValueError("raw channels must use decimation 1")
        if not 0 <= channel.phase < channel.decimation:
            raise ValueError("phase must be in [0, decimation)")
        if channel.active_level not in (0, 1):
            raise ValueError("active_level must be 0 or 1")
        channel.bits_per_value  # validate mode

    divisor_lcm = lcm(*(channel.decimation for channel in channels))
    bits_per_lcm = sum(
        divisor_lcm // channel.decimation * channel.bits_per_value
        for channel in channels
    )
    if block_samples is None:
        block_samples = divisor_lcm * (8 // gcd(bits_per_lcm, 8))
    elif block_samples <= 0 or any(
        block_samples % channel.decimation for channel in channels
    ):
        raise ValueError("block_samples must be a positive multiple of every decimation")

    wire_bits = sum(
        block_samples // channel.decimation * channel.bits_per_value
        for channel in channels
    )
    padding_bits = (-wire_bits) % 8
    wire_bytes = (wire_bits + padding_bits) // 8
    return Budget(
        block_samples=block_samples,
        payload_bits_per_block=wire_bits,
        padding_bits_per_block=padding_bits,
        wire_bytes_per_block=wire_bytes,
        wire_bytes_per_second=base_rate_hz / block_samples * wire_bytes,
    )


def _append_bits(output: bytearray, accumulator: int, bit_count: int, value: int, width: int) -> tuple[int, int]:
    accumulator |= value << bit_count
    bit_count += width
    while bit_count >= 8:
        output.append(accumulator & 0xFF)
        accumulator >>= 8
        bit_count -= 8
    return accumulator, bit_count


def encode_mixed(
    samples: list[int], channels: list[Channel], block_samples: int | None = None
) -> bytes:
    """Encode integer GPIO snapshots into shared, bit-packed channel planes."""
    budget = calculate_budget(1, channels, block_samples)
    if not samples or len(samples) % budget.block_samples:
        raise ValueError("sample count must be a positive multiple of block_samples")

    output = bytearray()
    for block_start in range(0, len(samples), budget.block_samples):
        block = samples[block_start : block_start + budget.block_samples]
        accumulator = 0
        bit_count = 0
        for lane, channel in enumerate(channels):
            mask = 1 << lane
            for bucket_start in range(0, budget.block_samples, channel.decimation):
                bucket = block[bucket_start : bucket_start + channel.decimation]
                if channel.mode in ("raw", "decimate_hold"):
                    value = int(bool(bucket[channel.phase] & mask))
                elif channel.mode == "any_active":
                    active = any(int(bool(sample & mask)) == channel.active_level for sample in bucket)
                    value = channel.active_level if active else 1 - channel.active_level
                elif channel.mode == "edge_latch":
                    levels = [int(bool(sample & mask)) for sample in bucket]
                    active_edge = any(
                        levels[index - 1] != channel.active_level
                        and levels[index] == channel.active_level
                        for index in range(1, len(levels))
                    )
                    value = levels[-1] | (int(active_edge) << 1)
                else:  # calculate_budget has already validated this
                    raise AssertionError(channel.mode)
                accumulator, bit_count = _append_bits(
                    output, accumulator, bit_count, value, channel.bits_per_value
                )
        if bit_count:
            output.append(accumulator & 0xFF)
        assert len(output) % budget.wire_bytes_per_block == 0
    return bytes(output)


def decode_mixed(
    wire: bytes, channels: list[Channel], block_samples: int | None = None
) -> list[int]:
    """Decode to the base-rate grid; coarse values are held for their bucket."""
    budget = calculate_budget(1, channels, block_samples)
    if not wire or len(wire) % budget.wire_bytes_per_block:
        raise ValueError("wire byte count must be a positive multiple of the wire block")

    result: list[int] = []
    for wire_start in range(0, len(wire), budget.wire_bytes_per_block):
        payload = wire[wire_start : wire_start + budget.wire_bytes_per_block]
        decoded = [0] * budget.block_samples
        bit_offset = 0
        for lane, channel in enumerate(channels):
            width = channel.bits_per_value
            for bucket_start in range(0, budget.block_samples, channel.decimation):
                byte_offset, shift = divmod(bit_offset, 8)
                value = payload[byte_offset] >> shift
                if shift + width > 8:
                    value |= payload[byte_offset + 1] << (8 - shift)
                value &= (1 << width) - 1
                # edge_latch bit 1 remains metadata; reconstructed GPIO uses end level.
                level = value & 1
                if level:
                    for index in range(bucket_start, bucket_start + channel.decimation):
                        decoded[index] |= 1 << lane
                bit_offset += width
        result.extend(decoded)
    return result


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
