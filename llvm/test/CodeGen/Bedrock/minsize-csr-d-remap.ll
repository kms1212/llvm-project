; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

define i32 @register_pressure(ptr %p, i32 %n) minsize optsize {
; CHECK-LABEL: register_pressure:
; CHECK:       PUSHM {D6,D7}
; CHECK:       MOV.Q D0, [[COUNT:D[0-7]]]
; CHECK:       MAXS.L 0, [[COUNT]]
; CHECK:       TEST.L [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK:       MOV.L [A0++], [[LOAD:A[0-7]]]
; CHECK-NEXT:  ADD.Q [[LOAD]], [[ACC:A[0-7]]]
; CHECK-NEXT:  DEC.L [[COUNT]]
; CHECK:       SUM.L {D0,D2,D3,D4,D5,D6,D7,[[ACC]]}, D0
; CHECK-NEXT:  POPM {D6,D7}
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  br label %while.cond

while.cond:
  %i.0 = phi i32 [ 0, %entry ], [ %add8, %while.body ]
  %a.0 = phi i32 [ 1, %entry ], [ %add, %while.body ]
  %b.0 = phi i32 [ 2, %entry ], [ %add1, %while.body ]
  %c.0 = phi i32 [ 3, %entry ], [ %add2, %while.body ]
  %d.0 = phi i32 [ 4, %entry ], [ %add3, %while.body ]
  %e.0 = phi i32 [ 5, %entry ], [ %add4, %while.body ]
  %f.0 = phi i32 [ 6, %entry ], [ %add5, %while.body ]
  %g.0 = phi i32 [ 7, %entry ], [ %add6, %while.body ]
  %h.0 = phi i32 [ 8, %entry ], [ %add7, %while.body ]
  %p.addr.0 = phi ptr [ %p, %entry ], [ %add.ptr, %while.body ]
  %exitcond.not = icmp eq i32 %i.0, %smax
  br i1 %exitcond.not, label %while.end, label %while.body

while.body:
  %0 = load i32, ptr %p.addr.0, align 4
  %add.ptr = getelementptr inbounds nuw i8, ptr %p.addr.0, i64 4
  %add = add nsw i32 %0, %a.0
  %add1 = add nsw i32 %add, %b.0
  %add2 = add nsw i32 %add1, %c.0
  %add3 = add nsw i32 %add2, %d.0
  %add4 = add nsw i32 %add3, %e.0
  %add5 = add nsw i32 %add4, %f.0
  %add6 = add nsw i32 %add5, %g.0
  %add7 = add nsw i32 %add6, %h.0
  %add8 = add nuw i32 %i.0, 1
  br label %while.cond

while.end:
  %add9 = add nsw i32 %b.0, %a.0
  %add10 = add nsw i32 %add9, %c.0
  %add11 = add nsw i32 %add10, %d.0
  %add12 = add nsw i32 %add11, %e.0
  %add13 = add nsw i32 %add12, %f.0
  %add14 = add nsw i32 %add13, %g.0
  %add15 = add nsw i32 %add14, %h.0
  ret i32 %add15
}
