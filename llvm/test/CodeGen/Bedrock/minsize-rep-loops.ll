; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)
declare void @use(ptr, ptr, i32)

define void @zero_tail_and_call(ptr %state) minsize optsize {
; CHECK-LABEL: zero_tail_and_call:
; CHECK:       MOV.L 128, D0
; CHECK:       LEA [A0 + 4528], [[PTR:A[0-7]]]
; CHECK:       CLR.Q [[ZERO:D[0-7]]]
; CHECK-NOT:   TEST
; CHECK-NOT:   JLE.W
; CHECK:       REP D0, MOV.L [[ZERO]], {{\[}}[[PTR]]++{{\]}}
; CHECK-NOT:   MOV.B
entry:
  %scratch = getelementptr inbounds i8, ptr %state, i64 4528
  br label %cond

cond:
  %idx = phi i64 [ 4528, %entry ], [ %next, %body ]
  %done = icmp eq i64 %idx, 5040
  br i1 %done, label %exit, label %body

body:
  %addr = getelementptr inbounds i8, ptr %state, i64 %idx
  store i8 0, ptr %addr, align 1
  %next = add nuw nsw i64 %idx, 1
  br label %cond

exit:
  call void @use(ptr %state, ptr %scratch, i32 512)
  ret void
}

define i32 @sum(ptr %p, i32 %n) minsize optsize {
; CHECK-LABEL: sum:
; CHECK:       CLR.Q [[ACC:D[0-7]]]
; CHECK:       MAXS.L 0, [[COUNT:D[0-7]]]
; CHECK:       TEST.L [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK:       REP [[COUNT]], ADD.L
; CHECK-SAME:  [A0++], [[ACC]]
; CHECK-NOT:   DEC.L [[COUNT]]
; CHECK:       RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  br label %cond

cond:
  %idx = phi i64 [ %idx.next, %body ], [ 0, %entry ]
  %acc = phi i32 [ %sum.next, %body ], [ 0, %entry ]
  %done = icmp eq i64 %idx, %wide
  br i1 %done, label %exit, label %body

body:
  %addr = getelementptr inbounds nuw i32, ptr %p, i64 %idx
  %v = load i32, ptr %addr, align 4
  %sum.next = add nsw i32 %v, %acc
  %idx.next = add nuw nsw i64 %idx, 1
  br label %cond

exit:
  ret i32 %acc
}

define i32 @scan_until_zero(ptr %p, i32 %n) minsize optsize {
; CHECK-LABEL: scan_until_zero:
; CHECK:       MAXS.L 0, D0
; CHECK-NEXT:  EXTZQ.L D0, [[COUNT:D[0-7]]]
; CHECK:       TEST.Q [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK:       MOV.L [[COUNT]], [[IDX:D[0-7]]]
; CHECK-NEXT:  REPNE [[IDX]], MOV.L
; CHECK-SAME:  [A0++], [[TMP:D[0-7]]]
; CHECK-NEXT:  MOV.L [[COUNT]], D0
; CHECK-NEXT:  SUB.L [[IDX]], D0
; CHECK-NEXT:  RET
; CHECK:       CLR.Q D0
; CHECK-NEXT:  RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  br label %cond

cond:
  %idx = phi i64 [ %idx.next, %cont ], [ 0, %entry ]
  %done = icmp eq i64 %idx, %wide
  br i1 %done, label %cleanup, label %body

body:
  %addr = getelementptr inbounds nuw i32, ptr %p, i64 %idx
  %v = load i32, ptr %addr, align 4
  %is.zero = icmp eq i32 %v, 0
  br i1 %is.zero, label %found, label %cont

cont:
  %idx.next = add nuw nsw i64 %idx, 1
  br label %cond

found:
  %idx32 = trunc nuw nsw i64 %idx to i32
  br label %cleanup

cleanup:
  %result = phi i32 [ %idx32, %found ], [ %smax, %cond ]
  ret i32 %result
}

define i32 @copy_prefix(ptr %dst, ptr %src, i32 %n) minsize optsize {
; CHECK-LABEL: copy_prefix:
; CHECK:       MAXS.L 0, D0
; CHECK-NEXT:  EXTZQ.L D0, [[COUNT:D[0-7]]]
; CHECK:       TEST.Q [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK:       MOV.L [[COUNT]], [[IDX:D[0-7]]]
; CHECK-NEXT:  REPNE [[IDX]], MOV.L
; CHECK-SAME:  [A1++], [A0++]
; CHECK-NEXT:  MOV.L [[COUNT]], D0
; CHECK-NEXT:  SUB.L [[IDX]], D0
; CHECK-NEXT:  RET
; CHECK:       CLR.Q D0
; CHECK-NEXT:  RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  br label %cond

cond:
  %idx = phi i64 [ %idx.next, %cont ], [ 0, %entry ]
  %done = icmp eq i64 %idx, %wide
  br i1 %done, label %cleanup, label %body

body:
  %src.addr = getelementptr inbounds nuw i32, ptr %src, i64 %idx
  %dst.addr = getelementptr inbounds nuw i32, ptr %dst, i64 %idx
  %v = load i32, ptr %src.addr, align 4
  store i32 %v, ptr %dst.addr, align 4
  %is.zero = icmp eq i32 %v, 0
  br i1 %is.zero, label %found, label %cont

cont:
  %idx.next = add nuw nsw i64 %idx, 1
  br label %cond

found:
  %idx32 = trunc nuw nsw i64 %idx to i32
  br label %cleanup

cleanup:
  %result = phi i32 [ %idx32, %found ], [ %smax, %cond ]
  ret i32 %result
}

define i32 @bias_sum(ptr %p, i32 %n, i32 %bias) minsize optsize {
; CHECK-LABEL: bias_sum:
; CHECK:       CLR.Q [[ACC:D[0-7]]]
; CHECK:       TEST.L [[COUNT:D[0-7]]], [[COUNT]]
; CHECK:       ADD.L [A0++], [[ACC]]
; CHECK-NEXT:  ADD.L D1, [[ACC]]
; CHECK-NEXT:  DEC.L [[COUNT]]
; CHECK-NEXT:  JMP.W
; CHECK:       RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  br label %cond

cond:
  %idx = phi i64 [ %idx.next, %body ], [ 0, %entry ]
  %acc = phi i32 [ %sum.bias, %body ], [ 0, %entry ]
  %done = icmp eq i64 %idx, %wide
  br i1 %done, label %exit, label %body

body:
  %addr = getelementptr inbounds nuw i32, ptr %p, i64 %idx
  %v = load i32, ptr %addr, align 4
  %sum = add nsw i32 %v, %acc
  %sum.bias = add nsw i32 %sum, %bias
  %idx.next = add nuw nsw i64 %idx, 1
  br label %cond

exit:
  ret i32 %acc
}

define i32 @prefix_lt(ptr %p, i32 %n, i32 %threshold) minsize optsize {
; CHECK-LABEL: prefix_lt:
; CHECK:       MAXS.L 0, D0
; CHECK-NEXT:  EXTZQ.L D0, [[COUNT:D[0-7]]]
; CHECK:       TEST.Q [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK:       MOV.L [[COUNT]], [[IDX:D[0-7]]]
; CHECK-NEXT:  REPGT [[IDX]], CMP.L
; CHECK-SAME:  [A0++], D1
; CHECK-NEXT:  MOV.L [[COUNT]], D0
; CHECK-NEXT:  SUB.L [[IDX]], D0
; CHECK-NEXT:  RET
; CHECK:       CLR.Q D0
; CHECK-NEXT:  RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide = zext i32 %smax to i64
  br label %cond

cond:
  %idx = phi i64 [ %idx.next, %cont ], [ 0, %entry ]
  %done = icmp eq i64 %idx, %wide
  br i1 %done, label %cleanup, label %body

body:
  %addr = getelementptr inbounds nuw i32, ptr %p, i64 %idx
  %v = load i32, ptr %addr, align 4
  %ge = icmp sge i32 %v, %threshold
  br i1 %ge, label %found, label %cont

cont:
  %idx.next = add nuw nsw i64 %idx, 1
  br label %cond

found:
  %idx32 = trunc nuw nsw i64 %idx to i32
  br label %cleanup

cleanup:
  %result = phi i32 [ %idx32, %found ], [ %smax, %cond ]
  ret i32 %result
}
