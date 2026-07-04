; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s --implicit-check-not=REPGT

define dso_local signext i32 @count_threshold(ptr noundef readonly captures(none) %p, i32 noundef signext %n, i32 noundef signext %threshold) minsize optsize {
; CHECK-LABEL: count_threshold:
; CHECK:       CLR.Q [[IDX:D[0-7]]]
; CHECK-NEXT:  MAXS.L 0, D0
; CHECK-NEXT:  EXTZQ.L D0, [[COUNT:D[0-7]]]
; CHECK:       TEST.Q [[COUNT]], [[COUNT]]
; CHECK:       DEC.Q [[COUNT]]
; CHECK-NEXT:  INC.L [[IDX]]
; CHECK-NEXT:  MOV.L [A0++], [[VAL:D[0-7]]]
; CHECK-NEXT:  CMP.L D1, [[VAL]]
; CHECK:       JLT.W
; CHECK:       MOV.L [[IDX]], D0
; CHECK:       RET
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide.trip.count = zext i32 %smax to i64
  br label %while.cond

while.cond:
  %indvars.iv = phi i64 [ %indvars.iv.next, %while.body ], [ 0, %entry ]
  %i.0 = phi i32 [ %add, %while.body ], [ 0, %entry ]
  %exitcond.not = icmp eq i64 %indvars.iv, %wide.trip.count
  br i1 %exitcond.not, label %while.end, label %while.body

while.body:
  %arrayidx = getelementptr inbounds i32, ptr %p, i64 %indvars.iv
  %v = load i32, ptr %arrayidx, align 4
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %add = add nuw nsw i32 %i.0, 1
  %keep.going = icmp slt i32 %v, %threshold
  br i1 %keep.going, label %while.cond, label %while.end

while.end:
  %i.1 = phi i32 [ %add, %while.body ], [ %smax, %while.cond ]
  ret i32 %i.1
}

declare i32 @llvm.smax.i32(i32, i32)
