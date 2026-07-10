; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)
declare i32 @llvm.smin.i32(i32, i32)
declare i32 @llvm.umax.i32(i32, i32)
declare i32 @llvm.umin.i32(i32, i32)
declare i64 @llvm.umin.i64(i64, i64)

define i32 @smax_rr(i32 %a, i32 %b) {
; CHECK-LABEL: smax_rr:
; CHECK: maxs.l r1, r0
; CHECK: ret
  %r = call i32 @llvm.smax.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @smin_rr(i32 %a, i32 %b) {
; CHECK-LABEL: smin_rr:
; CHECK: mins.l r1, r0
; CHECK: ret
  %r = call i32 @llvm.smin.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @umax_rr(i32 %a, i32 %b) {
; CHECK-LABEL: umax_rr:
; CHECK: maxu.l r1, r0
; CHECK: ret
  %r = call i32 @llvm.umax.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @umin_rr(i32 %a, i32 %b) {
; CHECK-LABEL: umin_rr:
; CHECK: minu.l r1, r0
; CHECK: ret
  %r = call i32 @llvm.umin.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @smax_zero(i32 %a) {
; CHECK-LABEL: smax_zero:
; CHECK: maxs.l 0, r0
; CHECK: ret
  %r = call i32 @llvm.smax.i32(i32 %a, i32 0)
  ret i32 %r
}

define i32 @smax_zero_lhs(i32 %a) {
; CHECK-LABEL: smax_zero_lhs:
; CHECK: maxs.l 0, r0
; CHECK: ret
  %r = call i32 @llvm.smax.i32(i32 0, i32 %a)
  ret i32 %r
}

define i32 @smin_zero(i32 %a) {
; CHECK-LABEL: smin_zero:
; CHECK: mins.l 0, r0
; CHECK: ret
  %r = call i32 @llvm.smin.i32(i32 %a, i32 0)
  ret i32 %r
}

define i32 @umax_imm(i32 %a) {
; CHECK-LABEL: umax_imm:
; CHECK: maxu.l 1, r0
; CHECK: ret
  %r = call i32 @llvm.umax.i32(i32 %a, i32 1)
  ret i32 %r
}

define i64 @umin_imm64(i64 %a) {
; CHECK-LABEL: umin_imm64:
; CHECK: minu.q 127, r0
; CHECK: ret
  %r = call i64 @llvm.umin.i64(i64 %a, i64 127)
  ret i64 %r
}

define i32 @select_umax_imm(i32 %a) {
; CHECK-LABEL: select_umax_imm:
; CHECK: maxu.l 1, r0
; CHECK: ret
  %c = icmp ult i32 %a, 1
  %r = select i1 %c, i32 1, i32 %a
  ret i32 %r
}
