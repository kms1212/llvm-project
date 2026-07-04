; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

define i32 @divmod_madd(ptr %p, i32 %n, i32 %d) minsize optsize {
; CHECK-LABEL: divmod_madd:
; CHECK:       MOV.L 3, [[C:D[0-7]]]
; CHECK:       MADD.L {{D[0-7]}}, [[C]], D0
; CHECK-NOT:   MULU.L [[C]],
; CHECK:       RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  br label %cond

cond:
  %iv = phi i64 [ 0, %entry ], [ %iv.next, %body ]
  %acc = phi i32 [ 0, %entry ], [ %next, %body ]
  %done = icmp eq i64 %iv, %wide
  br i1 %done, label %exit, label %body

body:
  %ptr = getelementptr i32, ptr %p, i64 %iv
  %load = load i32, ptr %ptr, align 4
  %idx = trunc i64 %iv to i32
  %v = add i32 %load, %idx
  %q = sdiv i32 %v, %d
  %r = srem i32 %v, %d
  %q3 = mul i32 %q, 3
  %sum = add i32 %q3, %acc
  %r5 = mul i32 %r, 5
  %next = add i32 %sum, %r5
  %iv.next = add nuw nsw i64 %iv, 1
  br label %cond

exit:
  ret i32 %acc
}
