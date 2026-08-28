; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O0 %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llvm-mc -triple=bedrock -filetype=obj %t.s -o /dev/null

@r1 = global i64 7

define i64 @sp() {
; CHECK: .globl `sp`
; CHECK: .type `sp`,@function
; CHECK: `sp`:
; CHECK: [pc + `r1`]
  %value = load i64, ptr @r1
  ret i64 %value
}
