#!/usr/bin/env python3
"""Replay recorded wall inputs through production RV32 ELFs using Unicorn.

Requires pyelftools and unicorn. Counts executed instructions, not FPGA cycles.
Each sample is nine little-endian uint32s: v1 x/y, v2 x/y, view x/y, half
length, expected distance, expected offset. All coordinates use 16.16 units.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1,
                                UC_RISCV_REG_GP, UC_RISCV_REG_SP,
                                UC_RISCV_REG_RA, UC_RISCV_REG_PC)


class WallMachine:
    SCRATCH = 0x30000000
    STOP = SCRATCH + 0x10000

    def __init__(self, path, reciprocal):
        self.cpu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.reciprocal = reciprocal
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            if elf.elfclass != 32 or elf['e_machine'] != 'EM_RISCV':
                raise ValueError("Expected a 32-bit RISC-V ELF")
            self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            segments = [(s['p_vaddr'], s['p_memsz'], s.data())
                        for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        ranges = []
        for addr, size, _ in sorted(segments):
            lo, hi = addr & ~4095, (addr + size + 4095) & ~4095
            if ranges and lo <= ranges[-1][1]:
                ranges[-1][1] = max(hi, ranges[-1][1])
            else:
                ranges.append([lo, hi])
        for lo, hi in ranges:
            self.cpu.mem_map(lo, hi - lo)
        for addr, _, data in segments:
            if data:
                self.cpu.mem_write(addr, data)
        self.cpu.mem_map(self.SCRATCH, 0x20000)
        names = [n for n in self.symbols if n.startswith('R_ExactDistOffset')]
        if len(names) != 1:
            raise ValueError(f"Expected one wall function, found {names}")
        self.entry = self.symbols[names[0]]
        self.cpu.hook_add(UC_HOOK_CODE, self.instruction)

    def instruction(self, cpu, address, size, _):
        self.counts['instructions'] += 1
        if size == 4:
            op = int.from_bytes(cpu.mem_read(address, 4), 'little')
            if op & 0xfe00007f == 0x02000033:
                kind = 'divide_instructions' if ((op >> 12) & 7) >= 4 else 'multiply_instructions'
                self.counts[kind] += 1

    def run(self, sample):
        x1, y1, x2, y2, vx, vy, length, expected_d, expected_o = sample
        if not length:
            raise ValueError("Wall lengths must be nonzero")
        base = self.SCRATCH
        self.cpu.mem_write(base, bytes(256))
        self.cpu.mem_write(base, struct.pack('<II', base + 0x100, base + 0x140))
        self.cpu.mem_write(base + 36, struct.pack('<I', length))
        if self.reciprocal:
            recip = (1 << 64) // length if length > 1 else 0
            self.cpu.mem_write(base + 40, struct.pack('<Q', recip))
        self.cpu.mem_write(base + 0x100, struct.pack('<II', x1, y1))
        self.cpu.mem_write(base + 0x140, struct.pack('<II', x2, y2))
        self.cpu.mem_write(self.symbols['viewx'], struct.pack('<I', vx))
        self.cpu.mem_write(self.symbols['viewy'], struct.pack('<I', vy))
        self.cpu.reg_write(UC_RISCV_REG_A0, base)
        self.cpu.reg_write(UC_RISCV_REG_A1, base + 0x180)
        self.cpu.reg_write(UC_RISCV_REG_GP, self.symbols['__global_pointer$'])
        self.cpu.reg_write(UC_RISCV_REG_SP, base + 0xf000)
        self.cpu.reg_write(UC_RISCV_REG_RA, self.STOP)
        self.counts = dict(instructions=0, divide_instructions=0, multiply_instructions=0)
        # The release compiler folds dist_out into rw_distance; offset_out is a1.
        self.cpu.emu_start(self.entry, self.STOP, count=10000)
        assert self.cpu.reg_read(UC_RISCV_REG_PC) == self.STOP, 'Function did not return'
        actual = (int.from_bytes(self.cpu.mem_read(self.symbols['rw_distance'], 4), 'little'),
                  int.from_bytes(self.cpu.mem_read(base + 0x180, 4), 'little'))
        assert actual == (expected_d, expected_o), (sample, actual)
        return dict(self.counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True, help='v1.1.23 MiSTer ELF')
    parser.add_argument('--after', type=Path, required=True, help='ELF with cached reciprocals')
    parser.add_argument('--samples', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    machines = dict(before=WallMachine(args.before, False), after=WallMachine(args.after, True))
    report = dict(scope='Executed RV32 instructions in wall setup; excludes caches, interrupts, GPU and pacing; not FPGA cycles or FPS',
                  elf_sha256={k: hashlib.sha256(p.read_bytes()).hexdigest()
                              for k, p in [('before', args.before), ('after', args.after)]}, samples={})
    for path in args.samples:
        records = list(struct.iter_unpack('<9I', path.read_bytes()))
        assert records, 'No samples'
        counts = {k: dict(instructions=0, divide_instructions=0, multiply_instructions=0) for k in machines}
        for sample in records:
            for key, machine in machines.items():
                for counter, value in machine.run(sample).items():
                    counts[key][counter] += value
        report['samples'][str(path)] = dict(matching_samples=len(records), totals=counts,
            sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        print(path, len(records), 'PASS', counts, flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
