; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

declare void @use(ptr)

define i64 @stack_slot(i64 %x) {
; CHECK-LABEL: stack_slot:
; CHECK: sub.q 16, sp
; CHECK-NEXT: mov.q r0, [sp + 8]
; CHECK-NEXT: sub.q 8, sp
; CHECK-NEXT: lea.q [sp + 16], r0
; CHECK-NEXT: call use
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: mov.q [sp + 8], r0
; CHECK-NEXT: add.q 16, sp
; CHECK-NEXT: ret
  %slot = alloca i64, align 8
  store i64 %x, ptr %slot, align 8
  call void @use(ptr %slot)
  %v = load i64, ptr %slot, align 8
  ret i64 %v
}

define i64 @narrow(ptr %p) {
; CHECK-LABEL: narrow:
; CHECK: extsq.w [r0 + 2], r1
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
; CHECK: cmpjuge.q r1, r0,
; CHECK: set r0
; CHECK: ret
; CHECK: lea.q 2, r0
; CHECK: ret
; OBJ-LABEL: <branch>:
; OBJ: cb c5 a8 80 03{{[ \t]+}}cmpjuge.q{{[ \t]+}}r1, r0, 3
; OBJ-NOT: d0 66
; OBJ-LABEL: <setcc>:
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
; CHECK: seteq r0
; CHECK: ret
  %c = icmp eq i64 %a, %b
  %z = zext i1 %c to i64
  ret i64 %z
}

define i64 @medium_branch(i64 %a, ptr %p) {
; CHECK-LABEL: medium_branch:
; CHECK: pushp 3
; CHECK: mov.q r1, r8
; CHECK: testjeq.q r0, r0, .LBB
; CHECK: popp 3
; CHECK: ret
; OBJ-LABEL: <medium_branch>:
; OBJ: cf c5 90 30 df 00{{[ \t]+}}testjeq.q{{[ \t]+}}r0, r0, 223
; OBJ-NOT: d0 66
  %c = icmp eq i64 %a, 0
  br i1 %c, label %far, label %body

body:
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  call void @use(ptr %p)
  ret i64 0

far:
  ret i64 1
}
