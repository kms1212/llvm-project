; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o

target triple = "bedrock"

declare i64 @callee(i64)
declare void @clobber()
declare void @use(ptr)

; A dynamic allocation with the ABI's maximum baseline object alignment must
; realign after subtracting the runtime size. The generic expansion only
; rounds that size to the eight-byte internal stack quantum.
define void @dynamic_aligned_alloca(i64 %size) {
; CHECK-LABEL: dynamic_aligned_alloca:
; CHECK: sub.q [[SIZE:r[0-9]+]], [[PTR:r[0-9]+]]
; CHECK-NEXT: and.q -16, [[PTR]]
; CHECK-NEXT: mov.q [[PTR]], sp
  %slot = alloca i8, i64 %size, align 16
  call void @use(ptr %slot)
  ret void
}

; Even a dynamically sized object that only requests eight-byte alignment must
; leave the body SP 16-byte aligned so the fixed near-call phase adjustment
; still establishes SP mod 16 = 8 immediately before CALL.
define void @dynamic_call_alignment(i64 %size) {
; CHECK-LABEL: dynamic_call_alignment:
; CHECK: sub.q [[SIZE:r[0-9]+]], [[PTR:r[0-9]+]]
; CHECK-NEXT: and.q -16, [[PTR]]
; CHECK-NEXT: mov.q [[PTR]], sp
; CHECK: sub.q 8, sp
; CHECK-NEXT: call use
  %slot = alloca i8, i64 %size, align 8
  call void @use(ptr %slot)
  ret void
}

; R14 holds the stable base for a realigned frame. Keep it out of the general
; allocation pool even under enough call-crossing pressure to otherwise use
; every callee-saved GPR.
define i64 @realigned_base_reserved(i64 %a, i64 %b, i64 %c, i64 %d,
                                    i64 %e, i64 %f, i64 %g) {
; CHECK-LABEL: realigned_base_reserved:
; CHECK: mov.q sp, r14
; CHECK-NEXT: and.q -64, r14
; CHECK-NEXT: mov.q r14, sp
; CHECK-NOT: mov.q {{.*}}, r14
; CHECK: call clobber
; CHECK-NOT: mov.q {{.*}}, r14
; CHECK: lea.q [r14{{( \+ [0-9]+)?}}], r0
  %slot = alloca i64, align 64
  call void @clobber()
  store i64 %a, ptr %slot, align 64
  call void @use(ptr %slot)
  %s0 = add i64 %a, %b
  %s1 = add i64 %s0, %c
  %s2 = add i64 %s1, %d
  %s3 = add i64 %s2, %e
  %s4 = add i64 %s3, %f
  %s5 = add i64 %s4, %g
  ret i64 %s5
}

; Incoming stack arguments remain relative to the entry SP. R15 preserves the
; pre-realignment body SP, while R14 may be rounded down by as much as 48 bytes
; and is only a stable base for aligned local objects.
define i64 @realigned_stack_argument(i64 %a0, i64 %a1, i64 %a2, i64 %a3,
                                     i64 %a4, i64 %a5, i64 %a6, i64 %a7,
                                     i64 %a8) {
; CHECK-LABEL: realigned_stack_argument:
; CHECK: mov.q sp, r15
; CHECK: mov.q sp, r14
; CHECK-NEXT: and.q -64, r14
; CHECK-NEXT: mov.q r14, sp
; CHECK: mov.q [r15 + 80], [[ARG:r[0-9]+]]
; CHECK-NEXT: mov.q [[ARG]], [r14]
; CHECK: lea.q [r14], r0
  %slot = alloca i64, align 64
  store i64 %a8, ptr %slot, align 64
  call void @use(ptr %slot)
  ret i64 %a8
}

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

; CALL pushes an eight-byte return address, so a fixed-frame caller reserves
; its eight-byte phase adjustment on entry and restores it in the epilogue.
define i64 @call_alignment(i64 %x) {
; CHECK-LABEL: call_alignment:
; CHECK: sub.q 8, sp
; CHECK-NEXT: call callee
; CHECK-NEXT: inc.q r0
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: ret
  %v = call i64 @callee(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; Size-optimized frameless callers reserve the same compact eight-byte phase.
define i64 @compact_call_alignment(i64 %x) minsize optsize {
; CHECK-LABEL: compact_call_alignment:
; CHECK-NOT: push
; CHECK: sub.q 8, sp
; CHECK-NEXT: call callee
; CHECK-NEXT: inc.q r0
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: ret
  %v = call i64 @callee(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; Multiple calls reuse one reserved call frame instead of adjusting SP around
; every call site.
define void @reused_call_alignment() {
; CHECK-LABEL: reused_call_alignment:
; CHECK: sub.q 8, sp
; CHECK-NEXT: call clobber
; CHECK-NEXT: call clobber
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: ret
  call void @clobber()
  call void @clobber()
  ret void
}

; A compact pad is removed again when the call becomes a tail jump.
define i64 @compact_tail_call(i64 %x) minsize optsize {
; CHECK-LABEL: compact_tail_call:
; CHECK-NOT: push
; CHECK-NOT: pop
; CHECK: jmp callee
  %v = tail call i64 @callee(i64 %x)
  ret i64 %v
}

; A fixed outgoing frame is reserved below the callee-save area.
define i64 @call_with_single_save(i64 %x) {
; CHECK-LABEL: call_with_single_save:
; CHECK: push r8
; CHECK-NEXT: sub.q 16, sp
; CHECK: call callee
; CHECK-NEXT: inc.q r0
; CHECK-NEXT: add.q 16, sp
; CHECK-NEXT: pop r8
; CHECK-NEXT: ret
  call void asm sideeffect "", "~{r8}"()
  %v = call i64 @callee(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; Declared 16-byte local alignment is preserved above the reserved outgoing
; frame.
define i64 @aligned_local(i64 %x) {
; CHECK-LABEL: aligned_local:
; CHECK: sub.q 24, sp
; CHECK-NEXT: mov.q r0, [sp + 8]
; CHECK-NEXT: lea.q [sp + 8], r0
; CHECK-NEXT: call use
; CHECK-NEXT: mov.q [sp + 8], r0
; CHECK-NEXT: add.q 24, sp
; CHECK-NEXT: ret
  %slot = alloca i64, align 16
  store i64 %x, ptr %slot, align 16
  call void @use(ptr %slot)
  %v = load i64, ptr %slot, align 16
  ret i64 %v
}
