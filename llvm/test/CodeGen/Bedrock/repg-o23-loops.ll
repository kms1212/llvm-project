; RUN: llc -mtriple=bedrock -O1 < %s | FileCheck %s --check-prefix=O1
; RUN: llc -mtriple=bedrock -O2 < %s | FileCheck %s --check-prefix=O2
; RUN: llc -mtriple=bedrock -O3 < %s | FileCheck %s --check-prefix=O3
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=DIS

declare i32 @llvm.abs.i32(i32, i1 immarg)
declare i32 @llvm.smax.i32(i32, i32)

define i32 @repg_abs_sum(ptr %p, i32 %n) {
; O1-LABEL: repg_abs_sum:
; O1:       DJT.Q {{D[0-7]}},
; O1-NOT:   REPG
;
; O2-LABEL: repg_abs_sum:
; O2:       MOV.Q D0, [[COUNT:D[0-7]]]
; O2-NEXT:  CLR.Q D0
; O2-NEXT:  MAXS.L 0, [[COUNT]]
; O2:       TEST.L [[COUNT]], [[COUNT]]
; O2-NEXT:  JEQ.W
; O2-LABEL: .LBB0_2:
; O2:       REPG [[COUNT]], {
; O2-NEXT:    MOV.L [A0++], D3
; O2-NEXT:    ABS.L D3
; O2-NEXT:    ADD.L D3, D0
; O2-NEXT:  }
; O2-NOT:   DJT.
;
; O3-LABEL: repg_abs_sum:
; O3:       MOV.Q D0, [[COUNT:D[0-7]]]
; O3-NEXT:  CLR.Q D0
; O3-NEXT:  MAXS.L 0, [[COUNT]]
; O3:       TEST.L [[COUNT]], [[COUNT]]
; O3-NEXT:  JEQ.W
; O3-LABEL: .LBB0_2:
; O3:       REPG [[COUNT]], {
; O3-NEXT:    MOV.L [A0++], D3
; O3-NEXT:    ABS.L D3
; O3-NEXT:    ADD.L D3, D0
; O3-NEXT:  }
; O3-NOT:   DJT.
;
; DIS-LABEL: <repg_abs_sum>:
; DIS:       REPG
; DIS:       ENDG
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide.trip.count = zext nneg i32 %smax to i64
  br label %while.cond

while.cond:
  %count = phi i64 [ %next.count, %while.body ], [ %wide.trip.count, %entry ]
  %cur = phi ptr [ %next.ptr, %while.body ], [ %p, %entry ]
  %acc = phi i32 [ %next.acc, %while.body ], [ 0, %entry ]
  %done = icmp eq i64 %count, 0
  br i1 %done, label %while.end, label %while.body

while.body:
  %v = load i32, ptr %cur, align 4
  %abs = tail call i32 @llvm.abs.i32(i32 %v, i1 false)
  %next.acc = add i32 %abs, %acc
  %next.ptr = getelementptr i8, ptr %cur, i64 4
  %next.count = add nsw i64 %count, -1
  br label %while.cond

while.end:
  ret i32 %acc
}
