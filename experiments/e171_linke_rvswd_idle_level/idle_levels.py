#!/usr/bin/env python3
"""Count the bus levels a WCH-LinkE rests RVSWD at: runs of an unchanged (SWCLK, SWDIO) at least MIN_US long.

usage: idle_levels.py [--min-us N] <file.sr>...   (SWCLK = bit 0, SWDIO = bit 1; unitsize from the .sr metadata)
Runs that touch the start or end of the recording are counted apart (edge): they are before or after the operation.
"""
import collections
import configparser
import re
import sys
import zipfile


def load(path):
    z = zipfile.ZipFile(path)
    meta = configparser.ConfigParser()
    meta.read_string(z.read('metadata').decode())
    dev = meta['device 1']
    rate = float(dev['samplerate'].split()[0]) * {'Hz': 1, 'kHz': 1e3, 'MHz': 1e6}[dev['samplerate'].split()[1]]
    unit = int(dev.get('unitsize', '1'))
    names = [n for n in z.namelist() if n.startswith(dev['capturefile'])]
    names.sort(key=lambda s: int(s.rsplit('-', 1)[1]) if s.rsplit('-', 1)[1].isdigit() else 0)
    data = b''.join(z.read(n) for n in names)[0::unit]   # low byte holds D0..D7
    return data, rate


def main(argv):
    min_us = 100.0
    if argv[:1] == ['--min-us']:
        min_us, argv = float(argv[1]), argv[2:]
    for path in argv:
        data, rate = load(path)
        n = int(min_us * 1e-6 * rate)
        inner, edge = collections.defaultdict(list), collections.defaultdict(list)
        for m in re.finditer(rb'(.)\1{%d,}' % (n - 1), data, re.S):
            level = m.group(1)[0] & 3
            ms = (m.end() - m.start()) / rate * 1e3
            (edge if m.start() == 0 or m.end() == len(data) else inner)[level].append(ms)
        def fmt(d):
            return ', '.join(f'SWCLK={k & 1} SWDIO={k >> 1 & 1}: {len(v)} runs, max {max(v):.2f} ms'
                             for k, v in sorted(d.items())) or 'none'
        print(f'{path}\t{len(data) / rate:.2f} s\tinner: {fmt(inner)}\tedge: {fmt(edge)}')


if __name__ == '__main__':
    main(sys.argv[1:])
