; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -filetype=null < %s 2>&1 | FileCheck %s
;
; AS6 has a 16-bit representation but denotes the 0x80..0xfe SFR direct-byte
; range. It must not alias low region-00 RAM merely because its width matches.
;
; CHECK: LLVM ERROR: MCS251: SFR byte address must be in 0x80..0xfe

define i8 @reject_low_ram_as_sfr() {
  %v = load volatile i8, ptr addrspace(6) inttoptr (i16 127 to ptr addrspace(6)), align 1
  ret i8 %v
}
