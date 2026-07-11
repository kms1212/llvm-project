; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

declare i64 @callee(i64, i64)

define i64 @arith(i64 %a, i64 %b) {
; CHECK-LABEL: arith:
; CHECK: mov.q r0, r2
; CHECK: add.q r1, r2
; CHECK: mul.q r1, r2
; CHECK: xor.q r2, r0
; CHECK: ret
  %add = add i64 %a, %b
  %mul = mul i64 %add, %b
  %xor = xor i64 %mul, %a
  ret i64 %xor
}

define i64 @call2(i64 %a, i64 %b) {
; CHECK-LABEL: call2:
; CHECK: jmp callee
; CHECK-NOT: ret
  %r = tail call i64 @callee(i64 %a, i64 %b)
  ret i64 %r
}

define signext i32 @add_i32(i32 signext %a, i32 signext %b) {
; CHECK-LABEL: add_i32:
; CHECK: add.l
; CHECK: ret
  %r = add nsw i32 %a, %b
  ret i32 %r
}

define i32 @zero_or_n(i32 %n) {
; CHECK-LABEL: zero_or_n:
; CHECK: testjle.l r0, r0,
; CHECK: ret
; CHECK: clr.q r0
; CHECK-NEXT: ret
  %cmp = icmp slt i32 %n, 1
  br i1 %cmp, label %zero, label %ret
ret:
  ret i32 %n
zero:
  ret i32 0
}

define i32 @cmp_imm_i32(i32 %n) {
; CHECK-LABEL: cmp_imm_i32:
; CHECK-NOT: lea.l 10
; CHECK: cmp.l 10, r0
; CHECK: seteq r0
; CHECK: ret
  %cmp = icmp eq i32 %n, 10
  %r = zext i1 %cmp to i32
  ret i32 %r
}

define i64 @cmp_imm_i64(i64 %n) {
; CHECK-LABEL: cmp_imm_i64:
; CHECK-NOT: lea.q 63
; CHECK: cmp.q 63, r0
; CHECK: setult r0
; CHECK: ret
  %cmp = icmp ult i64 %n, 63
  %r = zext i1 %cmp to i64
  ret i64 %r
}

define i32 @one_i32() {
; CHECK-LABEL: one_i32:
; CHECK: set r0
; CHECK-NEXT: ret
  ret i32 1
}

define i64 @one_i64() {
; CHECK-LABEL: one_i64:
; CHECK: set r0
; CHECK-NEXT: ret
  ret i64 1
}
