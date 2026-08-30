; REQUIRES: bedrock-registered-target
; RUN: not llvm-mc -triple=bedrock %s 2>&1 | FileCheck %s

j.eq -2
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: j.eq -2

set.eq r1
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: set.eq r1

call.eq -4
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: call.eq -4

trap.eq
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: trap.eq

rep.eq r0, nop
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: rep.eq r0, nop

rept r0, (nop)
; CHECK: error: expected operand
; CHECK-NEXT: rept r0, (nop)

dj.eq.l r1, [r2]
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: dj.eq.l r1, [r2]

djeq.l r1, [r2]
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: djeq.l r1, [r2]

djf r1, [r2]
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: djf r1, [r2]

pushm 3
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: pushm 3

popm 3
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: popm 3

sum.q 255, r2
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: sum.q 255, r2
