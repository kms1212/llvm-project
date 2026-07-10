; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

define i32 @select_const_cmp(i32 %x) {
; CHECK-LABEL: select_const_cmp:
; CHECK: cmp.l
; CHECK-NEXT: jult
; CHECK-NOT: setult
; CHECK-NOT: neg.l
; CHECK: ret
  %a = add i32 %x, 55
  %b = or i32 %x, 48
  %c = icmp ult i32 %x, 10
  %r = select i1 %c, i32 %b, i32 %a
  ret i32 %r
}
