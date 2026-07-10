; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

define i32 @zext8(i32 %a) {
; CHECK-LABEL: zext8:
; CHECK: extzl.b r0, r0
; CHECK: ret
  %r = and i32 %a, 255
  ret i32 %r
}

define i32 @zext16(i32 %a) {
; CHECK-LABEL: zext16:
; CHECK: extzl.w r0, r0
; CHECK: ret
  %r = and i32 %a, 65535
  ret i32 %r
}

define i64 @zext8q(i64 %a) {
; CHECK-LABEL: zext8q:
; CHECK: extzq.b r0, r0
; CHECK: ret
  %r = and i64 %a, 255
  ret i64 %r
}

define i64 @zext16q(i64 %a) {
; CHECK-LABEL: zext16q:
; CHECK: extzq.w r0, r0
; CHECK: ret
  %r = and i64 %a, 65535
  ret i64 %r
}

define i32 @sext8(i32 %a) {
; CHECK-LABEL: sext8:
; CHECK: extsl.b r0, r0
; CHECK: ret
  %t = shl i32 %a, 24
  %r = ashr i32 %t, 24
  ret i32 %r
}

define i32 @sext16(i32 %a) {
; CHECK-LABEL: sext16:
; CHECK: extsl.w r0, r0
; CHECK: ret
  %t = shl i32 %a, 16
  %r = ashr i32 %t, 16
  ret i32 %r
}

define i64 @sext8q(i64 %a) {
; CHECK-LABEL: sext8q:
; CHECK: extsq.b r0, r0
; CHECK: ret
  %t = shl i64 %a, 56
  %r = ashr i64 %t, 56
  ret i64 %r
}

define i64 @sext16q(i64 %a) {
; CHECK-LABEL: sext16q:
; CHECK: extsq.w r0, r0
; CHECK: ret
  %t = shl i64 %a, 48
  %r = ashr i64 %t, 48
  ret i64 %r
}
