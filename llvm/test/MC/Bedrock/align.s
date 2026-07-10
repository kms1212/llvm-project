; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s

nop
.p2align 2
ret

; CHECK: 0: 01
; CHECK-SAME: nop
; CHECK: 1: 01
; CHECK-SAME: nop
; CHECK: 2: 01
; CHECK-SAME: nop
; CHECK: 3: 01
; CHECK-SAME: nop
; CHECK: 4: 02
; CHECK-SAME: ret
