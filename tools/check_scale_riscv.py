#!/usr/bin/env python3
"""Replay recorded wall scales through production RV32 ELFs using Unicorn.

Requires pyelftools and unicorn. Counts executed instructions, not FPGA cycles.
Each sample is seven little-endian uint32s: visible angle, view angle, wall
normal angle, wall distance, projection, detail shift, expected scale.
Recorded by benchmark_doom.py's trace runs (scale.bin).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE
from unicorn.riscv_const import (UC_RISCV_REG_A0,
                                UC_RISCV_REG_GP, UC_RISCV_REG_SP,
                                UC_RISCV_REG_RA, UC_RISCV_REG_PC)


class ScaleMachine:
    SCRATCH = 0x30000000
    STOP = SCRATCH + 0x10000

    def __init__(self, path):
        self.cpu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
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
        self.entry = self.symbols['R_ScaleFromGlobalAngle']
        self.cpu.hook_add(UC_HOOK_CODE, self.instruction)

    def instruction(self, cpu, address, size, _):
        self.counts['instructions'] += 1
        if size == 4:
            op = int.from_bytes(cpu.mem_read(address, 4), 'little')
            if op & 0xfe00007f == 0x02000033:
                kind = 'divide_instructions' if ((op >> 12) & 7) >= 4 else 'multiply_instructions'
                self.counts[kind] += 1

    def run(self, sample):
        angle, view, normal, distance, projection, detail, expected = sample
        for name, value in [('viewangle', view), ('rw_normalangle', normal),
                            ('rw_distance', distance), ('projection', projection),
                            ('detailshift', detail)]:
            self.cpu.mem_write(self.symbols[name], struct.pack('<I', value))
        self.cpu.reg_write(UC_RISCV_REG_A0, angle)
        self.cpu.reg_write(UC_RISCV_REG_GP, self.symbols['__global_pointer$'])
        self.cpu.reg_write(UC_RISCV_REG_SP, self.SCRATCH + 0xf000)
        self.cpu.reg_write(UC_RISCV_REG_RA, self.STOP)
        self.counts = dict(instructions=0, divide_instructions=0, multiply_instructions=0)
        self.cpu.emu_start(self.entry, self.STOP, count=10000)
        if self.cpu.reg_read(UC_RISCV_REG_PC) != self.STOP:
            raise RuntimeError('Scale function did not return')
        actual = self.cpu.reg_read(UC_RISCV_REG_A0)
        if actual != expected:
            raise RuntimeError(f'Scale mismatch: {sample}, actual {actual}')
        return dict(self.counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True, help='Baseline production RV32 ELF')
    parser.add_argument('--after', type=Path, required=True, help='Candidate production RV32 ELF')
    parser.add_argument('--samples', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    machines = dict(before=ScaleMachine(args.before), after=ScaleMachine(args.after))
    report = dict(scope='Executed RV32 instructions in wall scaling; excludes caches, interrupts, GPU and pacing; not FPGA cycles or FPS',
                  elf_sha256={k: hashlib.sha256(p.read_bytes()).hexdigest()
                              for k, p in [('before', args.before), ('after', args.after)]}, samples={})
    for path in args.samples:
        records = list(struct.iter_unpack('<7I', path.read_bytes()))
        if not records:
            raise ValueError(f'No samples: {path}')
        counts = {k: dict(instructions=0, divide_instructions=0, multiply_instructions=0) for k in machines}
        call_instructions = {k: [] for k in machines}
        for sample in records:
            for key, machine in machines.items():
                measured = machine.run(sample)
                call_instructions[key].append(measured['instructions'])
                for counter, value in measured.items():
                    counts[key][counter] += value
        regressions = sum(a > b for b, a in zip(call_instructions['before'], call_instructions['after']))
        percentiles = {}
        for key, values in call_instructions.items():
            values.sort()
            percentiles[key] = dict(p50=values[(len(values) - 1) // 2],
                                    p99=values[(len(values) - 1) * 99 // 100],
                                    maximum=values[-1])
        report['samples'][str(path)] = dict(matching_samples=len(records), totals=counts,
            instructions_per_call=percentiles, samples_with_more_instructions=regressions,
            sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        print(path, len(records), 'PASS', counts, flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
