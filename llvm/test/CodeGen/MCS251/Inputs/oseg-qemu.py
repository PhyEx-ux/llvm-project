#!/usr/bin/env python3
"""Optional OSEG ABI end-to-end checks; no SDCC/QEMU dependency for lit.

Run in WSL with --work-dir /home/liu/mcs251-oseg-alice/v1/matrix.
LLVM assembly is assembled verbatim. Every image uses the strict native linker.
Serial is captured separately from QEMU diagnostics and compared byte for byte.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import subprocess

SIG = ('stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small '
       'stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 '
       'all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 '
       'compiler-build=mcs251-abi1.0-r1')


def run(cmd, **kwargs):
    print('+', ' '.join(map(str, cmd)), flush=True)
    return subprocess.run(list(map(str, cmd)), check=True, **kwargs)


def assemble(args, source):
    obj = source.with_suffix('.rel')
    run([args.tools / 'sdas251', '-plosgffw', '-o', obj, source])
    return obj


def compile_c(args, source):
    asm = source.with_suffix('.asm')
    # c1 mode is the locally supported equivalent of sdcc -S; save real asm.
    run([args.tools / 'sdcc', '-mmcs251', '--c1mode', '-o', asm],
        input=source.read_bytes())
    return assemble(args, asm)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work-dir', type=pathlib.Path, required=True)
    p.add_argument('--tools', type=pathlib.Path, default=pathlib.Path('/home/liu/build-sdcc/bin'))
    p.add_argument('--llc', type=pathlib.Path, default=pathlib.Path('/home/liu/build-mcs251/bin/llc'))
    p.add_argument('--qemu', type=pathlib.Path, default=pathlib.Path('/home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251'))
    args = p.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    tests = pathlib.Path(__file__).resolve().parent.parent
    root = tests.parents[3]
    manifest = {'source': 'post-40d6ce4f1+WIP-OSEG', 'tools': {}}
    for name, path in [('llc', args.llc), ('qemu', args.qemu),
                       ('sdcc', args.tools / 'sdcc'), ('sdas251', args.tools / 'sdas251')]:
        manifest['tools'][name] = {'path': str(path), 'md5': hashlib.md5(path.read_bytes()).hexdigest()}
    (work / 'tools.json').write_text(json.dumps(manifest, indent=2) + '\n')

    crt = work / 'crt.asm'
    crt.write_text((root / 'validation/mcs251-smoke/crt0.asm').read_text().replace('@OPTSDCC@', '.optsdcc ' + SIG))
    crt_obj = assemble(args, crt)
    oracle = work / 'oracle.c'
    oracle.write_text('''unsigned long oracle(unsigned int a, unsigned char b, unsigned long c) {
  return (c ^ (unsigned long)a) + (unsigned long)b;
}
''')
    oracle_obj = compile_c(args, oracle)

    a, b, c = 0x12345678, 0x89abcdef, 0x2468
    mixed = ((b ^ 0x1357) + 0xa5) & 0xffffffff
    wide = ((a ^ b) + c) & 0xffffffff
    nested = ((((a ^ 0x10203040) + 0x1357 + b) & 0xffffffff) ^ a)
    cases = [
        ('bytes', 'call_bytes()', 2, 2),
        ('mixed', 'call_mixed()', mixed, 8),
        ('wide', 'call_wide()', wide, 8),
        ('nested', 'call_nested()', nested, 8),
        ('outer', 'call_outer()', (nested + 0x01020304) & 0xffffffff, 8),
        ('sdcc', 'call_oracle()', mixed, 8),
        ('reverse', 'mixed(0x1357, 0xa5, 0x89abcdefUL)', mixed, 8),
        ('local', 'local_user()', ((0x1234 + 37) ^ 85) + 7, 4),
        ('modules', 'roundtrip()', ((0x1234 ^ 0x5678) + 0x5678) & 0xffff, 4),
        ('single', 'single(0x12345678UL)', a ^ 0x5555aaaa, 8),
        ('zero', 'zero()', 0xbeef, 4),
    ]
    declarations = '''__sfr __at (0x99) SBUF;
extern unsigned char call_bytes(void);
extern unsigned long call_mixed(void), call_wide(void), call_nested(void), call_outer(void), call_oracle(void);
extern __data unsigned char oracle_PARM_2, oracle_PARM_3[4];
extern unsigned long mixed(unsigned int, unsigned char, unsigned long);
extern unsigned int local_user(void), roundtrip(void), zero(void);
extern unsigned long single(unsigned long);
void digit(unsigned char v) { SBUF = v < 10 ? '0' + v : 'A' + v - 10; }
void bytehex(unsigned char v) { digit(v >> 4); digit(v & 15); }
void wordhex(unsigned int v) { bytehex(v >> 8); bytehex(v); }
void longhex(unsigned long v) { wordhex(v >> 16); wordhex(v); }
void main(void) {
'''
    lines = [declarations]
    expected = ''
    for label, expr, value, digits in cases:
        expected += f'{label}={value:0{digits}X}\n'
        lines += [f"SBUF = '{ch}';" for ch in label + '=']
        printer = {2: 'bytehex', 4: 'wordhex', 8: 'longhex'}[digits]
        lines += [f'{printer}({expr});', "SBUF = '\\n';"]
    # Read the oracle's actual static bytes, not a computed scalar result.
    # hex printers use only register arguments, so do not overwrite OSEG.
    lines += ['call_oracle();']
    lines += [f"SBUF = '{ch}';" for ch in 'layout=']
    lines += ['bytehex(oracle_PARM_2);']
    lines += [f'bytehex(oracle_PARM_3[{i}]);' for i in range(4)]
    lines += ["SBUF = '\\n';"]
    expected += 'layout=A589ABCDEF\n'
    expected += 'DONE\n'
    lines += [f"SBUF = '{ch}';" for ch in 'DONE'] + ["SBUF = '\\n';", 'for (;;) {}', '}']
    harness = work / 'harness.c'
    harness.write_text('\n'.join(lines) + '\n')
    harness_obj = compile_c(args, harness)
    (work / 'expected.serial').write_bytes(expected.encode())

    spec = importlib.util.spec_from_file_location('mcs251_ld', root / 'validation/mcs251-ld/mcs251_ld.py')
    linker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(linker)
    results = []
    for opt in [0, 2]:
        for kind in ['asm', 'obj']:
            name = f'O{opt}-{kind}'
            objects = [crt_obj, harness_obj, oracle_obj]
            for module in [tests / 'oseg-multi.ll', tests / 'Inputs/oseg-caller.ll']:
                output = work / (module.stem + '-' + name + ('.asm' if kind == 'asm' else '.rel'))
                run([args.llc, '-mtriple=mcs251', '-verify-machineinstrs', f'-O{opt}',
                     f'-filetype={kind}', module, '-o', output])
                objects.append(assemble(args, output) if kind == 'asm' else output)
            base = work / name
            script = base.with_suffix('.lk')
            script.write_text('\n'.join(['-i', '-m', '-A ' + SIG,
                                         '-b HOME=0xff0000', '-b GSINIT0=0xfc2800',
                                         str(base)] + list(map(str, objects)) + ['-e', '']))
            run(['python3', root / 'validation/mcs251-ld/mcs251_ld.py', '--mcs251-abi', '-f', script])
            # Inspect independently of firmware arithmetic: leaf frames must
            # overlay and the two non-leaf frames must concatenate.
            ld = linker.Linker(strict_abi=True)
            ld.parse_command_file(str(script))
            ld.read_all_rels()
            ld.check_abi_modules()
            ld.setarea()
            ld.lnkarea2()
            ld.symdef()
            assert not ld.lkerr
            names = ['bytes', 'mixed', 'wide', 'nested', 'outer']
            slots = {n: ld.symval(ld.symtab['_' + n + '_PARM_2']) for n in names}
            assert slots['bytes'] == slots['mixed'] == slots['wide']
            assert len({slots['wide'], slots['nested'], slots['outer']}) == 3
            assert slots['outer'] == slots['nested'] + 6
            assert min(slots.values()) >= 8
            base.with_suffix('.slots.json').write_text(json.dumps(slots, indent=2) + '\n')
            serial = base.with_suffix('.serial')
            cmd = [args.qemu, '-M', 'stc32g144k246', '-bios', base.with_suffix('.hex'),
                   '-accel', 'tcg', '-icount', 'shift=0,align=off,sleep=off',
                   '-display', 'none', '-monitor', 'none', '-serial', 'file:' + str(serial)]
            print('+', ' '.join(map(str, cmd)), flush=True)
            with base.with_suffix('.qemu.log').open('wb') as log:
                proc = subprocess.Popen(list(map(str, cmd)), stdin=subprocess.DEVNULL, stdout=log, stderr=log)
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    proc.wait(timeout=3)
            actual = serial.read_bytes()
            print(f'{name} serial={actual!r}', flush=True)
            results.append({'name': name, 'serial': actual.decode('ascii', errors='replace'),
                            'expected': expected, 'match': actual == expected.encode()})
            (work / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    assert all(r['match'] for r in results), 'serial mismatch; see results.json'
    print('All four paths match every serial byte.', flush=True)


if __name__ == '__main__':
    main()
