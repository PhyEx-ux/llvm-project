; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/i64-div-dynhi.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=I64
; RUN: not --crash llc -mtriple=mcs251 -O2 %t/i64-div-dynhi.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=I64
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %t/i64-folded-control.ll -o - | FileCheck %s --check-prefix=FOLD
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %t/i64-folded-control.ll -o - | FileCheck %s --check-prefix=FOLD
; I64: LLVM ERROR: unsupported library call operation
; FOLD-LABEL: _folded:
; FOLD-NOT: ecall
; FOLD: eret

; i64 remains unsupported (division design v4 §10-Q8; SPEC §1.3): no i64
; register class is registered, so i64 arithmetic expands into a libcall
; request whose impl is not in MCS251SystemLibrary (the default libcall set
; is unavailable on this target), and makeLibCall fails loudly.
;
; SCOPE: this is a representative UDIV i64 negative test. SDIV/SREM/UREM at
; i64 are NOT separately exercised here and no four-operation coverage is
; claimed (the wide-arithmetic path is shared; each op would need its own
; dynamic fixture to be verified individually).
;
; §8.1 validity requirements (all met):
;  1. i64 value is dynamically produced -- the dividend's high half is a
;     runtime value shifted/or'd into place, NOT a bare zext of a narrow
;     value (bare zext has constant-zero high bits and would fold);
;  2. result depends on the high bits (udiv i64 then trunc i32);
;  3. i64 appears ONLY in register arithmetic -- no i64 parameter or memory
;     object, so the oseg-errors.ll static-slot rejection cannot intercept;
;  4. the wide arithmetic survives into the IR that reaches llc (operands
;     are volatile loads, divisor halves too);
;  5. the folded control (same shape, no high-bit dependency) compiles:
;     rejection comes from the live wide-arithmetic path, not from textual
;     i64 presence.
;
; ACCEPTANCE (division design v4 §8.1, "文案随实现锁定后写进 CHECK"): the
; CHECK line above is the expected diagnostic from the shared wide-arithmetic
; rejection path. If the first run confirms the rejection comes from that
; path but the exact text differs, the CHECK line may be adjusted. If the
; failure class differs (parameter restriction / memory restriction /
; selection failure / silent crash / unexpected pass), the path MUST be
; investigated and the fixture fixed as needed -- blind acceptance of any
; error is not authorized.

;--- i64-div-dynhi.ll
@g_lo = external global i32
@g_hi = external global i32
@d_lo = external global i32
@d_hi = external global i32

define i32 @f() {
  %lo  = load volatile i32, ptr @g_lo, align 1
  %hi  = load volatile i32, ptr @g_hi, align 1
  %lo64 = zext i32 %lo to i64
  %hi64 = zext i32 %hi to i64
  %hi_shift = shl i64 %hi64, 32
  %a = or i64 %lo64, %hi_shift
  %dlo = load volatile i32, ptr @d_lo, align 1
  %dhi = load volatile i32, ptr @d_hi, align 1
  %dlo64 = zext i32 %dlo to i64
  %dhi64 = zext i32 %dhi to i64
  %d_shift = shl i64 %dhi64, 32
  %b = or i64 %dlo64, %d_shift
  %q = udiv i64 %a, %b
  %r = trunc i64 %q to i32
  ret i32 %r
}

;--- i64-folded-control.ll
@g_lo = external global i32

define i32 @folded() {
  %lo = load volatile i32, ptr @g_lo, align 1
  %lo64 = zext i32 %lo to i64
  %r = trunc i64 %lo64 to i32
  ret i32 %r
}
