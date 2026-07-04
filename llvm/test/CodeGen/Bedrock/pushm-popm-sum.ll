; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O1
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O2
; RUN: llc -mtriple=bedrock -O3 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O3

declare void @use(ptr)

define void @many_csr(ptr %p) {
; O1-LABEL: many_csr:
; O1:       PUSHM {D6,D7,A6,A7}
; O1:       POPM {D6,D7,A6,A7}
; O2-LABEL: many_csr:
; O2:       PUSHM {D6,D7,A6,A7}
; O2:       POPM {D6,D7,A6,A7}
entry:
  call void asm sideeffect "", "~{d6},~{d7},~{a6},~{a7}"()
  call void @use(ptr %p)
  ret void
}

define i64 @sum4(i64 %a, i64 %b, i64 %c, i64 %d) {
; O2-LABEL: sum4:
; O2-NOT:   SUM.Q
; O2:       ADD.Q D1, D0
; O2:       ADD.Q D2, D0
; O2:       ADD.Q D3, D0
; O3-LABEL: sum4:
; O3:       SUM.Q {D0,D1,D2,D3}, D0
entry:
  %s0 = add i64 %a, %b
  %s1 = add i64 %s0, %c
  %s2 = add i64 %s1, %d
  ret i64 %s2
}

define i32 @divmod_decomposed(i32 %x, i32 %d) {
; O1-LABEL: divmod_decomposed:
; O1:       DIVMODS.L D1, D2, D0
; O1-NOT:   MULU.L D1
; O1-NOT:   SUB.L
; O2-LABEL: divmod_decomposed:
; O2:       DIVMODS.L D1, D2, D0
entry:
  %q = sdiv i32 %x, %d
  %prod = mul i32 %q, %d
  %r = sub i32 %x, %prod
  %sum = add i32 %q, %r
  ret i32 %sum
}

define i32 @countdown_sum(ptr %p, i32 %n) {
; O1-LABEL: countdown_sum:
; O1:       DJT.Q
; O1-NOT:   CMP.Q
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop.preheader, label %exit

loop.preheader:
  %wide = zext i32 %n to i64
  br label %loop

loop:
  %iv = phi i64 [ 0, %loop.preheader ], [ %iv.next, %loop ]
  %acc = phi i32 [ 0, %loop.preheader ], [ %sum, %loop ]
  %ptr = getelementptr inbounds i32, ptr %p, i64 %iv
  %val = load i32, ptr %ptr, align 4
  %sum = add nsw i32 %val, %acc
  %iv.next = add nuw nsw i64 %iv, 1
  %done = icmp eq i64 %iv.next, %wide
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i32 [ 0, %entry ], [ %sum, %loop ]
  ret i32 %result
}

define i32 @madd_dot(ptr %a, ptr %b, i32 %n) {
; O2-LABEL: madd_dot:
; O2:       MOV.L [A0++],
; O2-NEXT:  MADD.L [A1++],
; O2-NOT:   MULU.L
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop.preheader, label %exit

loop.preheader:
  %wide = zext i32 %n to i64
  br label %loop

loop:
  %iv = phi i64 [ 0, %loop.preheader ], [ %iv.next, %loop ]
  %acc = phi i32 [ 0, %loop.preheader ], [ %sum, %loop ]
  %ap = getelementptr inbounds i32, ptr %a, i64 %iv
  %bp = getelementptr inbounds i32, ptr %b, i64 %iv
  %av = load i32, ptr %ap, align 4
  %bv = load i32, ptr %bp, align 4
  %prod = mul i32 %av, %bv
  %sum = add i32 %prod, %acc
  %iv.next = add nuw nsw i64 %iv, 1
  %done = icmp eq i64 %iv.next, %wide
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i32 [ 0, %entry ], [ %sum, %loop ]
  ret i32 %result
}

define i32 @weighted_divmod_loop(ptr %p, i32 %n, i32 %d) {
; O2-LABEL: weighted_divmod_loop:
; O2-NOT:   SUB.Q 16, SP
; O2:       DIVMODS.L
; O2:       MULU.L
; O2:       ADD.L
; O2-NOT:   [SP +
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop.preheader, label %exit

loop.preheader:
  %wide = zext i32 %n to i64
  br label %loop

loop:
  %ptr = phi ptr [ %next.ptr, %loop ], [ %p, %loop.preheader ]
  %i = phi i32 [ %next.i, %loop ], [ 0, %loop.preheader ]
  %count = phi i64 [ %next.count, %loop ], [ %wide, %loop.preheader ]
  %acc = phi i32 [ %next.acc, %loop ], [ 0, %loop.preheader ]
  %loaded = load i32, ptr %ptr, align 4
  %sum = add i32 %loaded, %i
  %frozen = freeze i32 %sum
  %q = sdiv i32 %frozen, %d
  %qprod = mul i32 %q, %d
  %r = sub i32 %frozen, %qprod
  %q3 = mul nsw i32 %q, 3
  %acc.q = add nsw i32 %q3, %acc
  %r5 = mul nsw i32 %r, 5
  %next.acc = add nsw i32 %acc.q, %r5
  %next.count = add nsw i64 %count, -1
  %next.i = add nuw nsw i32 %i, 1
  %next.ptr = getelementptr i8, ptr %ptr, i64 4
  %done = icmp eq i64 %next.count, 0
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i32 [ 0, %entry ], [ %next.acc, %loop ]
  ret i32 %result
}
