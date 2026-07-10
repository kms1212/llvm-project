; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

define i32 @add_imm(i32 %a) {
; CHECK-LABEL: add_imm:
; CHECK: add.l 5, r0
; CHECK: ret
  %r = add i32 %a, 5
  ret i32 %r
}

define i32 @add_lhs_imm(i32 %a) {
; CHECK-LABEL: add_lhs_imm:
; CHECK: add.l 5, r0
; CHECK: ret
  %r = add i32 5, %a
  ret i32 %r
}

define i32 @sub_imm(i32 %a) {
; CHECK-LABEL: sub_imm:
; CHECK: add.l -5, r0
; CHECK: ret
  %r = sub i32 %a, 5
  ret i32 %r
}

define i32 @and_imm(i32 %a) {
; CHECK-LABEL: and_imm:
; CHECK: and.l 999, r0
; CHECK: ret
  %r = and i32 %a, 999
  ret i32 %r
}

define i32 @or_imm(i32 %a) {
; CHECK-LABEL: or_imm:
; CHECK: or.l 5, r0
; CHECK: ret
  %r = or i32 %a, 5
  ret i32 %r
}

define i64 @or_single_bit_i64(i64 %a) {
; CHECK-LABEL: or_single_bit_i64:
; CHECK-NOT: or.q
; CHECK: bset 32, r0
; CHECK: ret
  %r = or i64 %a, 4294967296
  ret i64 %r
}

define i64 @or_two_large_bits_i64(i64 %a) {
; CHECK-LABEL: or_two_large_bits_i64:
; CHECK-NOT: or.q
; CHECK: bset 34, r0
; CHECK: bset 35, r0
; CHECK: ret
  %r = or i64 %a, 51539607552
  ret i64 %r
}

define i64 @or_two_small_bits_i64(i64 %a) {
; CHECK-LABEL: or_two_small_bits_i64:
; CHECK: or.q 3, r0
; CHECK: ret
  %r = or i64 %a, 3
  ret i64 %r
}

define i32 @xor_imm(i32 %a) {
; CHECK-LABEL: xor_imm:
; CHECK: xor.l 5, r0
; CHECK: ret
  %r = xor i32 %a, 5
  ret i32 %r
}

define i32 @shl_imm(i32 %a) {
; CHECK-LABEL: shl_imm:
; CHECK: shl.l 2, r0
; CHECK: ret
  %r = shl i32 %a, 2
  ret i32 %r
}

define i32 @srl_imm(i32 %a) {
; CHECK-LABEL: srl_imm:
; CHECK: shr.l 3, r0
; CHECK: ret
  %r = lshr i32 %a, 3
  ret i32 %r
}

define i32 @sra_imm(i32 %a) {
; CHECK-LABEL: sra_imm:
; CHECK: sar.l 4, r0
; CHECK: ret
  %r = ashr i32 %a, 4
  ret i32 %r
}
