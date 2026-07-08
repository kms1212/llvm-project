; RUN: llc -mtriple=bedrock -stop-after=finalize-isel -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=bedrock -verify-machineinstrs %s -o /dev/null

target triple = "bedrock"

define i32 @load_i32_idx(ptr %p, i64 %i) {
; CHECK-LABEL: name: load_i32_idx
; CHECK:       MOV32idx4rm
; CHECK-NOT:   SHL64ri
; CHECK-NOT:   ADD64rr
entry:
  %q = getelementptr i32, ptr %p, i64 %i
  %v = load i32, ptr %q, align 4
  ret i32 %v
}

define void @store_i32_idx(ptr %p, i64 %i, i32 %v) {
; CHECK-LABEL: name: store_i32_idx
; CHECK:       MOV32idx4mr
; CHECK-NOT:   SHL64ri
; CHECK-NOT:   ADD64rr
entry:
  %q = getelementptr i32, ptr %p, i64 %i
  store i32 %v, ptr %q, align 4
  ret void
}

define i32 @load_stack_i32_idx(i64 %i) {
; CHECK-LABEL: name: load_stack_i32_idx
; CHECK:       MOV32idx4rm %stack.0.arr, {{%[0-9]+}}, 0
entry:
  %arr = alloca [8 x i32], align 16
  %q = getelementptr [8 x i32], ptr %arr, i64 0, i64 %i
  store volatile i32 7, ptr %q, align 4
  %v = load volatile i32, ptr %q, align 4
  ret i32 %v
}
