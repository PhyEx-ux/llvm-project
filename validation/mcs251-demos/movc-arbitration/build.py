#!/usr/bin/env python3
"""构建 G12K128 MOVC 仲裁固件；QEMU版与真机版串口路径互斥。"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SIG = ("stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small "
       "stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 "
       "all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("/home/liu/mcs251-realhw-alice/layout-v2/movc"))
    parser.add_argument("--sdas", default="/home/liu/build-sdcc/bin/sdas251")
    parser.add_argument("--qemu", default="/home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251")
    parser.add_argument("--run-qemu", action="store_true")
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    linker = ROOT / "validation/mcs251-ld/mcs251_ld.py"
    spec = importlib.util.spec_from_file_location("mld", linker)
    mld = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mld)
    for real in [False, True]:
        base = args.out / ("movc-real-hw" if real else "movc-qemu")
        source = base.with_suffix(".asm")
        source.write_text(f".module movc_arbitration\nREAL_HW = {int(real)}\n" + (HERE / "probe.asm").read_text())
        subprocess.run([args.sdas, "-plosgffw", "-o", str(base.with_suffix(".rel")), str(source)], check=True)
        lk = base.with_suffix(".lk")
        lk.write_text("\n".join(["-muwx", "-i " + str(base), "-I 0x100",
            "-b HOME=0xff0000", "-b VECS=0xff0003", "-b BOOT=0xff0100",
            "-b CSEG=0xfe0800", "-b SENT_FE=0xfe8000", "-b SENT_FF=0xff8000",
            "-A " + SIG, str(base.with_suffix(".rel")), "-e", ""]))
        subprocess.run(["python3", str(linker), "--mcs251-abi", "-f", str(lk)], check=True)
        lnk = mld.Linker(strict_abi=True)
        lnk.parse_command_file(str(lk)); lnk.read_all_rels(); lnk.setarea(); lnk.lnkarea2()
        symbols = {name: lnk.symval(lnk.symtab[name]) for name in
                   ["movc_site", "sentinel_fe", "sentinel_ff", "__mcs251_stack_base"]}
        assert 0xfe0800 <= symbols["movc_site"] < 0xff0000
        assert symbols["sentinel_fe"] == 0xfe8000 and symbols["sentinel_ff"] == 0xff8000
        base.with_suffix(".layout.json").write_text(json.dumps(symbols, indent=2) + "\n")
        if args.run_qemu and not real:
            process = subprocess.Popen([args.qemu, "-M", "stc32g144k246", "-bios", str(base.with_suffix(".hex")),
                "-accel", "tcg", "-icount", "shift=0,align=off,sleep=off", "-display", "none", "-monitor", "none", "-serial", "stdio"],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                out, err = process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.terminate(); out, err = process.communicate(timeout=3)
            base.with_suffix(".serial").write_bytes(out)
            base.with_suffix(".stderr").write_bytes(err)
            print(out.decode(), end="")
            if out != b"MOVC=3C FE=3C FF=A7\n":
                raise RuntimeError("冻结QEMU的MOVC行为与基线不符，需重新调查")
    print("真机请烧 movc-real-hw.hex；EEPROM分区需<=0x700字节（推荐0）；尚未执行真机仲裁。")


if __name__ == "__main__":
    main()
