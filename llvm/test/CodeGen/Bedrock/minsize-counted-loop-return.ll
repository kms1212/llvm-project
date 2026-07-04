; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

define i32 @copy_words(ptr %dst, ptr %src, i32 %n) minsize optsize {
; CHECK-LABEL: copy_words:
; CHECK:       MAXS.L 0, D0
; CHECK-NEXT:  EXTZQ.L D0, [[COUNT:D[0-7]]]
; CHECK:       TEST.Q [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK:       REP [[COUNT]], MOV.L
; CHECK-SAME:  [A1++], [A0++]
; CHECK-NOT:   DEC.L [[COUNT]]
; CHECK:       RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  br label %cond

cond:
  %count = phi i64 [ %count.next, %body ], [ %wide, %entry ]
  %dst.iv = phi ptr [ %dst.next, %body ], [ %dst, %entry ]
  %src.iv = phi ptr [ %src.next, %body ], [ %src, %entry ]
  %done = icmp eq i64 %count, 0
  br i1 %done, label %exit, label %body

body:
  %v = load i32, ptr %src.iv, align 4
  store i32 %v, ptr %dst.iv, align 4
  %dst.next = getelementptr i8, ptr %dst.iv, i64 4
  %src.next = getelementptr i8, ptr %src.iv, i64 4
  %count.next = add nsw i64 %count, -1
  br label %cond

exit:
  ret i32 %smax
}
