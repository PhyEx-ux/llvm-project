#!/usr/bin/env python3
"""SDCC/LLVM endian QEMU matrix (optional, not a lit dependency).

WSL: python3 /mnt/c/Prj/LLVM/MCS251/validation/mcs251-endian/run.py
All intermediate inputs, tools' output, images and raw transcripts persist in
/home/liu/mcs251-endian. --baseline-llc runs a required-to-fail O2 negative
control first. The ordinary matrix must PASS at O0/O2 via both asm and obj.
"""
import argparse
import hashlib
import json
import pathlib
import shlex
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / 'llvm/test/CodeGen/MCS251'
FW = ROOT / 'validation/mcs251-firmware'
SIG = ('stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small '
       'stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 '
       'all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 '
       'compiler-build=mcs251-abi1.0-r1')
# SDCC spells flat far accesses __xdata even for region-00 edata addresses.
# Non-palindromic values, odd addresses and sentinels detect swaps/overwrites.
OBJECTS = [('e16', 16, 0x000801, 0x1357),
           ('e32', 32, 0x000805, 0x89abcdef),
           ('x16', 16, 0x010401, 0x1357),
           ('x32', 32, 0x010405, 0x89abcdef)]
PREFIX = bytes.fromhex('13 57 89 ab cd ef a1 b2 c3 01 23 45 67 89 ab cd ef 24 68 ac e0')


def run(cmd, log, **kwargs):
    cmd = list(map(str, cmd))
    print('+', shlex.join(cmd), flush=True)
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       **kwargs)
    pathlib.Path(log).write_bytes(p.stdout)
    if p.returncode:
        print(p.stdout.decode(errors='replace'), flush=True)
        raise RuntimeError(f'{cmd[0]} exit {p.returncode}; see {log}')
    return p.stdout


def assemble(args, src):
    obj = src.with_suffix('.rel')
    run([args.tools / 'sdas251', '-plosgffw', '-o', obj, src],
        src.with_suffix('.as.log'))
    return obj


def compile_c(args, src, initialized=False):
    pre = run(['cpp', '-P', '-undef', '-nostdinc', src], src.with_suffix('.i'))
    asm = src.with_suffix('.asm')
    cmd = [args.tools / 'sdcc', '-mmcs251', '--c1mode']
    if initialized:
        # Force actual SDCC GSINIT scalar stores, not an XINIT copy hook.
        cmd += ['--no-xinit-opt']
    run(cmd + ['-o', asm], src.with_suffix('.sdcc.log'), input=pre)
    return assemble(args, asm)


def emit_text(text):
    return ' '.join(f'UART_PUTC({ord(c)});' for c in text)


def harness(args, negative=False):
    decls, checks = [], []

    def check(bits, expected, expr, tag):
        suffix = 'UL' if bits == 32 else 'U'
        checks.append('do { u%d got = (%s); %s harness_hex%d(got); '
                      'UART_PUTC(10); harness_check_u%d(0x%x%s, got); } while (0);'
                      % (bits, expr, emit_text(tag + '='), bits, bits,
                         expected, suffix))

    for name, bits, addr, value in OBJECTS:
        ty = 'unsigned int' if bits == 16 else 'unsigned long'
        decls += [f'extern __xdata volatile {ty} {name};',
                  f'extern {ty} read_{name}(void);',
                  f'extern void write_{name}({ty});']
        check(bits, value, name, f'SDCC.init.{name}')
        check(bits, value, f'read_{name}()', f'LLVM.read.{name}')
        for i, byte in enumerate(value.to_bytes(bits // 8, 'big')):
            alias = f'{name}_byte{i}'
            decls += [f'__xdata __at (0x{addr+i:06x}) volatile unsigned char {alias};']
            check(8, byte, alias, f'init.bytes.{name}.{i}')
        for offset, val, tag in [(-1, 0xa5, 'before'), (bits // 8, 0x5a, 'after')]:
            alias = f'{name}_{tag}'
            decls += [f'extern __xdata volatile unsigned char {alias};']
            check(8, val, alias, f'guard.init.{name}.{tag}')
    # Negative control must reach low16 after all ordinary loads have passed.
    for fn, bits, expected in [('low16', 8, 0x57), ('low32', 8, 0xef),
                               ('lowword32', 16, 0xcdef),
                               ('forward16', 8, 0x13), ('forward32', 8, 0x89)]:
        decls += [f'extern unsigned {"char" if bits == 8 else "int"} {fn}(void);']
        check(bits, expected, fn + '()', 'LLVM.' + fn)
    for bits in [16, 32]:
        ty = 'unsigned int' if bits == 16 else 'unsigned long'
        decls += [f'extern {ty} stack{bits}({ty});']
        values = [0, 1, 0x1234, 0x8001, 0xffff] if bits == 16 else [
            0, 1, 0x12345678, 0x80010203, 0xffffffff]
        for value in values:
            check(bits, value, f'stack{bits}(0x{value:x}UL)', f'LLVM.stack{bits}')
    for name, bits, addr, value in OBJECTS:
        values = [0x2468, 0x8001, 0xffff, 0] if bits == 16 else [
            0x10293847, 0x80010203, 0xffffffff, 0]
        for value in values:
            checks += [f'write_{name}(0x{value:x}UL);']
            check(bits, value, name, f'SDCC.read.{name}')
            for i, byte in enumerate(value.to_bytes(bits // 8, 'big')):
                check(8, byte, f'{name}_byte{i}', f'write.bytes.{name}.{i}')
            check(8, 0xa5, name + '_before', f'guard.write.{name}.before')
            check(8, 0x5a, name + '_after', f'guard.write.{name}.after')
    decls += ['extern void trunc_store16(unsigned long);',
              'extern unsigned long zext_load16(void);']
    checks += ['trunc_store16(0x89abcdefUL);']
    check(16, 0xcdef, 'x16', 'SDCC.trunc_store16')
    check(32, 0xcdef, 'zext_load16()', 'LLVM.zext_load16')
    if not negative:
        decls += ['extern unsigned char prefix_byte(unsigned char);',
                  'extern unsigned char asm_byte(unsigned char);']
        for fn, data in [('prefix_byte', PREFIX),
                         ('asm_byte', bytes.fromhex('13 57 a1 b2 c3'))]:
            for i, byte in enumerate(data):
                check(8, byte, f'{fn}({i})', f'{fn}.{i}')
        for fn, bits, expected in [('fold_bytes16', 16, 0x1234),
                                   ('fold_bytes32', 32, 0x12345678),
                                   ('fold_scalar_byte', 8, 0x89),
                                   ('fold_scalar_word', 16, 0xcdef),
                                   ('fold_record', 16, 0x5789)]:
            ty = {8: 'char', 16: 'int', 32: 'long'}[bits]
            decls += [f'extern unsigned {ty} {fn}(void);']
            check(bits, expected, fn + '()', 'LLVM.' + fn)
    src = args.work_dir / ('negative-harness.c' if negative else 'harness.c')
    src.write_text('\n'.join(decls) + '\n#define MCS251_CHECKPOINTS() do { \\\n' +
                   ' \\\n'.join(checks) + ' \\\n} while (0)\n#include "' +
                   str(FW / 'harness-template.c') + '"\n')
    return compile_c(args, src)


def hex_memory(path):
    result, base = {}, 0
    for line in path.read_text().splitlines():
        raw = bytes.fromhex(line[1:])
        assert sum(raw) % 256 == 0, 'bad HEX checksum'
        size, addr, kind = raw[0], int.from_bytes(raw[1:3], 'big'), raw[3]
        if kind == 4:
            base = int.from_bytes(raw[4:6], 'big') << 16
        elif kind == 0:
            for i, value in enumerate(raw[4:4+size]):
                address = base + addr + i
                assert address not in result or result[address] == value
                result[address] = value
        elif kind != 1:
            raise RuntimeError(f'unhandled HEX record {kind}')
    return result


def link_qemu(args, name, objects, negative=False):
    base = args.work_dir / name
    lk = base.with_suffix('.lk')
    lk.write_text('\n'.join(['-i', '-m', '-A ' + SIG, '-b HOME=0xff0000',
                             '-b GSINIT0=0xfc2800', '-b XSEG=0x10000', str(base)] +
                            list(map(str, objects)) + ['-e', '']))
    run([args.tools / 'sdld', '--mcs251-abi', '-r', '-nf', lk],
        base.with_suffix('.sdld.log'))
    image = base.with_suffix('.hex')
    shutil.copyfile(base.with_suffix('.ihx'), image)
    cmd = ['timeout', '3', args.qemu, '-M', 'stc32g144k246', '-bios', image,
           '-accel', 'tcg', '-icount', 'shift=0,align=off,sleep=off',
           '-display', 'none', '-monitor', 'none', '-serial', 'stdio']
    print('+', shlex.join(list(map(str, cmd))), flush=True)
    p = subprocess.run(list(map(str, cmd)), stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
    base.with_suffix('.serial.raw').write_bytes(p.stdout)
    base.with_suffix('.qemu.stderr').write_bytes(p.stderr)
    text = p.stdout.decode(errors='replace').replace('\r\n', '\n')
    transcript = (shlex.join(list(map(str, cmd))) + '\n' + text +
                  p.stderr.decode(errors='replace') + f'\ntimeout exit={p.returncode}\n')
    base.with_suffix('.qemu.log').write_text(transcript)
    print(transcript, flush=True)
    assert p.returncode == 124, f'{name}: unexpected timeout exit'
    if negative:
        assert 'LLVM.low16=13\nFAIL expected=0x57 got=0x13\n' in text, name
        assert 'PASS' not in text, name
    else:
        assert text.endswith('PASS\n') and 'FAIL' not in text, name
    return image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work-dir', type=pathlib.Path, default=pathlib.Path('/home/liu/mcs251-endian/matrix'))
    p.add_argument('--tools', type=pathlib.Path, default=pathlib.Path('/home/liu/build-sdcc/bin'))
    p.add_argument('--qemu', type=pathlib.Path, default=pathlib.Path('/home/liu/build-qemu/qemu-system-mcs251'))
    p.add_argument('--llc', type=pathlib.Path, default=pathlib.Path('/home/liu/build-mcs251/bin/llc'))
    p.add_argument('--opt', type=pathlib.Path, default=pathlib.Path('/home/liu/build-mcs251/bin/opt'))
    p.add_argument('--baseline-llc', type=pathlib.Path)
    args = p.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    # The firmware template intentionally skips initializers. This local copy
    # enters SDCC's GSINIT instead; init-end.asm explicitly continues to main.
    crt = args.work_dir / 'crt0.asm'
    crt.write_text((FW / 'crt0.asm').read_text().replace(
        'ejmp    __sdcc_program_startup', 'ejmp    endian_gsinit') +
        '\n.area GSINIT (CODE)\nendian_gsinit:\n')
    crt_obj = assemble(args, crt)
    src = args.work_dir / 'provider.c'
    data = []
    for name, bits, addr, value in OBJECTS:
        ty = 'unsigned int' if bits == 16 else 'unsigned long'
        data += [f'__xdata __at (0x{addr:06x}) volatile {ty} {name} = 0x{value:x}UL;',
                 f'__xdata __at (0x{addr-1:06x}) volatile unsigned char {name}_before = 0xa5;',
                 f'__xdata __at (0x{addr+bits//8:06x}) volatile unsigned char {name}_after = 0x5a;']
    src.write_text('\n'.join(data) + '\n')
    provider = compile_c(args, src, initialized=True)
    # CSEG is introduced before HOME in the shared crt0, so SDLD need not
    # place GSINIT and GSFINAL adjacently. End the initializer area explicitly
    # instead of relying on fall-through to the harness's GSFINAL.
    init_end = args.work_dir / 'init-end.asm'
    init_end.write_text('.module endian_init_end\n.source\n.optsdcc ' + SIG +
                        '\n.globl __sdcc_program_startup\n.area GSINIT (CODE)\n'
                        'ejmp __sdcc_program_startup\n')
    init_end_obj = assemble(args, init_end)
    asm = args.work_dir / 'directives.asm'
    asm.write_text('.module endian_directives\n.source\n.optsdcc ' + SIG +
                   '\n.area CSEG (CODE)\n.word 0x1357\n.3byte 0xa1b2c3\n'
                   '_asm_anchor::\neret\n')
    directives = assemble(args, asm)
    cases = []
    if args.baseline_llc:
        negative = harness(args, negative=True)
        for kind in ['asm', 'obj']:
            cases.append(('negative-' + kind, args.baseline_llc, 2, kind, negative, True))
    positive = harness(args)
    for opt in [0, 2]:
        for kind in ['asm', 'obj']:
            cases.append((f'endian-O{opt}-{kind}', args.llc, opt, kind, positive, False))
    folded = args.work_dir / 'folded.ll'
    run([args.opt, '-mtriple=mcs251', '-passes=function(instcombine),globaldce',
         '-S', TESTS / 'endian-fold.ll', '-o', folded], args.work_dir / 'fold.log')
    images = {}
    for name, llc, opt, kind, harness_obj, negative in cases:
        # Provider's GSINIT must run before harness's GSFINAL jump.
        objects = [crt_obj, provider, harness_obj, init_end_obj]
        modules = [TESTS / 'endian-memory.ll']
        if not negative:
            modules += [TESTS / 'endian-prefix.ll', folded]
        for module in modules:
            out = args.work_dir / (name + '-' + module.stem + ('.asm' if kind == 'asm' else '.rel'))
            run([llc, '-mtriple=mcs251', '-verify-machineinstrs', f'-O{opt}',
                 f'-filetype={kind}', module, '-o', out], out.with_suffix('.llc.log'))
            obj = assemble(args, out) if kind == 'asm' else out
            if kind == 'obj':
                obj.with_suffix('.lst').touch()
            objects.append(obj)
        if not negative:
            objects.append(directives)
        images[name] = link_qemu(args, name, objects, negative)
    for opt in [0, 2]:
        assert hex_memory(images[f'endian-O{opt}-asm']) == hex_memory(images[f'endian-O{opt}-obj'])
        print(f'O{opt}: asm/obj linked address-to-byte maps identical', flush=True)
    inputs = [pathlib.Path(__file__), args.llc, args.opt, args.qemu,
              args.tools / 'sdcc', args.tools / 'sdas251', args.tools / 'sdld']
    inputs += list(TESTS.glob('endian-*.ll'))
    if args.baseline_llc:
        inputs.append(args.baseline_llc)
    (args.work_dir / 'sha256.json').write_text(json.dumps({
        str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}, indent=2) + '\n')
    print('ENDIAN MATRIX PASS: 4 positive images; optional negative controls failed as required.', flush=True)


if __name__ == '__main__':
    main()
