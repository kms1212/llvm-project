; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

define signext i32 @scan_until_zero(ptr readonly %p, i32 signext %n) minsize optsize {
; CHECK-LABEL: scan_until_zero:
; CHECK:       MAXS.L 0, D0
; CHECK-NEXT:  EXTZQ.L D0, [[COUNT:D[0-7]]]
; CHECK-NEXT:  CLR.Q [[IDX:D[0-7]]]
; CHECK:       TEST.Q [[COUNT]], [[COUNT]]
; CHECK:       MOV.L [A0++], D1
; CHECK-NOT:   ADD.Q 4, A0
; CHECK:       TEST.L D1, D1
; CHECK:       DEC.Q [[COUNT]]
; CHECK-NEXT:  INC.L [[IDX]]
; CHECK:       MOV.L [[IDX]], D0
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide.trip.count = zext nneg i32 %smax to i64
  br label %while.cond

while.cond:
  %ptr = phi ptr [ %ptr.next, %if.end ], [ %p, %entry ]
  %count = phi i64 [ %count.next, %if.end ], [ %wide.trip.count, %entry ]
  %index = phi i32 [ %index.next, %if.end ], [ 0, %entry ]
  %done = icmp eq i64 %count, 0
  br i1 %done, label %cleanup, label %while.body

while.body:
  %v = load i32, ptr %ptr, align 4
  %is.zero = icmp eq i32 %v, 0
  br i1 %is.zero, label %cleanup, label %if.end

if.end:
  %index.next = add nuw i32 %index, 1
  %ptr.next = getelementptr i8, ptr %ptr, i64 4
  %count.next = add nsw i64 %count, -1
  br label %while.cond

cleanup:
  %result = phi i32 [ %index, %while.body ], [ %smax, %while.cond ]
  ret i32 %result
}
