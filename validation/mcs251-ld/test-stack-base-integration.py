#!/usr/bin/env python3
"""Assemble/link/QEMU checks for the self-start stack symbol (run in WSL)."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SIG = ("stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small "
       "stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 "
       "all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("/home/liu/mcs251-realhw-alice/stack-integration"))
    args = parser.parse_args()
    work = args.out.resolve(); work.mkdir(parents=True, exist_ok=True)
    tools = Path("/home/liu/build-sdcc/bin")
    def assemble(name, text):
        source = work / (name + ".asm")
        source.write_text(".module " + name + "\n.source\n.optsdcc " + SIG + "\n" + text)
        obj = source.with_suffix(".rel")
        subprocess.run([str(tools / "sdas251"), "-plosgffw", "-o", str(obj), str(source)], check=True)
        return obj
    crt = work / "crt.rel"
    subprocess.run([str(tools / "sdas251"), "-plosgffw", "-o", str(crt), str(ROOT / "validation/mcs251-firmware/crt-selfstart.asm")], check=True)
    app = assemble("app", ".globl _main\n.area CSEG (CODE)\n_main:\nmov r0,0x85\ncmp r0,#2\njne bad\nmov r0,0x81\ncmp r0,#0x92\njne bad\nmov 0x99,#'K'\nhalt: sjmp halt\nbad: mov 0x99,#'F'\nsjmp halt\n")
    data = assemble("data", ".area STATIC (DATA)\n.ds 0x71\n")
    template = (ROOT / "validation/mcs251-firmware/link-selfstart.lk").read_text()
    def command(name, objects, extra=""):
        base = work / name
        text = template.replace("@OUTPUT_STEM@", str(base)).replace("@CRT_REL@", str(objects[0])).replace("@MODULE_REL@", "\n".join(map(str, objects[1:])))
        text = text.replace("-e\n", extra + "-e\n")
        lk = base.with_suffix(".lk"); lk.write_text(text)
        result = subprocess.run(["python3", str(HERE / "mcs251_ld.py"), "--mcs251-abi", "-f", str(lk)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        base.with_suffix(".log").write_bytes(result.stdout)
        return base, result
    # STATIC ends at 0271; align->0280, guard->0290, SPX=028f.
    # _main enters through ECALL, so its observed SPX must be 0292.
    good, result = command("good", [crt, app, data], "-b STATIC=0x200\n")
    assert result.returncode == 0, result.stdout
    qemu = "/home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251"
    process = subprocess.Popen([qemu, "-M", "stc32g144k246", "-bios", str(good.with_suffix(".hex")), "-accel", "tcg", "-display", "none", "-monitor", "none", "-serial", "stdio"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        serial, err = process.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        process.terminate(); serial, err = process.communicate(timeout=3)
    good.with_suffix(".serial").write_bytes(serial)
    assert serial == b"K", serial
    large = assemble("large", ".area STATIC (DATA)\n.ds 0xaf1\n")
    bad, result = command("capacity", [crt, app, large], "-b STATIC=0x100\n")
    assert result.returncode != 0
    assert b"stack capacity: data end 0x0BF1 leaves fewer than 1024 bytes" in result.stdout
    assert not bad.with_suffix(".hex").exists()
    # No opt-in reference: the identical overlarge data layout remains legal,
    # and the synthetic symbol is not introduced (old sdld-compatible mode).
    legacy = assemble("legacy", ".area HOME (CODE)\nsjmp .\n.area VECS (CODE)\n.area BOOT (CODE)\n.area CSEG (CODE)\n.area XINIT (CODE)\n")
    old, result = command("legacy", [legacy, large], "-b STATIC=0x100\n")
    assert result.returncode == 0, result.stdout
    spec = importlib.util.spec_from_file_location("mld", HERE / "mcs251_ld.py")
    mld = importlib.util.module_from_spec(spec); spec.loader.exec_module(mld)
    linker = mld.Linker(strict_abi=True); linker.parse_command_file(str(old.with_suffix(".lk"))); linker.read_all_rels(); linker.setarea(); linker.lnkarea2()
    assert "__mcs251_stack_base" not in linker.symtab
    print("STACK-INTEGRATION-PASS: crt SPX=028f, main SPX=0292 serial=K; capacity rejected; legacy unchanged")


if __name__ == "__main__":
    main()
