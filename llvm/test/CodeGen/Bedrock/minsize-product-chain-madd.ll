; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

define i32 @fir3_chain(ptr %dst, ptr %src, i32 %n, i32 %ca, i32 %cb, i32 %cc) minsize optsize {
; CHECK-LABEL: fir3_chain:
; CHECK:       PUSHM {D6,D7}
; CHECK:       ADD.Q 8, A1
; CHECK:       MAXS.L 0, D0
; CHECK-NEXT:  EXTZQ.L D0, [[COUNT:D[0-7]]]
; CHECK:       TEST.Q [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK:       REPG [[COUNT]], {
; CHECK:       MOV.L [A1 + -8], [[ACC:D[0-7]]]
; CHECK-NEXT:  MULU.L D1, [[ACC]]
; CHECK:       MADD.L [A1 + -4], D2,
; CHECK:       MADD.L [A1++], D3,
; CHECK:       MOV.L {{D[0-7]}}, [A0++]
; CHECK:       }
; CHECK:       POPM {D6,D7}
; CHECK:       RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  %src.start = getelementptr i8, ptr %src, i64 4
  br label %cond

cond:
  %count = phi i64 [ %count.next, %body ], [ %wide, %entry ]
  %src.iv = phi ptr [ %src.next, %body ], [ %src.start, %entry ]
  %dst.iv = phi ptr [ %dst.next, %body ], [ %dst, %entry ]
  %done = icmp eq i64 %count, 0
  br i1 %done, label %exit, label %body

body:
  %p0 = getelementptr i8, ptr %src.iv, i64 -4
  %v0 = load i32, ptr %p0, align 4
  %v1 = load i32, ptr %src.iv, align 4
  %p2 = getelementptr i8, ptr %src.iv, i64 4
  %v2 = load i32, ptr %p2, align 4
  %m0 = mul nsw i32 %v0, %ca
  %m1 = mul nsw i32 %v1, %cb
  %s1 = add nsw i32 %m1, %m0
  %m2 = mul nsw i32 %v2, %cc
  %s2 = add nsw i32 %s1, %m2
  store i32 %s2, ptr %dst.iv, align 4
  %dst.next = getelementptr i8, ptr %dst.iv, i64 4
  %src.next = getelementptr i8, ptr %src.iv, i64 4
  %count.next = add nsw i64 %count, -1
  br label %cond

exit:
  ret i32 %smax
}

declare i32 @llvm.smax.i32(i32, i32)
