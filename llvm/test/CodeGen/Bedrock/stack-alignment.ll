; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o

target triple = "bedrock"

declare i64 @callee(i64)
declare void @use(ptr)

; Function bodies keep SP 16-byte aligned.  A single eight-byte save therefore
; uses the adjacent padding register through the compact pair form.
define void @leaf_clobber_r8() {
; CHECK-LABEL: leaf_clobber_r8:
; CHECK: pushp 3
; CHECK: popp 3
; CHECK-NEXT: ret
  call void asm sideeffect "", "~{r8}"()
  ret void
}

; A real adjacent pair still uses the compact pair save and restore.
define void @leaf_clobber_r8_r9() {
; CHECK-LABEL: leaf_clobber_r8_r9:
; CHECK: pushp 3
; CHECK: popp 3
; CHECK-NEXT: ret
  call void asm sideeffect "", "~{r8},~{r9}"()
  ret void
}

; CALL pushes an eight-byte return address, so a caller adjusts SP to 8 modulo
; 16 before the instruction and restores it afterwards.
define i64 @call_alignment(i64 %x) {
; CHECK-LABEL: call_alignment:
; CHECK: sub.q 8, sp
; CHECK-NEXT: call callee
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: inc.q r0
; CHECK-NEXT: ret
  %v = call i64 @callee(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; Size-optimized frameless callers use the same memory-free stack adjustment;
; its eight-byte immediate has a dedicated one-byte encoding.
define i64 @compact_call_alignment(i64 %x) minsize optsize {
; CHECK-LABEL: compact_call_alignment:
; CHECK-NOT: push
; CHECK: sub.q 8, sp
; CHECK-NEXT: call callee
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: inc.q r0
; CHECK-NEXT: ret
  %v = call i64 @callee(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; A compact pad is removed again when the call becomes a tail jump.
define i64 @compact_tail_call(i64 %x) minsize optsize {
; CHECK-LABEL: compact_tail_call:
; CHECK-NOT: push
; CHECK-NOT: pop
; CHECK: jmp callee
  %v = call i64 @callee(i64 %x)
  ret i64 %v
}

; Callee saves keep the body aligned independently of the dynamic near-call
; padding.
define i64 @call_with_single_save(i64 %x) {
; CHECK-LABEL: call_with_single_save:
; CHECK: pushp 3
; CHECK: sub.q 8, sp
; CHECK-NEXT: call callee
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: inc.q r0
; CHECK: popp 3
; CHECK-NEXT: ret
  call void asm sideeffect "", "~{r8}"()
  %v = call i64 @callee(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; Declared 16-byte local alignment is preserved independently of the transient
; eight-byte caller-frame padding.
define i64 @aligned_local(i64 %x) {
; CHECK-LABEL: aligned_local:
; CHECK: sub.q 16, sp
; CHECK-NEXT: mov.q r0, [sp]
; CHECK-NEXT: sub.q 8, sp
; CHECK-NEXT: lea.q [sp + 8], r0
; CHECK-NEXT: call use
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: mov.q [sp], r0
; CHECK-NEXT: add.q 16, sp
; CHECK-NEXT: ret
  %slot = alloca i64, align 16
  store i64 %x, ptr %slot, align 16
  call void @use(ptr %slot)
  %v = load i64, ptr %slot, align 16
  ret i64 %v
}
