#!/usr/bin/env python3
"""Prepare a normal Pocket ELF for renderer replay on the real CPU RTL.

Requires pyelftools and the SDK toolchain container. Does not instrument or
relink the application. Calls its existing symbols from a separate driver.
Use openfpgaOS/tools/build_pocket_renderer_replay.py to build the simulator.
"""
from pathlib import Path
from elftools.elf.elffile import ELFFile
import argparse,hashlib,json,re,subprocess,struct
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--mode',choices=['renderer','submission','bands','planes'],default='renderer')
args=parser.parse_args();elfpath=args.elf.resolve();p=args.output.resolve()
if not p.is_relative_to(root):
 parser.error('--output must be inside the Doom checkout mounted by the SDK container')
p.mkdir(parents=True,exist_ok=False)
driver=root/'tools/tests'/dict(renderer='pocket_renderer_replay.c',submission='pocket_gpu_submission.c',bands='pocket_gpu_bands.c',planes='pocket_gpu_planes.c')[args.mode]
if args.mode!='renderer':
 from gpu_test_sources import gpu_types
 (p/'gpu_types.h').write_text(gpu_types((root/'src/sdk/include/of_gpu.h').read_text()))
with elfpath.open('rb')as f:
 e=ELFFile(f);assert e.elfclass==32 and e['e_machine']=='EM_RISCV'
 symbols={s.name:(s['st_value'],s['st_size'])for s in e.get_section_by_name('.symtab').iter_symbols()}
 segments=[(s['p_vaddr'],s.data())for s in e.iter_segments()if s['p_type']=='PT_LOAD'and s['p_filesz']]
 assert symbols['gpu_plane_params'][1]==116
 assert symbols['gpu_wall_tiers'][1]==12568
 assert all(s['sh_addr']+s['sh_size']<=0x13800000 for s in e.iter_sections()if s['sh_flags']&2 and s['sh_addr']>=0x10000000)
names=set(re.findall(r'\b[UPF]\((\w+)',driver.read_text()))-{'n'}
if 'R_GPU_BeginView' not in symbols:
 symbols['R_GPU_BeginView']=(0,0)  # release ELFs before the per-view setup hook
missing=names-symbols.keys()
if args.mode=='submission' and '_gpu_emit_param_span_list' in missing:
 parser.error('This ELF split/inlined the private emitter; use --mode bands to measure public end-surface submission without assuming an internal ABI')
assert not missing,missing
(p/'symbols.h').write_text(''.join(f'#define A_{n} 0x{symbols[n][0]:08x}u\n#define SIZE_{n} {symbols[n][1]}u\n' for n in sorted(names)))
start=f'''.section .text.start,"ax"
.global _start
_start:
 li sp,0x103fffe0
 li gp,0x{symbols['__global_pointer$'][0]:08x}
 li t0,0x6000
 csrs mstatus,t0
 la t0,trap
 csrw mtvec,t0
 csrw fcsr,zero
 call main
1: j 1b
.balign 4
trap:
 csrr a0,mcause
 csrr a1,mepc
 csrr a2,mtval
 call trap_report
 j 1b
'''
(p/'start.S').write_text(start)
(p/'link.ld').write_text('ENTRY(_start)\nSECTIONS { . = 0x10320000; .text : { *(.text.start) *(.text*) *(.rodata*) } .data : { *(.data*) *(.sdata*) } .bss : { *(.bss*) *(.sbss*) } }\n')
mk=f'''all:
\triscv64-unknown-elf-gcc -march=rv32imafc_zicsr_zifencei -mabi=ilp32f -O2 -msmall-data-limit=0 -ffreestanding -fno-builtin -nostdlib -nostartfiles -nostdinc -isystem {root}/src/sdk/musl/include -I{root}/src/sdk/include -I{p} -Wl,-T,{p}/link.ld -Wl,--no-relax {p}/start.S {driver} -lgcc -o {p}/driver.elf
'''
(p/'driver.mk').write_text(mk)
with (p/'build.log').open('w')as log:subprocess.run(['bash',str(root/'tools/sdk-container.sh'),'make','-f',str(p/'driver.mk')],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
with (p/'driver.elf').open('rb')as f:
 e=ELFFile(f);segments +=[(s['p_vaddr'],s.data())for s in e.iter_segments()if s['p_type']=='PT_LOAD'and s['p_filesz']]
base=0x10320000;end=max(a+len(d)for a,d in segments if a>=base)
buf=bytearray(end-base);bram=bytearray(32768)
struct.pack_into('<II',bram,0,0x103202b7,0x00028067)
for a,data in segments:
 if a<32768:
  assert a+len(data)<=32768
  bram[a:a+len(data)]=data
 else:
  assert a>=base
  buf[a-base:a-base+len(data)]=data
(p/'replay.bin').write_bytes(buf)
(p/'firmware.mif').write_text('WIDTH=32;\nDEPTH=8192;\nADDRESS_RADIX=DEC;\nDATA_RADIX=HEX;\nCONTENT BEGIN\n'+''.join(f'{n} : {v:08X};\n'for n,(v,)in enumerate(struct.iter_unpack('<I',bram)))+'END;\n')
(p/'manifest.json').write_text(json.dumps(dict(elf=str(elfpath),sha256=hashlib.sha256(elfpath.read_bytes()).hexdigest(),driver_sha256=hashlib.sha256(driver.read_bytes()).hexdigest(),symbols={n:symbols[n]for n in sorted(names)},mode=args.mode,scope='Production ELF CPU work on Pocket RTL; no complete game/GPU/audio timing'),indent=2)+'\n')
print('Ready',p,len(buf),'bytes',flush=True)
