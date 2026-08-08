; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

declare void @sink5(i64, i64, i64, i64, i64)

define i64 @zero_arg_and_return(i64 %a, i64 %b, i64 %c) minsize optsize {
; CHECK-LABEL: zero_arg_and_return:
; CHECK: sub.q 8, sp
; CHECK: lea.q 8, r3
; CHECK-NEXT: clr.q r4
; CHECK-NEXT: call sink5
; CHECK-NEXT: clr.q r0
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: ret
entry:
  call void @sink5(i64 %a, i64 %b, i64 %c, i64 8, i64 0)
  ret i64 0
}

define i64 @one_arg_and_return(i64 %a, i64 %b, i64 %c) minsize optsize {
; CHECK-LABEL: one_arg_and_return:
; CHECK: sub.q 8, sp
; CHECK: lea.q 8, r3
; CHECK-NEXT: set r4
; CHECK-NEXT: call sink5
; CHECK-NEXT: set r0
; CHECK-NEXT: add.q 8, sp
; CHECK-NEXT: ret
entry:
  call void @sink5(i64 %a, i64 %b, i64 %c, i64 8, i64 1)
  ret i64 1
}
