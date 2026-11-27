<#
.SYNOPSIS
  One-click build and verification script for the STC32G12K128 LLVM/Clang
  MCS-251 demo firmware on Windows.
  Windows 平台 STC32G12K128 LLVM/Clang MCS-251 固件示例一键构建与校验脚本。

.USAGE
  .\build.ps1                # Build real-hardware firmware + verify / 构建真机固件并校验
  .\build.ps1 -QemuMode      # Build QEMU test-port variant / 构建 QEMU 测试端口版本
  .\build.ps1 -Lto           # Build with Full LTO (-flto) / 启用 Full LTO (-flto) 构建
  .\build.ps1 -Clean         # Clean build/ directory / 清理 build/ 目录
#>
param(
  [string]$ToolchainBin = "",
  [string]$OutDir       = (Join-Path $PSScriptRoot "build"),
  [switch]$QemuMode,
  [switch]$Lto,
  [switch]$Clean
)

$ErrorActionPreference = "Stop"

if ($Clean) {
  if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
  Write-Host "Cleaned $OutDir"
  exit 0
}

# Auto-detect toolchain bin directory (portable release package or repo build).
# 自动探测工具链 bin 目录（优先适配独立发布包，其次适配源码仓库构建目录）。
if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
  $pkgBin  = Join-Path $PSScriptRoot "..\..\bin"
  $repoBin = Join-Path $PSScriptRoot "..\..\..\build-mcs251\bin"
  if (Test-Path (Join-Path $pkgBin "clang.exe")) {
    $ToolchainBin = (Resolve-Path $pkgBin).Path
  } elseif (Test-Path (Join-Path $repoBin "clang.exe")) {
    $ToolchainBin = (Resolve-Path $repoBin).Path
  } else {
    $ToolchainBin = "C:\Prj\XL\build-mcs251\bin"
  }
}

$Clang     = Join-Path $ToolchainBin "clang.exe"
$Yaml2Obj  = Join-Path $ToolchainBin "yaml2obj.exe"
$ObjCopy   = Join-Path $ToolchainBin "llvm-objcopy.exe"
$ObjDump   = Join-Path $ToolchainBin "llvm-objdump.exe"
$ReadObj   = Join-Path $ToolchainBin "llvm-readobj.exe"
$DwarfDump = Join-Path $ToolchainBin "llvm-dwarfdump.exe"

foreach ($tool in @($Clang, $ObjCopy, $ObjDump, $ReadObj, $DwarfDump)) {
  if (-not (Test-Path $tool)) {
    throw "Missing required toolchain binary: $tool"
  }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 1. Locate prebuilt v2 IRQ CRT (packaged mode) or materialize from YAML (repo mode).
# 1. 定位预构建 v2 中断模式 CRT（发布包模式）或从 YAML 生成（源码仓库模式）。
$PkgCrt = Join-Path $PSScriptRoot "..\..\lib\mcs251\crt\crt-irq-v2.o"
$CrtObj = Join-Path $OutDir "crt-irq-v2.o"
if (Test-Path $PkgCrt) {
  Write-Host "[1/5] Using packaged v2 IRQ CRT -> $CrtObj"
  Copy-Item -Force $PkgCrt $CrtObj
} else {
  if (-not (Test-Path $Yaml2Obj)) {
    throw "Missing required toolchain binary: $Yaml2Obj"
  }
  $CrtYaml = Join-Path $PSScriptRoot "..\mcs251-elf\runtime\crt-irq-v2.yaml"
  Write-Host "[1/5] Generating v2 IRQ CRT -> $CrtObj"
  & $Yaml2Obj $CrtYaml -o $CrtObj
  if ($LASTEXITCODE -ne 0) { throw "yaml2obj failed" }

  # Set ELF32BE e_flags (offset 36) to 0x00000102 (EF_MCS251_ABI_V2 | EF_MCS251_BACKEND_NATIVE)
  # 将 ELF32 大端头部的 e_flags（偏移 36）设为 v2 标志 0x00000102
  $bytes = [System.IO.File]::ReadAllBytes($CrtObj)
  $bytes[36] = 0x00; $bytes[37] = 0x00; $bytes[38] = 0x01; $bytes[39] = 0x02
  [System.IO.File]::WriteAllBytes($CrtObj, $bytes)
}

# 2. Compile C translation units (built-in <stc32.h>, zero -I flags needed!)
# 2. 编译各 C 编译单元（直接使用内置 <stc32.h>，无需任何 -I 参数）
$CFlags = @(
  "--target=mcs251-unknown-none",
  "-std=c11",
  "-Os",
  "-g",
  "-ffunction-sections",
  "-fdata-sections",
  "-Wall",
  "-Wextra",
  "-Werror"
)
if ($QemuMode) { $CFlags += "-DQEMU_MODE" }
if ($Lto)      { $CFlags += "-flto" }

$Sources = @("uart.c", "timer.c", "main.c")
$Objs = @()
Write-Host "[2/5] Compiling $($Sources -join ', ') (built-in <stc32.h>)"
foreach ($src in $Sources) {
  $srcPath = Join-Path $PSScriptRoot "src\$src"
  $objName = if ($Lto) { "$src.bc" } else { "$src.o" }
  $objPath = Join-Path $OutDir $objName
  & $Clang @CFlags -c $srcPath -o $objPath
  if ($LASTEXITCODE -ne 0) { throw "Compilation failed for $src" }
  $Objs += $objPath
}

# 3. Link via clang -T stc32g12k128.ld -> mcs251-lld (GC + JSON map + G11 verify)
# 3. 经由 clang -T 链接脚本调用 mcs251-lld（开启 GC、JSON Map 与 G11 独立校验）
$LdScript  = Join-Path $PSScriptRoot "stc32g12k128.ld"
$ElfOut    = Join-Path $OutDir "stc32g12k128-demo.elf"
$HexOut    = Join-Path $OutDir "stc32g12k128-demo.hex"
$MapOut    = Join-Path $OutDir "stc32g12k128-demo.map"
$MapJson   = Join-Path $OutDir "stc32g12k128-demo.map.json"
$PlaceRep  = Join-Path $OutDir "stc32g12k128-demo.placement"
$FactsOut  = Join-Path $OutDir "stc32g12k128-demo.facts"
$LstOut    = Join-Path $OutDir "stc32g12k128-demo.lst"

$LinkArgs = @(
  "--target=mcs251-unknown-none",
  "-T", $LdScript,
  "-Wl,--gc-sections",
  "-Wl,--keep-symbols",
  "-Wl,--map=$MapOut",
  "-Wl,--map-json=$MapJson",
  "-Wl,--link-facts=$FactsOut"
)
if (-not $Lto) {
  # Zero-trust G11 placement verifier inspects the ELF object files on disk.
  # 零信任 G11 放置校验器直接独立读取磁盘上的 ELF 目标文件进行复核。
  $LinkArgs += "-Wl,--placement-report=$PlaceRep"
  $LinkArgs += "-Wl,--verify-placement"
}
if ($Lto) {
  $LinkArgs += "-flto"
}

Write-Host "[3/5] Linking via mcs251-lld (-T stc32g12k128.ld, --gc-sections$(if (-not $Lto) {', --verify-placement'} else {', -flto'}))"
& $Clang @LinkArgs $CrtObj @Objs -o $ElfOut
if ($LASTEXITCODE -ne 0) { throw "Link failed" }

# 4. Generate burnable Intel HEX and disassembly listing
# 4. 生成可直接烧录的 Intel HEX 与反汇编清单
Write-Host "[4/5] Generating Intel HEX -> $HexOut"
& $ObjCopy -O ihex $ElfOut $HexOut
if ($LASTEXITCODE -ne 0) { throw "llvm-objcopy -O ihex failed" }

& $ObjDump -d $ElfOut | Out-File -Encoding ascii $LstOut
if ($LASTEXITCODE -ne 0) { throw "llvm-objdump failed" }

# 5. Automated verification (ELF header, DWARF5, GC, KEEP, ISR priority init)
# 5. 自动化校验（ELF v2 头标志、DWARF5 完整性、GC 裁剪、KEEP 保留与 ISR 优先级合成）
Write-Host "[5/5] Verifying ELF, DWARF, GC, and ISR priority auto-synthesis"
$hdr = & $ReadObj -h $ElfOut
if ($LASTEXITCODE -ne 0) { throw "llvm-readobj failed" }
if (-not ($hdr -join "`n" -match "EF_MCS251_ABI_V2")) {
  throw "Expected EF_MCS251_ABI_V2 in output ELF"
}

& $DwarfDump --verify $ElfOut | Out-Null
if ($LASTEXITCODE -ne 0) { throw "llvm-dwarfdump --verify failed" }

$mapText = Get-Content -Raw $MapOut
if ($mapText -match "_dead_unused_helper") {
  throw "Expected _dead_unused_helper to be removed by --gc-sections"
}
if ($mapText -notmatch "_fw_info_keep") {
  throw "Expected _fw_info_keep to be preserved by KEEP(...) in stc32g12k128.ld"
}
if ($mapText -notmatch "___mcs251_isr_priority_init") {
  throw "Expected linker-synthesized ___mcs251_isr_priority_init"
}

$hash = (Get-FileHash -Algorithm SHA256 $HexOut).Hash.ToLower()
Write-Host ""
Write-Host "BUILD & VERIFY PASSED!" -ForegroundColor Green
Write-Host "  ELF        : $ElfOut"
Write-Host "  Intel HEX  : $HexOut (SHA-256: $hash)"
Write-Host "  Map / JSON : $MapOut , $MapJson"
Write-Host "  Disasm     : $LstOut"
if (-not $Lto) {
  Write-Host "  Placement  : $PlaceRep (verified by mcs251-placement-verify)"
}
