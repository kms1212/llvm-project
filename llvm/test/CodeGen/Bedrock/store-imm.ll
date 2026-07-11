; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

declare void @use(ptr)
declare void @clobber()
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)

@g64 = global i64 0
@g64_pair = global [2 x i64] zeroinitializer
@g64_triple = global [3 x i64] zeroinitializer

define void @store_imm_reg(ptr %p) {
; CHECK-LABEL: store_imm_reg:
; CHECK: mov.l 7, [r0]
; CHECK: ret
  store i32 7, ptr %p, align 4
  ret void
}

define void @store_imm_offset(ptr %p) {
; CHECK-LABEL: store_imm_offset:
; CHECK: mov.q 5, [r0 + 8]
; CHECK: ret
  %q = getelementptr i8, ptr %p, i64 8
  store i64 5, ptr %q, align 8
  ret void
}

define void @store_imm_abs() {
; CHECK-LABEL: store_imm_abs:
; CHECK: mov.q 7, [g64]
; CHECK: ret
  store i64 7, ptr @g64, align 8
  ret void
}

define void @store_imm_abs_offset() {
; CHECK-LABEL: store_imm_abs_offset:
; CHECK: mov.q 2, [g64_pair+8]
; CHECK: ret
  store i64 2, ptr getelementptr inbounds ([2 x i64], ptr @g64_pair, i64 0, i64 1), align 8
  ret void
}

define void @store_imm_stack() {
; CHECK-LABEL: store_imm_stack:
; CHECK: mov.q 5, [sp
; CHECK: call use
; CHECK: ret
  %slot = alloca i64, align 8
  store i64 5, ptr %slot, align 8
  call void @use(ptr %slot)
  ret void
}

define void @store_zero(ptr %p) {
; CHECK-LABEL: store_zero:
; CHECK: clr.l [r0]
; CHECK: ret
  store i32 0, ptr %p, align 4
  ret void
}

define void @store_zero_abs() {
; CHECK-LABEL: store_zero_abs:
; CHECK: clr.q [g64]
; CHECK: ret
  store i64 0, ptr @g64, align 8
  ret void
}

define void @zero_memset_before_call() {
; CHECK-LABEL: zero_memset_before_call:
; CHECK: lea.q g64_triple+16, r6
; CHECK-NEXT: clr.q [r6]
; CHECK-NEXT: clr.q [r6 - 8]
; CHECK-NEXT: clr.q [r6 - 16]
; CHECK-NEXT: jmp clobber
  call void @llvm.memset.p0.i64(ptr @g64_triple, i8 0, i64 24, i1 false)
  tail call void @clobber()
  ret void
}
