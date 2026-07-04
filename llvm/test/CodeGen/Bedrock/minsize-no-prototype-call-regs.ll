; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)
declare i32 @ext_add(...)
declare i32 @ext_mix(...)
declare i32 @ext_fold(...)

define i32 @call_heavy(ptr readonly %p, i32 %n) minsize nounwind optsize {
; CHECK-LABEL: call_heavy:
; CHECK:       PUSHM {D6,D7,A6,A7}
; CHECK-NOT:   SUB.Q
; CHECK-NOT:   MOV.L {{.*}}, [SP + 0]
; CHECK-NOT:   MOV.L {{.*}}, [SP + 16]
; CHECK:       MOV.Q {{.*}}, D1
; CHECK-NEXT:  CALL ext_add@PCREL16
; CHECK:       MOV.Q {{.*}}, D1
; CHECK-NEXT:  CALL ext_mix@PCREL16
; CHECK:       MOV.Q {{.*}}, D1
; CHECK-NEXT:  CALL ext_fold@PCREL16
; CHECK-NOT:   ADD.Q {{.*}}, SP
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide.trip.count = zext nneg i32 %smax to i64
  br label %while.cond

while.cond:
  %ptr = phi ptr [ %ptr.next, %while.body ], [ %p, %entry ]
  %count = phi i64 [ %count.next, %while.body ], [ %wide.trip.count, %entry ]
  %index = phi i32 [ %index.next, %while.body ], [ 0, %entry ]
  %acc = phi i32 [ %call2, %while.body ], [ 0, %entry ]
  %done = icmp eq i64 %count, 0
  br i1 %done, label %while.end, label %while.body

while.body:
  %v = load i32, ptr %ptr, align 4
  %call = tail call i32 (...) @ext_add(i32 %acc, i32 %v)
  %call1 = tail call i32 (...) @ext_mix(i32 %call, i32 %index)
  %add = add i32 %index, %v
  %call2 = tail call i32 (...) @ext_fold(i32 %call1, i32 %add)
  %ptr.next = getelementptr i8, ptr %ptr, i64 4
  %index.next = add nuw i32 %index, 1
  %count.next = add nsw i64 %count, -1
  br label %while.cond

while.end:
  ret i32 %acc
}
