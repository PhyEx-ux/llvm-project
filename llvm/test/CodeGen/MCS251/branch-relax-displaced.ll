; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O1 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -mcs251-object-format=elf -filetype=obj -O2 < %s -o %t.o
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -verify-machineinstrs -filetype=obj -O0 < %s -o %t-o0.o
;
; E1 regression: nested loops with diamonds make MachineBlockPlacement
; displace a long-branch skip block away from its branch, pushing the rel8
; displacement past +127 (previously a hard "MCS251 PC-relative branch out
; of range" error at the assembler backend; with -O0 the layout keeps the
; skip adjacent, so only the O1/O2 pipelines hit the displacement).
;
; The post-layout MCS251BranchRelaxation pass must retarget the displaced
; conditional to a fresh 4-byte-away trampoline block that carries an ejmp
; to the original destination -- same opcode, same condition, same taken
; edge.  Structure check:
;
; CHECK: je .LBB0_48
; CHECK-NEXT: ejmp .LBB0_1
; CHECK-NEXT: .LBB0_48:
; CHECK-NEXT: ejmp .LBB0_8
;
; The obj RUN lines above are the acceptance end: the module must assemble
; with -verify-machineinstrs at every optimization level.

; ModuleID = 'min2.c'
source_filename = "min2.c"
target datalayout = "E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8"
target triple = "mcs251-unknown-none"

; Function Attrs: nofree norecurse nosync nounwind memory(inaccessiblemem: readwrite)
define dso_local i32 @f(i32 noundef %0, i32 noundef %1) local_unnamed_addr #0 {
  %3 = alloca i8, align 1
  %4 = alloca i8, align 1
  %5 = alloca i8, align 1
  %6 = alloca i8, align 1
  %7 = alloca i8, align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %3)
  call void @llvm.lifetime.start.p0(ptr nonnull %4)
  call void @llvm.lifetime.start.p0(ptr nonnull %5)
  call void @llvm.lifetime.start.p0(ptr nonnull %6)
  call void @llvm.lifetime.start.p0(ptr nonnull %7)
  br label %8

8:                                                ; preds = %2, %76
  %9 = phi i32 [ 0, %2 ], [ %77, %76 ]
  %10 = phi i32 [ %0, %2 ], [ %97, %76 ]
  %11 = load volatile i8, ptr %3, align 1, !tbaa !8
  %12 = zext i8 %11 to i32
  %13 = icmp eq i32 %10, %12
  br i1 %13, label %16, label %14

14:                                               ; preds = %62, %8
  %15 = phi i32 [ %10, %8 ], [ %73, %62 ]
  br label %83

16:                                               ; preds = %8
  %17 = load volatile i8, ptr %4, align 1, !tbaa !8
  %18 = zext i8 %17 to i32
  %19 = add nuw nsw i32 %10, %18
  %20 = load volatile i8, ptr %4, align 1, !tbaa !8
  %21 = zext i8 %20 to i32
  %22 = add nuw nsw i32 %19, %21
  %23 = load volatile i8, ptr %4, align 1, !tbaa !8
  %24 = zext i8 %23 to i32
  %25 = add nuw nsw i32 %22, %24
  %26 = load volatile i8, ptr %4, align 1, !tbaa !8
  %27 = zext i8 %26 to i32
  %28 = add nuw nsw i32 %25, %27
  %29 = load volatile i8, ptr %4, align 1, !tbaa !8
  %30 = zext i8 %29 to i32
  %31 = add nuw nsw i32 %28, %30
  %32 = load volatile i8, ptr %4, align 1, !tbaa !8
  %33 = zext i8 %32 to i32
  %34 = add nuw nsw i32 %31, %33
  %35 = load volatile i8, ptr %4, align 1, !tbaa !8
  %36 = zext i8 %35 to i32
  %37 = add nuw nsw i32 %34, %36
  %38 = load volatile i8, ptr %4, align 1, !tbaa !8
  %39 = zext i8 %38 to i32
  %40 = add nuw nsw i32 %37, %39
  %41 = load volatile i8, ptr %4, align 1, !tbaa !8
  %42 = zext i8 %41 to i32
  %43 = add nuw nsw i32 %40, %42
  %44 = load volatile i8, ptr %4, align 1, !tbaa !8
  %45 = zext i8 %44 to i32
  %46 = add nuw nsw i32 %43, %45
  %47 = load volatile i8, ptr %4, align 1, !tbaa !8
  %48 = zext i8 %47 to i32
  %49 = add nuw nsw i32 %46, %48
  %50 = load volatile i8, ptr %4, align 1, !tbaa !8
  %51 = zext i8 %50 to i32
  %52 = add nuw nsw i32 %49, %51
  %53 = load volatile i8, ptr %4, align 1, !tbaa !8
  %54 = zext i8 %53 to i32
  %55 = add nuw nsw i32 %52, %54
  %56 = load volatile i8, ptr %4, align 1, !tbaa !8
  %57 = zext i8 %56 to i32
  %58 = add nuw nsw i32 %55, %57
  %59 = load volatile i8, ptr %4, align 1, !tbaa !8
  %60 = zext i8 %59 to i32
  %61 = add nuw nsw i32 %58, %60
  br label %62

62:                                               ; preds = %16, %62
  %63 = phi i32 [ %74, %62 ], [ 0, %16 ]
  %64 = phi i32 [ %73, %62 ], [ %61, %16 ]
  %65 = load volatile i8, ptr %7, align 1, !tbaa !8
  %66 = zext i8 %65 to i32
  %67 = add i32 %64, %66
  %68 = load volatile i8, ptr %5, align 1, !tbaa !8
  %69 = zext i8 %68 to i32
  %70 = add i32 %67, %69
  %71 = load volatile i8, ptr %6, align 1, !tbaa !8
  %72 = zext i8 %71 to i32
  %73 = add i32 %70, %72
  %74 = add nuw nsw i32 %63, 1
  %75 = icmp eq i32 %74, 16
  br i1 %75, label %14, label %62, !llvm.loop !9

76:                                               ; preds = %93
  %77 = add nuw nsw i32 %9, 1
  %78 = icmp eq i32 %77, 3
  br i1 %78, label %79, label %8, !llvm.loop !11

79:                                               ; preds = %76
  %80 = load volatile i8, ptr %7, align 1, !tbaa !8
  %81 = zext i8 %80 to i32
  %82 = icmp eq i32 %97, %81
  br i1 %82, label %100, label %104

83:                                               ; preds = %14, %93
  %84 = phi i32 [ %98, %93 ], [ 0, %14 ]
  %85 = phi i32 [ %97, %93 ], [ %15, %14 ]
  %86 = load volatile i8, ptr %5, align 1, !tbaa !8
  %87 = zext i8 %86 to i32
  %88 = icmp eq i32 %85, %87
  br i1 %88, label %93, label %89

89:                                               ; preds = %83
  %90 = load volatile i8, ptr %6, align 1, !tbaa !8
  %91 = zext i8 %90 to i32
  %92 = add i32 %85, %91
  br label %93

93:                                               ; preds = %89, %83
  %94 = phi i32 [ %92, %89 ], [ %85, %83 ]
  %95 = load volatile i8, ptr %4, align 1, !tbaa !8
  %96 = zext i8 %95 to i32
  %97 = add i32 %94, %96
  %98 = add nuw nsw i32 %84, 1
  %99 = icmp eq i32 %98, 15
  br i1 %99, label %76, label %83, !llvm.loop !12

100:                                              ; preds = %79
  %101 = load volatile i8, ptr %4, align 1, !tbaa !8
  %102 = zext i8 %101 to i32
  %103 = add nuw nsw i32 %97, %102
  br label %104

104:                                              ; preds = %79, %100
  %105 = phi i32 [ %103, %100 ], [ %97, %79 ]
  %106 = load volatile i8, ptr %7, align 1, !tbaa !8
  %107 = zext i8 %106 to i32
  %108 = icmp eq i32 %105, %107
  br i1 %108, label %109, label %113

109:                                              ; preds = %104
  %110 = load volatile i8, ptr %4, align 1, !tbaa !8
  %111 = zext i8 %110 to i32
  %112 = add nuw nsw i32 %105, %111
  br label %113

113:                                              ; preds = %109, %104
  %114 = phi i32 [ %112, %109 ], [ %105, %104 ]
  %115 = load volatile i8, ptr %7, align 1, !tbaa !8
  %116 = zext i8 %115 to i32
  %117 = icmp eq i32 %114, %116
  br i1 %117, label %118, label %122

118:                                              ; preds = %113
  %119 = load volatile i8, ptr %4, align 1, !tbaa !8
  %120 = zext i8 %119 to i32
  %121 = add nuw nsw i32 %114, %120
  br label %122

122:                                              ; preds = %118, %113
  %123 = phi i32 [ %121, %118 ], [ %114, %113 ]
  %124 = load volatile i8, ptr %7, align 1, !tbaa !8
  %125 = zext i8 %124 to i32
  %126 = icmp eq i32 %123, %125
  br i1 %126, label %127, label %131

127:                                              ; preds = %122
  %128 = load volatile i8, ptr %4, align 1, !tbaa !8
  %129 = zext i8 %128 to i32
  %130 = add nuw nsw i32 %123, %129
  br label %131

131:                                              ; preds = %127, %122
  %132 = phi i32 [ %130, %127 ], [ %123, %122 ]
  %133 = load volatile i8, ptr %7, align 1, !tbaa !8
  %134 = zext i8 %133 to i32
  %135 = icmp eq i32 %132, %134
  br i1 %135, label %136, label %140

136:                                              ; preds = %131
  %137 = load volatile i8, ptr %4, align 1, !tbaa !8
  %138 = zext i8 %137 to i32
  %139 = add nuw nsw i32 %132, %138
  br label %140

140:                                              ; preds = %136, %131
  %141 = phi i32 [ %139, %136 ], [ %132, %131 ]
  %142 = load volatile i8, ptr %7, align 1, !tbaa !8
  %143 = zext i8 %142 to i32
  %144 = icmp eq i32 %141, %143
  br i1 %144, label %145, label %149

145:                                              ; preds = %140
  %146 = load volatile i8, ptr %4, align 1, !tbaa !8
  %147 = zext i8 %146 to i32
  %148 = add nuw nsw i32 %141, %147
  br label %149

149:                                              ; preds = %145, %140
  %150 = phi i32 [ %148, %145 ], [ %141, %140 ]
  %151 = load volatile i8, ptr %7, align 1, !tbaa !8
  %152 = zext i8 %151 to i32
  %153 = icmp eq i32 %150, %152
  br i1 %153, label %154, label %158

154:                                              ; preds = %149
  %155 = load volatile i8, ptr %4, align 1, !tbaa !8
  %156 = zext i8 %155 to i32
  %157 = add nuw nsw i32 %150, %156
  br label %158

158:                                              ; preds = %154, %149
  %159 = phi i32 [ %157, %154 ], [ %150, %149 ]
  %160 = load volatile i8, ptr %7, align 1, !tbaa !8
  %161 = zext i8 %160 to i32
  %162 = icmp eq i32 %159, %161
  br i1 %162, label %163, label %167

163:                                              ; preds = %158
  %164 = load volatile i8, ptr %4, align 1, !tbaa !8
  %165 = zext i8 %164 to i32
  %166 = add nuw nsw i32 %159, %165
  br label %167

167:                                              ; preds = %163, %158
  %168 = phi i32 [ %166, %163 ], [ %159, %158 ]
  %169 = load volatile i8, ptr %7, align 1, !tbaa !8
  %170 = zext i8 %169 to i32
  %171 = icmp eq i32 %168, %170
  br i1 %171, label %172, label %176

172:                                              ; preds = %167
  %173 = load volatile i8, ptr %4, align 1, !tbaa !8
  %174 = zext i8 %173 to i32
  %175 = add nuw nsw i32 %168, %174
  br label %176

176:                                              ; preds = %172, %167
  %177 = phi i32 [ %175, %172 ], [ %168, %167 ]
  %178 = load volatile i8, ptr %7, align 1, !tbaa !8
  %179 = zext i8 %178 to i32
  %180 = icmp eq i32 %177, %179
  br i1 %180, label %181, label %185

181:                                              ; preds = %176
  %182 = load volatile i8, ptr %4, align 1, !tbaa !8
  %183 = zext i8 %182 to i32
  %184 = add nuw nsw i32 %177, %183
  br label %185

185:                                              ; preds = %181, %176
  %186 = phi i32 [ %184, %181 ], [ %177, %176 ]
  %187 = load volatile i8, ptr %7, align 1, !tbaa !8
  %188 = zext i8 %187 to i32
  %189 = icmp eq i32 %186, %188
  br i1 %189, label %190, label %192

190:                                              ; preds = %185
  %191 = load volatile i8, ptr %4, align 1, !tbaa !8
  br label %192

192:                                              ; preds = %190, %185
  call void @llvm.lifetime.end.p0(ptr nonnull %3)
  call void @llvm.lifetime.end.p0(ptr nonnull %4)
  call void @llvm.lifetime.end.p0(ptr nonnull %5)
  call void @llvm.lifetime.end.p0(ptr nonnull %6)
  call void @llvm.lifetime.end.p0(ptr nonnull %7)
  ret i32 undef
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

attributes #0 = { nofree norecurse nosync nounwind memory(inaccessiblemem: readwrite) "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}
!llvm.errno.tbaa = !{!3}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{!"clang version 24.0.0git (https://github.com/llvm/llvm-project.git c9b3382a517e4605790589263b1054fce3627969)"}
!3 = !{!4, !5, i64 0}
!4 = !{!"__libc_errno", !5, i64 0}
!5 = !{!"int", !6, i64 0}
!6 = !{!"omnipotent char", !7, i64 0}
!7 = !{!"Simple C/C++ TBAA"}
!8 = !{!6, !6, i64 0}
!9 = distinct !{!9, !10}
!10 = !{!"llvm.loop.mustprogress"}
!11 = distinct !{!11, !10}
!12 = distinct !{!12, !10}
