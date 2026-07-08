; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

declare i64 @callee(i64, i64)

define i64 @arith(i64 %a, i64 %b) {
; CHECK-LABEL: arith:
; CHECK: mov.q r0, r2
; CHECK: add.q r1, r2
; CHECK: mul.q r1, r2
; CHECK: xor.q r0, r2
; CHECK: mov.q r2, r0
; CHECK: ret
  %add = add i64 %a, %b
  %mul = mul i64 %add, %b
  %xor = xor i64 %mul, %a
  ret i64 %xor
}

define i64 @call2(i64 %a, i64 %b) {
; CHECK-LABEL: call2:
; CHECK: call callee
; CHECK: ret
  %r = call i64 @callee(i64 %a, i64 %b)
  ret i64 %r
}

define signext i32 @add_i32(i32 signext %a, i32 signext %b) {
; CHECK-LABEL: add_i32:
; CHECK: add.l
; CHECK: ret
  %r = add nsw i32 %a, %b
  ret i32 %r
}
