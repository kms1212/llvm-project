; REQUIRES: bedrock-registered-target
; RUN: not llvm-mc -triple=bedrock -filetype=obj %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s

iret
; CHECK: error: invalid instruction mnemonic

encinst [r2]
; CHECK: error: invalid instruction mnemonic

pop cs
; CHECK: error: invalid operand for instruction

wrseg r0, cs
; CHECK: error: invalid instruction mnemonic

jmp r1
; CHECK: error: invalid operand for instruction

jmp.w r1
; CHECK: error: invalid instruction mnemonic

fmovcr.q 0, f0
; CHECK: error: invalid instruction mnemonic

facosa.s f0, f1
; CHECK: error: instruction requires the +fptransa feature
