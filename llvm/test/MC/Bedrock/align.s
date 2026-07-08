; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s

nop
.p2align 2
ret

; CHECK: 0: 20 40
; CHECK-SAME: nop
; CHECK: 2: 20 40
; CHECK-SAME: nop
; CHECK: 4: 20 41
; CHECK-SAME: ret
