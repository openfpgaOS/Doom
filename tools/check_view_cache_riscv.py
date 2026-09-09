#!/usr/bin/env python3
"""Replay complete bbox request windows through production RV32 ELFs.

Requires pyelftools and unicorn. Input comes from trace_view_cache.py. Both
machines start each window with a cold cache. Counts include R_ClearClipSegs
once per captured frame and R_BBoxPointAngle with all its callees; they exclude
vertex caches and all other rendering work. This is not FPGA cycle timing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_scale_riscv import ScaleMachine
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1,
                                UC_RISCV_REG_GP, UC_RISCV_REG_SP,
                                UC_RISCV_REG_RA, UC_RISCV_REG_PC)


class ViewMachine(ScaleMachine):
    def write(self, name, value):
        self.cpu.mem_write(self.symbols[name], struct.pack('<I', value))

    def cold(self):
        self.cpu.mem_write(self.symbols['bbox_angle_cache'], bytes(4096 * 16))
        if 'bsp_view_valid' in self.symbols:
            self.write('bsp_view_valid', 0)
        self.write('wallmerge_enabled', 0)
        self.write('viewwidth', 320)
        self.write('numvertexes', 0)

    def instruction(self, cpu, address, size, data):
        super().instruction(cpu, address, size, data)
        if address == self.symbols['R_PointToAngleBSP']:
            self.counts['angle_calculations'] += 1

    def invoke(self, name, x=0, y=0):
        self.cpu.reg_write(UC_RISCV_REG_A0, x)
        self.cpu.reg_write(UC_RISCV_REG_A1, y)
        self.cpu.reg_write(UC_RISCV_REG_GP, self.symbols['__global_pointer$'])
        self.cpu.reg_write(UC_RISCV_REG_SP, self.SCRATCH + 0xf000)
        self.cpu.reg_write(UC_RISCV_REG_RA, self.STOP)
        self.counts = dict(instructions=0, divide_instructions=0,
                           multiply_instructions=0, angle_calculations=0)
        self.cpu.emu_start(self.symbols[name], self.STOP, count=10000)
        if self.cpu.reg_read(UC_RISCV_REG_PC) != self.STOP:
            raise RuntimeError(f'{name} did not return')
        return self.cpu.reg_read(UC_RISCV_REG_A0), self.counts

    def frame(self, vx, vy, stamp):
        self.write('viewx', vx)
        self.write('viewy', vy)
        self.write('validcount', stamp)
        return self.invoke('R_ClearClipSegs')[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--samples', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    machines = {k: ViewMachine(p) for k, p in [('before', args.before), ('after', args.after)]}
    report = dict(scope=__doc__, samples={}, elf_sha256={
        k: hashlib.sha256(p.read_bytes()).hexdigest() for k, p in [('before', args.before), ('after', args.after)]})
    for path in args.samples:
        records = list(struct.iter_unpack('<8I', path.read_bytes()))
        if not records:
            raise ValueError(f'No records: {path}')
        totals = {k: dict(instructions=0, divide_instructions=0,
                          multiply_instructions=0, angle_calculations=0) for k in machines}
        frames = points = 0
        window = frame = None
        for kind, tic, vx, vy, stamp, x, y, expected in records:
            current_window = 0 if tic < 64 else 1
            if current_window != window:
                for m in machines.values(): m.cold()
                window, frame = current_window, None
            if kind == 0:
                frame = (tic, vx, vy, stamp)
                frames += 1
            elif kind == 1:
                if frame != (tic, vx, vy, stamp):
                    raise ValueError('Point without its complete frame header')
                points += 1
            else:
                raise ValueError(f'Unknown record kind: {kind}')
            for key, m in machines.items():
                if kind == 0:
                    counts = m.frame(vx, vy, stamp)
                else:
                    actual, counts = m.invoke('R_BBoxPointAngle', x, y)
                    if actual != expected:
                        raise RuntimeError(f'{key} angle mismatch at tic {tic}: {actual} != {expected}')
                for counter, value in counts.items(): totals[key][counter] += value
        report['samples'][str(path)] = dict(frames=frames, matching_points=points, totals=totals,
                                             sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        print(path, frames, 'frames', points, 'matching points', totals, flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
