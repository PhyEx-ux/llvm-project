#!/usr/bin/env python3

"""Run one SDCC reference image and one LLVM replacement image on QEMU.

The LLVM replacement path is zero-text-processing (Phase 12, Step 1): llc
emits a complete ASxxxx (sdas251) dialect module that is assembled and linked
as-is.  SDCC is only used to build the reference image, the harness/crt0
sides of the replacement image, and to drive the board linker.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = Path(__file__).resolve().parent
DEFAULT_TOOLCHAIN = ROOT / "sdcc-upstream" / "build-smoke"
DEFAULT_QEMU = (
    ROOT
    / "qemu-processmission"
    / "build-mcs251-sys"
    / "qemu-system-mcs251"
)


def run(command: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True, env=env)


def compile_c1(
    cpp: Path,
    sdcc: Path,
    sdas251: Path,
    source: Path,
    asm: Path,
    obj: Path,
    env: dict[str, str],
) -> None:
    """Preprocess with the host cpp, compile with SDCC c1 mode, then assemble."""
    cpp_command = [str(cpp), "-P", "-undef", "-nostdinc", str(source)]
    print(
        "+",
        " ".join(cpp_command),
        "|",
        str(sdcc),
        "-mmcs251 --c1mode ...",
        flush=True,
    )
    preprocessed = subprocess.check_output(cpp_command, env=env)
    subprocess.run(
        [str(sdcc), "-mmcs251", "--c1mode", "-o", str(asm)],
        input=preprocessed,
        check=True,
        env=env,
    )
    run(
        [str(sdas251), "-plosgffw", "-o", str(obj), str(asm)],
        env=env,
    )


def run_qemu(qemu: Path, image: Path, label: str, timeout: float) -> bytes:
    command = [
        str(qemu),
        "-M",
        "stc32g144k246",
        "-bios",
        str(image),
        "-accel",
        "tcg",
        "-icount",
        "shift=0,align=off,sleep=off",
        "-display",
        "none",
        "-monitor",
        "none",
        "-serial",
        "stdio",
    ]
    print("+", " ".join(command), flush=True)
    process = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    assert process.stdout is not None

    output = bytearray()
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout

    try:
        while time.monotonic() < deadline:
            events = selector.select(max(0.0, deadline - time.monotonic()))
            if not events:
                break
            chunk = os.read(process.stdout.fileno(), 4096)
            if not chunk:
                break
            output.extend(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            normalized = bytes(output).replace(b"\r\n", b"\n")
            if b"PASS\n" in normalized or b"FAIL\n" in normalized:
                break
    finally:
        selector.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    normalized = bytes(output).replace(b"\r\n", b"\n")
    if b"FAIL\n" in normalized:
        raise RuntimeError(f"{label}: firmware emitted FAIL")
    if b"PASS\n" not in normalized:
        raise RuntimeError(f"{label}: QEMU did not emit PASS before timeout")
    print(f"{label}: PASS", flush=True)
    return bytes(output)


def extract_return_instruction(asm: Path, *, require_symbol: bool) -> str:
    text = asm.read_text()
    if require_symbol and not re.search(
        r"^_mcs251_probe:\s*(?:;.*)?$", text, re.MULTILINE
    ):
        raise RuntimeError(
            f"LLVM assembly does not define the SDCC ABI symbol "
            f"_mcs251_probe in {asm}"
        )
    return_re = re.compile(
        r"^\s*(eret|ret)\s*(?:;.*)?$", re.IGNORECASE
    )
    matches = [
        return_re.match(line).group(1).lower()
        for line in text.splitlines()
        if return_re.match(line)
    ]
    if len(matches) != 1:
        raise RuntimeError(
            f"expected exactly one operand-free return in {asm}, got {matches}"
        )
    return matches[0]


def extract_optsdcc(sdcc_asm: Path) -> str:
    matches = [
        line.strip()
        for line in sdcc_asm.read_text().splitlines()
        if line.lstrip().startswith(".optsdcc ")
    ]
    if len(matches) != 1:
        raise RuntimeError(
            f"expected exactly one .optsdcc ABI signature in {sdcc_asm}, "
            f"got {matches}"
        )
    return matches[0]


# Directives sdas251 rejects; llc's ASxxxx output must not contain any of
# them (defense-in-depth for the Step 1 zero-text-processing guarantee).
BANNED_DIRECTIVE_RE = re.compile(
    r"^[ \t]*\.(?:text|section|p2align|align|type|size|ident|space|"
    r"long|short|quad|zero|fill|file|weak|end)(?:[ \t]|$)",
    re.MULTILINE,
)

# The locked ABI signature llc embeds in every module (Phase 12 specimen).
LLVM_OPTSDCC = (
    ".optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small "
    "stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 "
    "all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 "
    "compiler-build=mcs251-abi1.0-r1"
)


def check_asxxxx_module(text: str, asm: Path) -> None:
    """Check that an llc output is a complete, self-contained sdas251 module."""
    banned = BANNED_DIRECTIVE_RE.findall(text)
    if banned:
        raise RuntimeError(
            f"{asm}: contains directives sdas251 rejects: {sorted(banned)}"
        )
    required = [
        r"(?m)^[ \t]*\.module[ \t]+\S",
        r"(?m)^[ \t]*\.source[ \t]*$",
        f"(?m)^{re.escape(LLVM_OPTSDCC)}$",
        r"(?m)^[ \t]*\.area CSEG \(CODE\)[ \t]*$",
    ]
    for pattern in required:
        if re.search(pattern, text) is None:
            raise RuntimeError(
                f"{asm}: missing required ASxxxx module element "
                f"matching {pattern}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--toolchain", type=Path, default=DEFAULT_TOOLCHAIN)
    parser.add_argument("--sdcc", type=Path)
    parser.add_argument("--sdas251", type=Path)
    parser.add_argument("--cpp", type=Path)
    parser.add_argument("--qemu", type=Path, default=DEFAULT_QEMU)
    parser.add_argument("--llc", type=Path)
    parser.add_argument(
        "--sdcc-only",
        action="store_true",
        help="stop after the SDCC reference firmware passes in QEMU",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=SOURCE_DIR / "build",
    )
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    toolchain = args.toolchain.resolve()
    qemu = args.qemu.resolve()
    build_dir = args.build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)

    sdcc = args.sdcc.resolve() if args.sdcc else toolchain / "bin" / "sdcc"
    sdas251 = (
        args.sdas251.resolve()
        if args.sdas251
        else toolchain / "bin" / "sdas251"
    )
    if args.cpp:
        cpp = args.cpp.resolve()
    else:
        cpp_name = shutil.which("cpp")
        if cpp_name is None:
            parser.error("required host preprocessor was not found: cpp")
        cpp = Path(cpp_name).resolve()

    llc = args.llc.resolve() if args.llc else None
    if not args.sdcc_only and llc is None:
        parser.error("--llc is required unless --sdcc-only is used")

    required_paths = [sdcc, sdas251, cpp, qemu]
    if llc is not None:
        required_paths.append(llc)
    for required in required_paths:
        if not required.exists():
            parser.error(f"required path does not exist: {required}")

    env = os.environ.copy()
    env["PATH"] = f"{sdcc.parent}{os.pathsep}{env.get('PATH', '')}"
    board_link_flags = [
        "--code-loc",
        "0xff0000",
        "-Wl-b GSINIT0=0xfc2800",
    ]

    crt_rel = build_dir / "crt0.rel"
    crt_asm = build_dir / "crt0-generated.asm"
    harness_rel = build_dir / "harness.rel"
    harness_asm = build_dir / "harness.asm"
    sdcc_probe_asm = build_dir / "probe-sdcc.asm"
    sdcc_probe_rel = build_dir / "probe-sdcc.rel"
    sdcc_image = build_dir / "smoke-sdcc.hex"

    compile_c1(
        cpp,
        sdcc,
        sdas251,
        SOURCE_DIR / "harness.c",
        harness_asm,
        harness_rel,
        env,
    )
    compile_c1(
        cpp,
        sdcc,
        sdas251,
        SOURCE_DIR / "probe.c",
        sdcc_probe_asm,
        sdcc_probe_rel,
        env,
    )
    abi_signature = extract_optsdcc(sdcc_probe_asm)
    sdcc_return = extract_return_instruction(
        sdcc_probe_asm, require_symbol=False
    )
    crt_template = (SOURCE_DIR / "crt0.asm").read_text()
    if crt_template.count("@OPTSDCC@") != 1:
        raise RuntimeError("crt0.asm must contain exactly one @OPTSDCC@ token")
    crt_asm.write_text(
        crt_template.replace("@OPTSDCC@", f"        {abi_signature}")
    )
    run([
        str(sdas251),
        "-plosgffw",
        "-o",
        str(crt_rel),
        str(crt_asm),
    ], env=env)
    run([
        str(sdcc),
        "-mmcs251",
        "--nostdlib",
        "--no-xinit-opt",
        *board_link_flags,
        "-o",
        str(sdcc_image),
        str(crt_rel),
        str(harness_rel),
        str(sdcc_probe_rel),
    ], env=env)
    run_qemu(qemu, sdcc_image, "SDCC reference", args.timeout)

    if args.sdcc_only:
        print(f"SDCC assembly: {sdcc_probe_asm}")
        print(f"SDCC image:    {sdcc_image}")
        return 0

    # ---- LLVM replacement path: llc output is assembled verbatim ---------
    # Step 1 (Phase 12): llc emits the complete ASxxxx module; no extraction
    # or wrapping happens between llc and sdas251.
    llvm_asm = build_dir / "probe-llvm.asm"
    assert llc is not None
    run([
        str(llc),
        "-enable-new-pm=0",
        "-mtriple=mcs251-unknown-none",
        "-filetype=asm",
        "-o",
        str(llvm_asm),
        str(SOURCE_DIR / "probe.ll"),
    ])
    check_asxxxx_module(llvm_asm.read_text(), llvm_asm)
    llvm_return = extract_return_instruction(llvm_asm, require_symbol=True)
    if llvm_return != sdcc_return:
        raise RuntimeError(
            "LLVM return instruction does not match the SDCC MCS-251 ABI: "
            f"LLVM emitted {llvm_return}, SDCC emitted {sdcc_return}"
        )

    llvm_probe_rel = build_dir / "probe-llvm.rel"
    llvm_image = build_dir / "smoke-llvm.hex"
    run([
        str(sdas251),
        "-plosgffw",
        "-o",
        str(llvm_probe_rel),
        str(llvm_asm),
    ], env=env)
    run([
        str(sdcc),
        "-mmcs251",
        "--nostdlib",
        "--no-xinit-opt",
        *board_link_flags,
        "-o",
        str(llvm_image),
        str(crt_rel),
        str(harness_rel),
        str(llvm_probe_rel),
    ], env=env)
    run_qemu(qemu, llvm_image, "LLVM replacement", args.timeout)

    print(f"LLVM assembly: {llvm_asm}")
    print(f"SDCC assembly: {sdcc_probe_asm}")
    print(f"SDCC image:    {sdcc_image}")
    print(f"LLVM image:    {llvm_image}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
