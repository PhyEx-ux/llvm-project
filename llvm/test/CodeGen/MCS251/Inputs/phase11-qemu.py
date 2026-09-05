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
    lines += emit_text('PASS\n') + ['halt: sjmp halt', 'fail:']
    lines += emit_text('FAIL\n') + ['sjmp halt']
    source = args.work_dir / 'isa.asm'
    source.write_text('\n'.join(lines) + '\n')
    qemu(args, link(args, 'isa', [assemble(args, source)]))
    print(f'ISA assertions passed: {checks} (120 CMP32 branches, 4 DR addresses)')


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


if __name__ == '__main__':
    main()
