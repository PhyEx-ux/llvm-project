; ub_probe.ll — UB 输入 IR 探针（除法设计 v4 §8.1/§8.3）。
;
; 独立探针函数：非法输入经函数参数（运行期值）抵达 IR 层除法指令。
; 不与语义行同函数混排（§8.3-2），观察形态允许返回/终止/超时。
;
; 探针只承载 IR 层 udiv/sdiv（除零、MIN÷-1 四种形态中 udiv/sdiv 两类；
; urem/srem 的 UB 面与 sdiv/udiv 同判据，本探针集覆盖四种操作中的
; 除零两类与 MIN-1 两类，供 ub_observe.c 调用）。

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251-unknown-none"

define i16 @probe_udiv16_ub(i16 %a, i16 %b) {
entry:
  ; 除零：UB。无论优化器是否折叠，运行期参数供给。
  %q = udiv i16 %a, %b
  ret i16 %q
}

define i16 @probe_sdiv16_ub(i16 %a, i16 %b) {
entry:
  ; INT16_MIN ÷ (-1)：UB。
  %q = sdiv i16 %a, %b
  ret i16 %q
}

define i32 @probe_udiv32_ub(i32 %a, i32 %b) {
entry:
  %q = udiv i32 %a, %b
  ret i32 %q
}

define i32 @probe_sdiv32_ub(i32 %a, i32 %b) {
entry:
  %q = sdiv i32 %a, %b
  ret i32 %q
}
