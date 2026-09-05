; T2 demo-03-shaped display table and bit-scan kernel.
; The table is intentionally represented in the LLVM module as a local
; ten-entry array, because this frozen backend rejects module-level globals.
; Each glyph is an active-high 7-segment byte. We select digit 3, scan its
; seven bits, and emit the mathematical result to SBUF.
target triple = "mcs251-unknown-none"

define i8 @_t2_display_table() {
entry:
  ; t_display[0..9], the same ten glyphs used by a demo-03 lookup table.
  %t_display = alloca [10 x i8]
  %p0 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 0
  store i8 63, ptr %p0
  %p1 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 1
  store i8 6, ptr %p1
  %p2 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 2
  store i8 91, ptr %p2
  %p3 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 3
  store i8 79, ptr %p3
  %p4 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 4
  store i8 102, ptr %p4
  %p5 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 5
  store i8 109, ptr %p5
  %p6 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 6
  store i8 125, ptr %p6
  %p7 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 7
  store i8 7, ptr %p7
  %p8 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 8
  store i8 127, ptr %p8
  %p9 = getelementptr inbounds [10 x i8], ptr %t_display, i32 0, i32 9
  store i8 111, ptr %p9

  ; digit 3: the table lookup is a genuine array access.
  %glyph = load i8, ptr %p3
  ; Unroll the seven bit positions with fixed masks. This expresses the
  ; bit-scan arithmetic without unsupported variable shifts or select nodes.
  %m0 = and i8 %glyph, 1
  %z0 = icmp eq i8 %m0, 0
  br i1 %z0, label %bit0_zero, label %bit0_one
bit0_zero:
  br label %bit0_join
bit0_one:
  br label %bit0_join
bit0_join:
  %c0 = phi i8 [ 0, %bit0_zero ], [ 1, %bit0_one ]
  %m1 = and i8 %glyph, 2
  %z1 = icmp eq i8 %m1, 0
  br i1 %z1, label %bit1_zero, label %bit1_one
bit1_zero:
  br label %bit1_join
bit1_one:
  br label %bit1_join
bit1_join:
  %c1 = phi i8 [ 0, %bit1_zero ], [ 1, %bit1_one ]
  %s1 = add i8 %c0, %c1
  %m2 = and i8 %glyph, 4
  %z2 = icmp eq i8 %m2, 0
  br i1 %z2, label %bit2_zero, label %bit2_one
bit2_zero:
  br label %bit2_join
bit2_one:
  br label %bit2_join
bit2_join:
  %c2 = phi i8 [ 0, %bit2_zero ], [ 1, %bit2_one ]
  %s2 = add i8 %s1, %c2
  %m3 = and i8 %glyph, 8
  %z3 = icmp eq i8 %m3, 0
  br i1 %z3, label %bit3_zero, label %bit3_one
bit3_zero:
  br label %bit3_join
bit3_one:
  br label %bit3_join
bit3_join:
  %c3 = phi i8 [ 0, %bit3_zero ], [ 1, %bit3_one ]
  %s3 = add i8 %s2, %c3
  %m4 = and i8 %glyph, 16
  %z4 = icmp eq i8 %m4, 0
  br i1 %z4, label %bit4_zero, label %bit4_one
bit4_zero:
  br label %bit4_join
bit4_one:
  br label %bit4_join
bit4_join:
  %c4 = phi i8 [ 0, %bit4_zero ], [ 1, %bit4_one ]
  %s4 = add i8 %s3, %c4
  %m5 = and i8 %glyph, 32
  %z5 = icmp eq i8 %m5, 0
  br i1 %z5, label %bit5_zero, label %bit5_one
bit5_zero:
  br label %bit5_join
bit5_one:
  br label %bit5_join
bit5_join:
  %c5 = phi i8 [ 0, %bit5_zero ], [ 1, %bit5_one ]
  %s5 = add i8 %s4, %c5
  %m6 = and i8 %glyph, 64
  %z6 = icmp eq i8 %m6, 0
  br i1 %z6, label %bit6_zero, label %bit6_one
bit6_zero:
  br label %bit6_join
bit6_one:
  br label %bit6_join
bit6_join:
  %c6 = phi i8 [ 0, %bit6_zero ], [ 1, %bit6_one ]
  %sum = add i8 %s5, %c6
  ; 0x4f has five lit segments (a,b,c,d,g); return that count.
  ret i8 %sum
}

define void @_t2_display_emit() {
entry:
  %count = call i8 @_t2_display_table()
  ; SBUF output is intentionally mixed with the pure table/scan algorithm.
  store volatile i8 %count, ptr inttoptr (i32 153 to ptr)
  ret void
}
