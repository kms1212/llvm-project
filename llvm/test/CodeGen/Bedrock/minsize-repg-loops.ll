; RUN: llc -mtriple=bedrock -O2 < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=DIS

declare i32 @llvm.abs.i32(i32, i1 immarg)
declare i32 @llvm.smax.i32(i32, i32)
declare i32 @repg_seed(i32)

define i32 @repg_abs_sum(ptr %p, i32 %n) minsize optsize {
; CHECK-LABEL: repg_abs_sum:
; CHECK:       MOV.Q D0, [[COUNT:D[0-7]]]
; CHECK-NEXT:  CLR.Q [[ACC:D[0-7]]]
; CHECK-NEXT:  MAXS.L 0, [[COUNT]]
; CHECK:       TEST.L [[COUNT]], [[COUNT]]
; CHECK-NEXT:  JEQ.W
; CHECK-LABEL: .LBB0_2:
; CHECK:       REPG [[COUNT]], {
; CHECK-NEXT:    MOV.L [A0++], D3
; CHECK-NEXT:    ABS.L D3
; CHECK-NEXT:    ADD.L D3, [[ACC]]
; CHECK-NEXT:  }
; CHECK-NOT:   DEC.L
; CHECK-NOT:   JNE.W
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

define void @repg_store_arith_i32(ptr %p) minsize optsize {
; CHECK-LABEL: repg_store_arith_i32:
; CHECK:       MOV.L 1, [[VALUE:D[0-7]]]
; CHECK:       MOV.L 12, [[COUNT:D[0-7]]]
; CHECK:       REPG [[COUNT]], {
; CHECK-NEXT:    MOV.L [[VALUE]], [{{A[0-7]}}++]
; CHECK-NEXT:    ADD.L 13, [[VALUE]]
; CHECK-NEXT:  }
; CHECK-NOT:   JMP.W
;
; DIS-LABEL: <repg_store_arith_i32>:
; DIS:       REPG
; DIS:       ENDG
entry:
  br label %cond

cond:
  %i = phi i64 [ 0, %entry ], [ %in, %body ]
  %v = phi i32 [ 1, %entry ], [ %vn, %body ]
  %done = icmp eq i64 %i, 48
  br i1 %done, label %exit, label %body

body:
  %addr = getelementptr i8, ptr %p, i64 %i
  store i32 %v, ptr %addr, align 4
  %vn = add i32 %v, 13
  %in = add i64 %i, 4
  br label %cond

exit:
  ret void
}

define void @repg_address_store_progression(ptr %p) minsize optsize {
; CHECK-LABEL: repg_address_store_progression:
; CHECK:       MOV.L -13, [[Y:D[0-7]]]
; CHECK:       MOV.L 12, [[COUNT:D[0-7]]]
; CHECK:       REPG [[COUNT]], {
; CHECK:       MULU.L [[Y]],
; CHECK:       MOV.L {{D[0-7]}}, [{{A[0-7]}}++]
; CHECK:       ADD.L 7, [[Y]]
; CHECK:       }
; CHECK-NOT:   JMP.W
;
; DIS-LABEL: <repg_address_store_progression>:
; DIS:       REPG
; DIS:       ENDG
entry:
  br label %cond

cond:
  %i = phi i64 [ 0, %entry ], [ %in, %body ]
  %x = phi i32 [ 0, %entry ], [ %xn, %body ]
  %y = phi i32 [ -13, %entry ], [ %yn, %body ]
  %done = icmp eq i64 %i, 48
  br i1 %done, label %exit, label %body

body:
  %mul = mul i32 %x, %y
  %base = getelementptr i8, ptr %p, i64 128
  %addr = getelementptr i8, ptr %base, i64 %i
  store i32 %mul, ptr %addr, align 4
  %yn = add i32 %y, 7
  %xn = add i32 %x, 1
  %in = add i64 %i, 4
  br label %cond

exit:
  ret void
}

define void @rep_adjacent_zero_windows(ptr %p, i32 %seed) minsize optsize {
; CHECK-LABEL: rep_adjacent_zero_windows:
; CHECK:       CALL repg_seed@PCREL16
; CHECK:       CLR.Q [[ZERO:D[0-7]]]
; CHECK:       MOV.L 16, [[COUNT0:D[0-7]]]
; CHECK-NEXT:  REP [[COUNT0]], MOV.L [[ZERO]], [{{A[0-7]}}++]
; CHECK-NOT:   ADD.Q 64
; CHECK-NOT:   JMP.W
; CHECK:       MOV.L 32, [[COUNT1:D[0-7]]]
; CHECK:       REP [[COUNT1]], MOV.L [[ZERO]], [{{A[0-7]}}++]
;
; DIS-LABEL: <rep_adjacent_zero_windows>:
; DIS:       REP
; DIS:       REP
entry:
  %call = tail call i32 @repg_seed(i32 %seed)
  %state = getelementptr inbounds nuw i8, ptr %p, i64 200
  store i32 %call, ptr %state, align 4
  br label %cond0

cond0:
  %i = phi i64 [ 0, %entry ], [ %in, %body0 ]
  %done0 = icmp eq i64 %i, 64
  br i1 %done0, label %pre1, label %body0

body0:
  %addr0 = getelementptr i8, ptr %p, i64 %i
  store i32 0, ptr %addr0, align 4
  %in = add nuw nsw i64 %i, 4
  br label %cond0

pre1:
  %base1 = getelementptr i8, ptr %p, i64 64
  br label %cond1

cond1:
  %j = phi i64 [ 0, %pre1 ], [ %jn, %body1 ]
  %done1 = icmp eq i64 %j, 128
  br i1 %done1, label %exit, label %body1

body1:
  %addr1 = getelementptr i8, ptr %base1, i64 %j
  store i32 0, ptr %addr1, align 4
  %jn = add nuw nsw i64 %j, 4
  br label %cond1

exit:
  ret void
}

define i32 @repg_load_progression_cmp_imm(ptr %p, i32 %seed) minsize optsize {
; CHECK-LABEL: repg_load_progression_cmp_imm:
; CHECK:       MOV.L 8, [[COUNT:D[0-7]]]
; CHECK:       REPG [[COUNT]], {
; CHECK-NEXT:    MOV.L [{{A[0-7]}}++], [[LOAD:D[0-7]]]
; CHECK-NEXT:    ADD.L {{D[0-7]}}, [[LOAD]]
; CHECK-NEXT:    XOR.L [[LOAD]], D0
; CHECK-NEXT:    ADD.L 101, {{D[0-7]}}
; CHECK-NEXT:  }
; CHECK-NOT:   CMP.Q 32
; CHECK-NOT:   JMP.W
;
; DIS-LABEL: <repg_load_progression_cmp_imm>:
; DIS:       REPG
; DIS:       ENDG
entry:
  br label %cond

cond:
  %i = phi i64 [ 0, %entry ], [ %in, %body ]
  %bias = phi i32 [ 0, %entry ], [ %bias.next, %body ]
  %acc = phi i32 [ %seed, %entry ], [ %acc.next, %body ]
  %done = icmp eq i64 %i, 32
  br i1 %done, label %exit, label %body

body:
  %addr = getelementptr i8, ptr %p, i64 %i
  %v = load i32, ptr %addr, align 4
  %sum = add i32 %v, %bias
  %acc.next = xor i32 %sum, %acc
  %bias.next = add i32 %bias, 101
  %in = add i64 %i, 4
  br label %cond

exit:
  ret i32 %acc
}
