; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O1
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O2
; RUN: llc -mtriple=bedrock -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0

define i8 @pop8(i8 %x) {
; O1-LABEL: pop8:
; O1:       POPCNT.B
entry:
  %r = call i8 @llvm.ctpop.i8(i8 %x)
  ret i8 %r
}

define i16 @clz16(i16 %x) {
; O1-LABEL: clz16:
; O1:       CLZ.W
entry:
  %r = call i16 @llvm.ctlz.i16(i16 %x, i1 false)
  ret i16 %r
}

define i32 @ctz32(i32 %x) {
; O1-LABEL: ctz32:
; O1:       CTZ.L
entry:
  %r = call i32 @llvm.cttz.i32(i32 %x, i1 false)
  ret i32 %r
}

define i64 @clmul64(i64 %x, i64 %y) {
; O1-LABEL: clmul64:
; O1:       CLMUL.Q D1, D0
entry:
  %r = call i64 @llvm.clmul.i64(i64 %x, i64 %y)
  ret i64 %r
}

define i64 @cls64(i64 %x) {
; O1-LABEL: cls64:
; O1:       CLS.Q
entry:
  %n = xor i64 %x, -1
  %r = call i64 @llvm.ctlz.i64(i64 %n, i1 false)
  ret i64 %r
}

define i64 @cts64(i64 %x) {
; O1-LABEL: cts64:
; O1:       CTS.Q
entry:
  %n = xor i64 %x, -1
  %r = call i64 @llvm.cttz.i64(i64 %n, i1 false)
  ret i64 %r
}

define i64 @bset64(i64 %x) {
; O1-LABEL: bset64:
; O1:       BSET.Q 3, D0
; O0-LABEL: bset64:
; O0:       OR.Q 8, D0
entry:
  %r = or i64 %x, 8
  ret i64 %r
}

define i64 @bclr64(i64 %x) {
; O1-LABEL: bclr64:
; O1:       BCLR.Q 3, D0
entry:
  %r = and i64 %x, -9
  ret i64 %r
}

define i64 @bchg64(i64 %x) {
; O1-LABEL: bchg64:
; O1:       BCHG.Q 3, D0
entry:
  %r = xor i64 %x, 8
  ret i64 %r
}

define void @btest64(i64 %x, ptr %p) {
; O1-LABEL: btest64:
; O1:       BTEST.Q 3, D0
; O0-LABEL: btest64:
; O0-NOT:   BTEST.Q
entry:
  %m = and i64 %x, 8
  %c = icmp ne i64 %m, 0
  br i1 %c, label %t, label %f

t:
  store i64 1, ptr %p
  ret void

f:
  store i64 0, ptr %p
  ret void
}

define void @set_mem(ptr %p) {
; O2-LABEL: set_mem:
; O2:       BSET.Q 3, [A0]
entry:
  %v = load i64, ptr %p
  %r = or i64 %v, 8
  store i64 %r, ptr %p
  ret void
}

define void @clr_mem(ptr %p) {
; O2-LABEL: clr_mem:
; O2:       BCLR.Q 3, [A0]
entry:
  %v = load i64, ptr %p
  %r = and i64 %v, -9
  store i64 %r, ptr %p
  ret void
}

define void @chg_mem(ptr %p) {
; O2-LABEL: chg_mem:
; O2:       BCHG.Q 3, [A0]
entry:
  %v = load i64, ptr %p
  %r = xor i64 %v, 8
  store i64 %r, ptr %p
  ret void
}

declare i8 @llvm.ctpop.i8(i8)
declare i16 @llvm.ctlz.i16(i16, i1 immarg)
declare i32 @llvm.cttz.i32(i32, i1 immarg)
declare i64 @llvm.ctlz.i64(i64, i1 immarg)
declare i64 @llvm.cttz.i64(i64, i1 immarg)
declare i64 @llvm.clmul.i64(i64, i64)
