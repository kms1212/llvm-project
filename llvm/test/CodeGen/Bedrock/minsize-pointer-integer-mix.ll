; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

define i32 @pointer_integer_mix(ptr %base, ptr %limit, i32 %n,
                                i64 %byte_bias) minsize optsize {
; CHECK-LABEL: pointer_integer_mix:
; CHECK:       MOV.Q D0, D2
; CHECK:       DIVS.Q 4, D1
; CHECK-NOT:   ADD.Q A2, D1
; CHECK-NOT:   SAR.Q 63
; CHECK:       LEA [A0 + D1 * 4], A2
; CHECK:       LEA [A0 + D2.L * 4], A3
; CHECK:       EXTZQ.L D2, D2
; CHECK:       CMP.Q D4, D2
; CHECK:       JEQ.W
; CHECK:       CMP.Q D4, A1
; CHECK:       JGE.W
; CHECK:       INC.L D0
; CHECK:       MOV.L [A3++], [[PVAL:D[0-7]]]
; CHECK:       MOV.L [A2++], D0
; CHECK:       INC.Q D4
; CHECK-NOT:   MOV.Q 1
; CHECK:       RET
entry:
  %idx.ext = sext i32 %n to i64
  %add.ptr = getelementptr inbounds i32, ptr %base, i64 %idx.ext
  %div = sdiv i64 %byte_bias, 4
  %add.ptr1 = getelementptr inbounds i32, ptr %base, i64 %div
  %sub.ptr.lhs.cast = ptrtoint ptr %limit to i64
  %sub.ptr.rhs.cast = ptrtoint ptr %base to i64
  %sub.ptr.sub = sub i64 %sub.ptr.lhs.cast, %sub.ptr.rhs.cast
  %sub.ptr.div = ashr exact i64 %sub.ptr.sub, 2
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide.trip.count = zext nneg i32 %smax to i64
  br label %while.cond

while.cond:
  %indvars.iv = phi i64 [ %indvars.iv.next, %while.body ], [ 0, %entry ]
  %q.0 = phi ptr [ %add.ptr4, %while.body ], [ %add.ptr1, %entry ]
  %p.0 = phi ptr [ %add.ptr3, %while.body ], [ %add.ptr, %entry ]
  %acc.0 = phi i32 [ %spec.select, %while.body ], [ 0, %entry ]
  %exitcond.not = icmp eq i64 %indvars.iv, %wide.trip.count
  br i1 %exitcond.not, label %while.end, label %while.body

while.body:
  %0 = load i32, ptr %p.0, align 4
  %1 = load i32, ptr %q.0, align 4
  %add.ptr3 = getelementptr inbounds nuw i8, ptr %p.0, i64 4
  %add.ptr4 = getelementptr inbounds nuw i8, ptr %q.0, i64 4
  %cmp5 = icmp slt i64 %sub.ptr.div, %indvars.iv
  %add7 = zext i1 %cmp5 to i32
  %add = add i32 %acc.0, %add7
  %add2 = add i32 %add, %0
  %spec.select = add i32 %add2, %1
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  br label %while.cond

while.end:
  ret i32 %acc.0
}
