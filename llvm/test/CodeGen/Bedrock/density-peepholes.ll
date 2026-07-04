; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

define void @add_store_i32(ptr %p, i32 %x) optsize {
; CHECK-LABEL: add_store_i32:
; CHECK:       ADD.L D0, [A0]
; CHECK-NEXT:  RET
entry:
  %v = load i32, ptr %p, align 4
  %s = add i32 %v, %x
  store i32 %s, ptr %p, align 4
  ret void
}

define void @add_store_i64(ptr %p, i64 %x) optsize {
; CHECK-LABEL: add_store_i64:
; CHECK:       ADD.Q D0, [A0]
; CHECK-NEXT:  RET
entry:
  %v = load i64, ptr %p, align 8
  %s = add i64 %v, %x
  store i64 %s, ptr %p, align 8
  ret void
}

define ptr @lea4_i32_index(ptr %base, i32 %idx) {
; CHECK-LABEL: lea4_i32_index:
; CHECK:       LEA [A0 + D0 * 4],
; CHECK-NOT:   SHL.Q
entry:
  %wide = zext i32 %idx to i64
  %ptr = getelementptr i32, ptr %base, i64 %wide
  ret ptr %ptr
}

define void @store_byte_offset_top_test(ptr %base) minsize optsize {
; CHECK-LABEL: store_byte_offset_top_test:
; CHECK:       REP {{D[0-7]}}, MOV.L {{D[0-7]}}, [A0++]
; CHECK-NOT:   ADD.Q 4
; CHECK-NOT:   DEC.L D1
; CHECK-NOT:   JNE.W
; CHECK:       RET
entry:
  br label %loop

loop:
  %iv = phi i64 [ 0, %entry ], [ %next, %body ]
  %done = icmp eq i64 %iv, 32
  br i1 %done, label %exit, label %body

body:
  %p = getelementptr inbounds i8, ptr %base, i64 %iv
  store i32 0, ptr %p, align 4
  %next = add nuw nsw i64 %iv, 4
  br label %loop

exit:
  ret void
}

define i32 @bitfield_i32_return(i32 signext %a, i32 signext %b) optsize {
; CHECK-LABEL: bitfield_i32_return:
; CHECK:       AND.L -993, D1
; CHECK-NEXT:  AND.L 999, D0
; CHECK-NEXT:  OR.L D1, D0
; CHECK-NEXT:  RET
entry:
  %a.masked = and i32 %a, 999
  %b.masked = and i32 %b, -993
  %or = or i32 %a.masked, %b.masked
  ret i32 %or
}

define i32 @div4_pointer_lea(ptr %base, ptr %limit, i32 signext %n, i64 %byte_bias) {
; CHECK-LABEL: div4_pointer_lea:
; CHECK-NOT:   AND.Q -4
; CHECK:       SAR.Q 2, D1
; CHECK-NEXT:  LEA [A0 + D1 * 4],
; CHECK:       LEA [A0 + D1.L * 4],
; CHECK:       IJT.Q {{D[0-7]}}, {{D[0-7]}},
; CHECK-NOT:   AND.Q -4
entry:
  %cmp20 = icmp sgt i32 %n, 0
  br i1 %cmp20, label %preheader, label %exit

preheader:
  %limit.int = ptrtoint ptr %limit to i64
  %base.int = ptrtoint ptr %base to i64
  %span.bytes = sub i64 %limit.int, %base.int
  %span = ashr exact i64 %span.bytes, 2
  %div = sdiv i64 %byte_bias, 4
  %q = getelementptr inbounds i32, ptr %base, i64 %div
  %idx = zext nneg i32 %n to i64
  %p = getelementptr inbounds nuw i32, ptr %base, i64 %idx
  br label %loop

loop:
  %iv = phi i64 [ 0, %preheader ], [ %iv.next, %loop ]
  %p.cur = phi ptr [ %p, %preheader ], [ %p.next, %loop ]
  %q.cur = phi ptr [ %q, %preheader ], [ %q.next, %loop ]
  %acc = phi i32 [ 0, %preheader ], [ %next.acc, %loop ]
  %pv = load i32, ptr %p.cur, align 4
  %qv = load i32, ptr %q.cur, align 4
  %past = icmp slt i64 %span, %iv
  %bonus = zext i1 %past to i32
  %acc.bonus = add i32 %acc, %bonus
  %acc.p = add i32 %acc.bonus, %pv
  %next.acc = add i32 %acc.p, %qv
  %iv.next = add nuw nsw i64 %iv, 1
  %p.next = getelementptr inbounds nuw i8, ptr %p.cur, i64 4
  %q.next = getelementptr inbounds nuw i8, ptr %q.cur, i64 4
  %done = icmp eq i64 %iv.next, %idx
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i32 [ 0, %entry ], [ %next.acc, %loop ]
  ret i32 %result
}
