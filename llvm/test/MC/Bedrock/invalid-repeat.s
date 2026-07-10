; REQUIRES: bedrock-registered-target
; RUN: not llvm-mc -triple=bedrock %s 2>&1 | FileCheck %s

repg r1, 0
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: repg r1, 0

repgf r1, 2
; CHECK: error: expected '{' before grouped repeat body
; CHECK-NEXT: repgf r1, 2

repgf r3, {
  syscall
}
; CHECK: error: invalid grouped repeat body
; CHECK-NEXT: repgf r3, {

repgf r3, {
  mov.q r2, r3
}
; CHECK: error: invalid grouped repeat body
; CHECK-NEXT: repgf r3, {

repgf r3, {
  seglea.q [ds:r2 + r4], r3
}
; CHECK: error: invalid grouped repeat body
; CHECK-NEXT: repgf r3, {

nontemporal, mov.q r1, r2
; CHECK: error: expected operand
; CHECK-NEXT: nontemporal, mov.q r1, r2
