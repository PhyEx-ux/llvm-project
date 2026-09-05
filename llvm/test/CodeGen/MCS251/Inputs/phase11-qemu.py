#!/usr/bin/env python3
"""Optional MCS251 hardware-semantics/firmware checks; not a lit dependency.

Run in WSL with --work-dir /tmp/mcs251-phase11. All generated assets stay there.
Requires the locally built SDCC tools and QEMU, not an installed device library.
"""
import argparse
import pathlib
import subprocess
import sys

SIG = ('stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small '
       'stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 '
       'all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 '
       'compiler-build=mcs251-abi1.0-r1')


def run(cmd, **kwargs):
    print('+', ' '.join(map(str, cmd)), flush=True)
    return subprocess.run(list(map(str, cmd)), check=True, **kwargs)


def qemu(args, image):
    cmd = [args.qemu, '-M', 'stc32g144k246', '-bios', image,
           '-accel', 'tcg', '-icount', 'shift=0,align=off,sleep=off',
           '-display', 'none', '-monitor', 'none', '-serial', 'stdio']
    proc = subprocess.Popen(list(map(str, cmd)), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    try:
        out, _ = proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.terminate()
        out, _ = proc.communicate(timeout=3)
    image.with_suffix('.qemu.log').write_bytes(out)
    print(out.decode(errors='replace'), flush=True)
    assert b'PASS\n' in out.replace(b'\r\n', b'\n') and b'FAIL' not in out, image


def assemble(args, source):
    obj = source.with_suffix('.rel')
    run([args.tools / 'sdas251', '-plosgffw', '-o', obj, source])
    return obj


def link(args, name, objects):
    base = args.work_dir / name
    script = base.with_suffix('.lk')
    script.write_text('\n'.join(['-i', '-m', '-A ' + SIG, '-b HOME=0xff0000',
                                  '-b GSINIT0=0xfc2800', str(base)] +
                                 list(map(str, objects)) + ['-e', '']))
    run([args.tools / 'sdld', '--mcs251-abi', '-r', '-nf', script])
    image = base.with_suffix('.hex')
    image.write_bytes(base.with_suffix('.ihx').read_bytes())
    return image


def emit_text(text):
    return [line for c in text for line in
            [f'mov r12, #0x{ord(c):02x}', 'mov 0x99, r12']]


def isa(args):
    lines = ['.module phase11_isa', '.source', '.optsdcc ' + SIG,
             '.area HOME (CODE)', 'ejmp start', '.area GSINIT0 (CODE)', '.area CSEG (CODE)',
             'start:', 'mov spx, #0x2fff']
    pairs = [(0, 0), (0, 1), (1, 0), (0xffffffff, 0), (0, 0xffffffff),
             (0x80000000, 1), (0x7fffffff, 0xffffffff),
             (0x80000000, 0x7fffffff), (0x7fffffff, 0x80000000),
             (0x12345678, 0x12345678), (0x10000, 0xffff),
             (0x80000001, 0x80000000)]
    checks = 0
    signed = lambda x: x if x < 0x80000000 else x - 0x100000000
    for a, b in pairs:
        lines += [f'mov dr0, #0x{a & 65535:04x}', f'movh dr0, #0x{a >> 16:04x}',
                  f'mov dr4, #0x{b & 65535:04x}', f'movh dr4, #0x{b >> 16:04x}']
        expected = [('je', a == b), ('jne', a != b), ('jc', a < b),
                    ('jnc', a >= b), ('jg', a > b), ('jle', a <= b),
                    ('jsl', signed(a) < signed(b)), ('jsge', signed(a) >= signed(b)),
                    ('jsg', signed(a) > signed(b)), ('jsle', signed(a) <= signed(b))]
        for branch, take in expected:
            label = f'check{checks}'
            lines += emit_text(f'{checks:03d}\n')
            lines += ['cmp dr0, dr4', f'{branch} {label}']
            if take:
                lines += ['ejmp fail', label + ':']
            else:
                lines += [f'ejmp {label}ok', label + ':', 'ejmp fail', label + 'ok:']
            checks += 1
    # Displacements are signed 16-bit. Cross a 64K region boundary, in both
    # directions, and distinguish sign extension from zero extension.
    for base, disp, expected in [(0x1ffff, 1, 0x20000), (0x20000, -1, 0x1ffff),
                                 (0x18020, -32768, 0x10020),
                                 (0x10020, 32767, 0x1801f)]:
        lines += emit_text(f'{checks:03d}\n')
        lines += [f'mov dr0, #0x{base & 65535:04x}', f'movh dr0, #0x{base >> 16:04x}',
                  'mov r7, #0x5a', f'mov @dr0{disp:+d}, r7',
                  f'mov dr4, #0x{expected & 65535:04x}', f'movh dr4, #0x{expected >> 16:04x}',
                  'mov r6, @dr4', 'cmp r6, #0x5a', f'je mem{checks}',
                  'ejmp fail', f'mem{checks}:']
        checks += 1
    # Candidate frame anchor: DR16 has no allocatable byte lanes, but native
    # DR moves, CMP and indexed addressing must all work independently.
    lines += emit_text('DR16\n')
    lines += ['mov dr16, dr60', 'mov dr0, dr16', 'cmp dr0, dr60',
              'je anchor_copy', 'ejmp fail', 'anchor_copy:',
              'mov r7, #0x69', 'mov @dr16-2, r7', 'mov r6, @dr60-2',
              'cmp r6, #0x69', 'je anchor_mem', 'ejmp fail', 'anchor_mem:',
              'push dr16', 'mov dr16, #0x1234', 'pop dr16',
              'cmp dr16, dr60', 'je anchor_restore', 'ejmp fail', 'anchor_restore:']
    checks += 3
    lines += emit_text('PASS\n') + ['halt: sjmp halt', 'fail:']
    lines += emit_text('FAIL\n') + ['sjmp halt']
    source = args.work_dir / 'isa.asm'
    source.write_text('\n'.join(lines) + '\n')
    qemu(args, link(args, 'isa', [assemble(args, source)]))
    print(f'ISA assertions passed: {checks} (120 CMP32 branches, 4 DR addresses, 3 DR16 anchor checks)')


def compile_c(args, source):
    asm = source.with_suffix('.asm')
    pre = subprocess.check_output(['cpp', '-P', '-undef', '-nostdinc', str(source)])
    run([args.tools / 'sdcc', '-mmcs251', '--c1mode', '-o', asm], input=pre)
    return assemble(args, asm)


def comparisons(args):
    test_dir = pathlib.Path(__file__).resolve().parent.parent
    root = test_dir.parents[3]
    firmware = root / 'validation/mcs251-firmware'
    crt = args.work_dir / 'crt0.asm'
    crt.write_text((firmware / 'crt0.asm').read_text())
    crt_obj = assemble(args, crt)
    preds = ['eq', 'ne', 'ult', 'uge', 'ugt', 'ule', 'slt', 'sge', 'sgt', 'sle']
    decls = ['extern unsigned char ' + p + '(unsigned long);' for p in preds]
    decls += ['extern unsigned long select32(unsigned long);',
              'extern unsigned char branch32(unsigned long);']
    checks = []
    for x in [0, 1, 305419895, 305419896, 305419897, 0x7fffffff, 0x80000000, 0xffffffff]:
        y = x if x < 0x80000000 else x - 0x100000000
        k = 305419896
        results = [x == k, x != k, x < k, x >= k, x > k, x <= k,
                   y < k, y >= k, y > k, y <= k]
        for p, expected in zip(preds, results):
            checks.append(f'harness_check_u8({int(expected)}, {p}(0x{x:08x}UL));')
        checks += [f'harness_check_u32(0x{(x if x < k else 123456789):08x}UL, select32(0x{x:08x}UL));',
                   f'harness_check_u8({42 if x < k else 17}, branch32(0x{x:08x}UL));']
    source = args.work_dir / 'compare-harness.c'
    source.write_text('\n'.join(decls) + '\n#define MCS251_CHECKPOINTS() do { ' +
                      ' '.join(checks) + ' } while (0)\n' +
                      '#include "' + str(firmware / 'harness-template.c') + '"\n')
    harness = compile_c(args, source)
    # The IR-level ABI names are explicit (the backend does not add '_').
    module = args.work_dir / 'compare.ll'
    module.write_text((test_dir / 'i32-compare.ll').read_text().replace('@', '@_'))
    for opt in [0, 2]:
        for kind in ['asm', 'obj']:
            name = f'compare-O{opt}-{kind}'
            output = args.work_dir / (name + ('.asm' if kind == 'asm' else '.rel'))
            run([args.llc, '-mtriple=mcs251', '-verify-machineinstrs', f'-O{opt}',
                 f'-filetype={kind}', module, '-o', output])
            obj = assemble(args, output) if kind == 'asm' else output
            qemu(args, link(args, name, [crt_obj, harness, obj]))


def pointers(args):
    test_dir = pathlib.Path(__file__).resolve().parent.parent
    firmware = test_dir.parents[3] / 'validation/mcs251-firmware'
    provider = args.work_dir / 'pointer-provider.asm'
    provider.write_text('.module pointer_provider\n.source\n.optsdcc ' + SIG +
                        '\n.globl _far, _slot\n_far = 0x01fff0\n_slot = 0x010300\n.area CSEG (CODE)\n')
    provider_obj = assemble(args, provider)
    source = args.work_dir / 'pointer-harness.c'
    source.write_text('''
extern unsigned long pointer_size(void);
extern unsigned long global_pointer(void);
extern unsigned long global_carry(void);
extern unsigned char ptr_inc(unsigned char *);
extern unsigned char ptr_neg(unsigned char *);
extern unsigned char ptr_big(unsigned char *);
extern unsigned long ptr_index(int);
extern unsigned char ptr_branch(unsigned char *);
extern unsigned char ptr_roundtrip(unsigned char *);
extern unsigned long load32(unsigned char *);
extern void store32(unsigned long);
extern unsigned char stack_address(void);
__sfr __at (0xe0) ACC;
__xdata __at (0x020000) volatile unsigned char upper;
__xdata __at (0x01ffff) volatile unsigned char lower;
__xdata __at (0x010200) volatile unsigned char word[4];
__xdata __at (0x010300) volatile unsigned char slotbytes[4];
#define MCS251_CHECKPOINTS() do { \\
  harness_check_u32(4, pointer_size()); \\
  harness_check_u32(0x1fff0UL, global_pointer()); \\
  harness_check_u32(0x20010UL, global_carry()); \\
  upper = 0x5a; lower = 0xa5; \\
  harness_check_u8(0x5a, ptr_inc((unsigned char *)0x1ffffUL)); \\
  harness_check_u8(0xa5, ptr_neg((unsigned char *)0x20000UL)); \\
  harness_check_u8(0x5a, ptr_big((unsigned char *)0x10000UL)); \\
  harness_check_u32(0x2000cUL, ptr_index(3)); \\
  harness_check_u32(0x1fff8UL, ptr_index(-2)); \\
  ACC = 0xab; \\
  harness_check_u8(42, ptr_branch((unsigned char *)0x1ffffUL)); \\
  harness_check_u8(17, ptr_branch((unsigned char *)0x20000UL)); \\
  ACC = 0xcd; \\
  harness_check_u8(1, ptr_roundtrip((unsigned char *)0x20000UL)); \\
  harness_check_u8(0, slotbytes[0]); \\
  harness_check_u8(2, slotbytes[1]); \\
  harness_check_u8(0, slotbytes[2]); \\
  harness_check_u8(0, slotbytes[3]); \\
  store32(0x12345678UL); \\
  harness_check_u8(0x12, word[0]); \\
  harness_check_u8(0x34, word[1]); \\
  harness_check_u8(0x56, word[2]); \\
  harness_check_u8(0x78, word[3]); \\
  harness_check_u32(0x12345678UL, load32((unsigned char *)0x10200UL)); \\
  harness_check_u8(0x5a, stack_address()); \\
} while (0)
''' + '#include "' + str(firmware / 'harness-template.c') + '"\n')
    harness = compile_c(args, source)
    for opt in [0, 2]:
        images = []
        for kind in ['asm', 'obj']:
            name = f'pointer-O{opt}-{kind}'
            output = args.work_dir / (name + ('.asm' if kind == 'asm' else '.rel'))
            run([args.llc, '-mtriple=mcs251', '-verify-machineinstrs', f'-O{opt}',
                 f'-filetype={kind}', test_dir / 'pointer32.ll', '-o', output])
            obj = assemble(args, output) if kind == 'asm' else output
            image = link(args, name, [args.work_dir / 'crt0.rel', harness, obj, provider_obj])
            qemu(args, image)
            images.append(image)
        # HEX record boundaries may differ; compare the actual address->byte maps.
        def memory(path):
            result, base = {}, 0
            for line in path.read_text().splitlines():
                raw = bytes.fromhex(line[1:])
                size, address, kind = raw[0], int.from_bytes(raw[1:3], 'big'), raw[3]
                if kind == 4:
                    base = int.from_bytes(raw[4:6], 'big') << 16
                elif kind == 0:
                    result.update((base + address + i, x) for i, x in enumerate(raw[4:4+size]))
            return result
        assert memory(images[0]) == memory(images[1]), 'asm/obj linked byte mismatch'


def regressions(args):
    test_dir = pathlib.Path(__file__).resolve().parent.parent
    firmware = test_dir.parents[3] / 'validation/mcs251-firmware'
    source = args.work_dir / 'regression-harness.c'
    source.write_text('''
extern unsigned char indirect(unsigned char (*)(void));
typedef unsigned long (*long_fn)(unsigned long value);
extern unsigned long indirect_i32(long_fn fn);
extern unsigned char local_indirect(void);
extern unsigned char local_fn(void);
extern unsigned char (*local_address(void))(void);
extern unsigned char (*local_addend(void))(void);
extern unsigned long sdcc_pointer_result(void);
extern unsigned char sdcc_pointer_arg(void);
extern unsigned int external_word(unsigned int);
extern unsigned long edge_load(unsigned char *);
extern unsigned char edge_neg(unsigned char *);
extern unsigned char dyn_stack(unsigned int);
extern unsigned char nested_dyn(unsigned int);
extern unsigned int vla_loop(unsigned int);
extern unsigned int recurse(unsigned int);
__sfr __at (0xe0) ACC;
__xdata __at (0x020000) volatile unsigned char upper;
__xdata __at (0x01ffff) volatile unsigned char lower;
__xdata __at (0x010200) volatile unsigned char word[4];
__xdata __at (0x010400) volatile unsigned int external_scalar;
unsigned char target_byte(void) { return 93; }
unsigned long target_long(unsigned long x) { return x + 1; }
unsigned char *sdcc_pointer(void) { ACC = 0xab; return (unsigned char *)0x20000UL; }
unsigned char sdcc_reader(__xdata unsigned char *p) { return *p; }
#define MCS251_CHECKPOINTS() do { \\
  harness_check_u8(93, indirect(target_byte)); \\
  harness_check_u32(0x12345679UL, indirect_i32(target_long)); \\
  harness_check_u8(77, local_indirect()); \\
  harness_check_u32((unsigned long)local_fn, (unsigned long)local_address()); \\
  harness_check_u32((unsigned long)local_fn - 0x10000UL, (unsigned long)local_addend()); \\
  harness_check_u32(0x20000UL, sdcc_pointer_result()); \\
  upper = 0x5a; lower = 0xa5; \\
  harness_check_u8(0x5a, sdcc_pointer_arg()); \\
  harness_check_u16(0x1357, external_word(0x1357)); \\
  harness_check_u16(0x1357, external_scalar); \\
  word[0]=0x89; word[1]=0xab; word[2]=0xcd; word[3]=0xef; \\
  harness_check_u32(0x89abcdefUL, edge_load((unsigned char *)0x8201UL)); \\
  harness_check_u8(0xa5, edge_neg((unsigned char *)0x28000UL)); \\
  harness_check_u8(90, dyn_stack(9)); \\
  harness_check_u8(90, dyn_stack(259)); \\
  harness_check_u16(12, vla_loop(37)); \\
  harness_check_u16(12, vla_loop(257)); \\
  harness_check_u16(0, recurse(0)); \\
  harness_check_u16(55, recurse(10)); \\
  harness_check_u16(210, recurse(20)); \\
  harness_check_u8(112, nested_dyn(31)); \\
} while (0)
''' + '#include "' + str(firmware / 'harness-template.c') + '"\n')
    # Emit a checkpoint letter before each assertion for failure localization.
    text = source.read_text()
    import re
    counter = iter(range(65, 91))
    text = re.sub(r'  harness_check_', lambda _: f"  UART_PUTC('{chr(next(counter))}'); harness_check_", text)
    source.write_text(text)
    harness = compile_c(args, source)
    for opt in [0, 2]:
        for kind in ['asm', 'obj']:
            name = f'regression-O{opt}-{kind}'
            output = args.work_dir / (name + ('.asm' if kind == 'asm' else '.rel'))
            run([args.llc, '-mtriple=mcs251', '-verify-machineinstrs', f'-O{opt}',
                 f'-filetype={kind}', test_dir / 'pointer-regressions.ll', '-o', output])
            obj = assemble(args, output) if kind == 'asm' else output
            qemu(args, link(args, name, [args.work_dir / 'crt0.rel', harness, obj,
                                        args.work_dir / 'pointer-provider.rel']))


def legacy(args):
    test_dir = pathlib.Path(__file__).resolve().parent.parent
    root = test_dir.parents[3]
    firmware = root / 'validation/mcs251-firmware'
    module = args.work_dir / 'i32-legacy.ll'
    module.write_text((test_dir / 'i32.ll').read_text().replace('@', '@_'))
    decls = ['extern unsigned long c(void);', 'extern unsigned long call_i32(void);']
    names = ['id', 'add1', 'neg1', 'sub1', 'and_self', 'and_const', 'or_const', 'xor_const']
    decls += [f'extern unsigned long {n}(unsigned long);' for n in names]
    checks = ['harness_check_u32(0x12345678UL, c());',
              'harness_check_u32(0x12345679UL, call_i32());']
    for x in [0, 0xffff, 0x10000, 0x89abcdef, 0xffffffff]:
        values = [x, (x+1)&0xffffffff, (x-1)&0xffffffff, (x-1)&0xffffffff,
                  x, x&0xffff, x|0xffff, x^0xffff]
        checks += [f'harness_check_u32(0x{v:08x}UL, {n}(0x{x:08x}UL));'
                   for n, v in zip(names, values)]
    source = args.work_dir / 'i32-legacy-harness.c'
    source.write_text('\n'.join(decls) + '\n#define MCS251_CHECKPOINTS() do { ' +
                      ' '.join(checks) + ' } while (0)\n#include "' +
                      str(firmware / 'harness-template.c') + '"\n')
    harness = compile_c(args, source)
    smoke_c = args.work_dir / 'smoke-harness.c'
    smoke_c.write_text((root / 'validation/mcs251-smoke/harness.c').read_text())
    smoke_obj = compile_c(args, smoke_c)
    for name, ir, obj in [('legacy', module, harness),
                          ('smoke', root / 'validation/mcs251-smoke/probe.ll', smoke_obj)]:
        for opt in [0, 2]:
            for kind in ['asm', 'obj']:
                stem = f'{name}-O{opt}-{kind}'
                output = args.work_dir / (stem + ('.asm' if kind == 'asm' else '.rel'))
                run([args.llc, '-mtriple=mcs251', '-verify-machineinstrs', f'-O{opt}',
                     f'-filetype={kind}', ir, '-o', output])
                code = assemble(args, output) if kind == 'asm' else output
                qemu(args, link(args, stem, [args.work_dir / 'crt0.rel', obj, code]))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work-dir', type=pathlib.Path, required=True)
    p.add_argument('--tools', type=pathlib.Path, default=pathlib.Path('/home/liu/build-sdcc/bin'))
    p.add_argument('--qemu', type=pathlib.Path, default=pathlib.Path('/home/liu/build-qemu/qemu-system-mcs251'))
    p.add_argument('--llc', type=pathlib.Path, default=pathlib.Path('/home/liu/build-mcs251/bin/llc'))
    args = p.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    isa(args)
    comparisons(args)
    pointers(args)
    regressions(args)
    legacy(args)


if __name__ == '__main__':
    main()
