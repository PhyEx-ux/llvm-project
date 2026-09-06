#!/usr/bin/env python3
"""构建并审计 G12K128 单文件自检（默认生成真机HEX，--qemu仅供回归）。"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def run(command):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("/home/liu/mcs251-realhw-alice/selftest-real-hw"))
    parser.add_argument("--qemu", action="store_true", help="只构建test-port版，不能烧到真机")
    parser.add_argument("--clang", default="/home/liu/build-clang/bin/clang")
    parser.add_argument("--llc", default="/home/liu/build-mcs251/bin/llc")
    parser.add_argument("--sdas", default="/home/liu/build-sdcc/bin/sdas251")
    args = parser.parse_args()
    work = args.out.resolve(); work.mkdir(parents=True, exist_ok=True)
    source = HERE / "01-selftest.c"
    profile = [] if args.qemu else ["-DSTC32_REAL_HW"]
    run([args.clang, "--target=mcs251-unknown-none", "-std=c11", "-O2", "-Wall", "-Wextra", *profile, "-S", "-emit-llvm", source, "-o", work / "selftest.ll"])
    run([args.llc, "-mtriple=mcs251", "-verify-machineinstrs", "-filetype=obj", work / "selftest.ll", "-o", work / "selftest.rel"])
    run([args.sdas, "-plosgffw", "-o", work / "crt.rel", ROOT / "validation/mcs251-firmware/crt-selfstart.asm"])
    text = (ROOT / "validation/mcs251-firmware/link-selfstart.lk").read_text()
    for key, value in {"OUTPUT_STEM": work / "selftest", "CRT_REL": work / "crt.rel", "MODULE_REL": work / "selftest.rel"}.items():
        text = text.replace("@" + key + "@", str(value))
    script = work / "selftest.lk"; script.write_text(text)
    linker = ROOT / "validation/mcs251-ld/mcs251_ld.py"
    run(["python3", linker, "--mcs251-abi", "-f", script])
    spec = importlib.util.spec_from_file_location("mld", linker)
    mld = importlib.util.module_from_spec(spec); spec.loader.exec_module(mld)
    lnk = mld.Linker(strict_abi=True)
    lnk.parse_command_file(str(script)); lnk.read_all_rels(); lnk.setarea(); lnk.lnkarea2(); lnk.symdef()
    assert not lnk.lkerr
    layout = {"profile": "QEMU TEST PORT -- NOT FOR HARDWARE" if args.qemu else "G12K128 HIRC=24MHz UART1=115200/8N1 EEPROM=0",
              "spx": lnk.symval(lnk.symtab["__mcs251_stack_base"]), "areas": []}
    for area in lnk.areas:
        for ax in area.areaxs:
            if not ax.size:
                continue
            lo, hi = ax.addr, ax.addr + ax.size
            if area.loc_index() == 1:
                assert 0xfe0000 <= lo < hi <= 0x1000000, (area.name, lo, hi)
            elif area.loc_index() == 2:
                assert 0x10000 <= lo < hi <= 0x12000, (area.name, lo, hi)
            elif area.loc_index() == 0:
                assert 0 <= lo < hi <= layout["spx"], (area.name, lo, hi)
            layout["areas"].append({"name": area.name, "begin": lo, "end": hi})
    (work / "layout.json").write_text(json.dumps(layout, indent=2) + "\n")
    print("HEX:", work / "selftest.hex")
    print(layout["profile"])
    print("尚未真机实测；先按 REALHW-GUIDE 检查供电、ISP时钟和EEPROM配置。")


if __name__ == "__main__":
    main()
