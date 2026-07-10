; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

define i32 @load_off(ptr %p) {
; CHECK-LABEL: load_off:
; CHECK: mov.l [r0 + 4], r0
; CHECK: ret
  %q = getelementptr i8, ptr %p, i64 4
  %v = load i32, ptr %q, align 4
  ret i32 %v
}

define i64 @sextload_off(ptr %p) {
; CHECK-LABEL: sextload_off:
; CHECK: extsq.w [r0 - 2], r0
; CHECK: ret
  %q = getelementptr i8, ptr %p, i64 -2
  %v = load i16, ptr %q, align 2
  %s = sext i16 %v to i64
  ret i64 %s
}

define void @store_off(ptr %p, i32 %v) {
; CHECK-LABEL: store_off:
; CHECK: mov.l r1, [r0 + 8]
; CHECK: ret
  %q = getelementptr i8, ptr %p, i64 8
  store i32 %v, ptr %q, align 4
  ret void
}
