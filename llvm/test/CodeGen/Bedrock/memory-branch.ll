; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

declare void @use(ptr)

define i64 @stack_slot(i64 %x) {
; CHECK-LABEL: stack_slot:
; CHECK: sub.q 16, sp
; CHECK: mov.q r0, [sp + 8]
; CHECK: lea.q [sp + 8], r0
; CHECK: call use
; CHECK: mov.q [sp + 8], r0
; CHECK: add.q 16, sp
; CHECK: ret
  %slot = alloca i64, align 8
  store i64 %x, ptr %slot, align 8
  call void @use(ptr %slot)
  %v = load i64, ptr %slot, align 8
  ret i64 %v
}

define i64 @narrow(ptr %p) {
; CHECK-LABEL: narrow:
; CHECK: extsq.w [r2], r1
; CHECK: extzq.b [r0], r0
; CHECK: add.q r1, r0
; CHECK: ret
  %b = load i8, ptr %p, align 1
  %z = zext i8 %b to i64
  %q = getelementptr i8, ptr %p, i64 2
  %w = load i16, ptr %q, align 2
  %s = sext i16 %w to i64
  %r = add i64 %z, %s
  ret i64 %r
}

define i64 @branch(i64 %a, i64 %b) {
; CHECK-LABEL: branch:
; CHECK: cmp.q r1, r0
; CHECK: j.uge .LBB
; CHECK: jmp .LBB
; CHECK: lea.q 1, r0
; CHECK: ret
; CHECK: lea.q 2, r0
; CHECK: ret
entry:
  %c = icmp ult i64 %a, %b
  br i1 %c, label %lt, label %ge
lt:
  ret i64 1
ge:
  ret i64 2
}

define i64 @setcc(i64 %a, i64 %b) {
; CHECK-LABEL: setcc:
; CHECK: cmp.q r1, r0
; CHECK: set.eq r0
; CHECK: ret
  %c = icmp eq i64 %a, %b
  %z = zext i1 %c to i64
  ret i64 %z
}
