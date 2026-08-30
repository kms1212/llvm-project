; REQUIRES: bedrock-registered-target
; RUN: not llvm-mc -triple=bedrock -filetype=obj %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s
; RUN: echo '[0xc3,0xb9,0x02]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=OLD-CLRF
; RUN: echo '[0x1f,0x65,0x02,0x80,0x00]' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefix=OLD-FPU-CONVERT
; RUN: echo '[0x1f,0x67,0x02,0x00]' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefix=OLD-FPU-ARITH
; RUN: echo '[0xcb,0xf3,0xc0,0x98,0x82]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=OLD-MOVCC-STORE
; RUN: echo '[0xcb,0xf3,0x80,0x10,0x82]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=OLD-MOVCC-LOAD
; RUN: echo '[0xcb,0xf6,0xc2,0x09,0x82]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=OLD-CMPXCHG
; RUN: echo '[0xcf,0xf1,0x1d,0x98,0xdb,0x00]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=MOVCC-IMMEDIATE-DEST
; RUN: echo '[0xc7,0xd1,0x01,0x12]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DIRECT-DJ-FALSE
; RUN: echo '[0xc3,0xbe,0x10,0x01]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DIRECT-REP-FALSE
; RUN: echo '[0xcb,0xf0,0x20,0x11,0x23]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DIRECT-IJ-FALSE
; RUN: echo '[0xcb,0xce,0xc8,0xdb,0x00]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=MOVUC-IMMEDIATE-DEST
; RUN: echo '[0xc7,0x80,0x5b,0x00]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=IMMEDIATE-DEST
; RUN: echo '[0xc7,0xa1,0x5b,0x00]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=RECLAIMED-IMMEDIATE
; RUN: echo '[0xcf,0x81,0x10,0x00,0x00,0x00]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=WIDE-IMMEDIATE
; RUN: echo '[0xa5,0x00]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=RECLAIMED-SUB-ZERO
; RUN: echo '[0xc7,0xe4,0xf1,0xd8]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=FEA-SCALAR-SP
; RUN: echo '[0xcb,0xe4,0x12,0x5b,0x01]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=FEA-SCALAR-IMM8
; RUN: echo '[0xcf,0xe4,0x12,0x5c,0x01,0x00]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=FEA-SCALAR-IMM16
; RUN: echo '[0xd7,0xe4,0x71,0xdd,0x00,0x00,0x80,0x3f]' \
; RUN:   | not llvm-mc -triple=bedrock -disassemble -show-encoding 2>&1 \
; RUN:   | FileCheck %s --check-prefix=FEA-IMMEDIATE-DEST

; OLD-CLRF: warning: invalid instruction encoding
; OLD-FPU-CONVERT: popp	7{{.*}}encoding: [0x1f]
; OLD-FPU-CONVERT-NEXT: mov.q	sp, r5{{.*}}encoding: [0x65]
; OLD-FPU-CONVERT-NEXT: ret{{.*}}encoding: [0x02]
; OLD-FPU-CONVERT-NEXT: mov.l	r0, r0{{.*}}encoding: [0x80,0x00]
; OLD-FPU-ARITH: popp	7{{.*}}encoding: [0x1f]
; OLD-FPU-ARITH-NEXT: mov.q	sp, r7{{.*}}encoding: [0x67]
; OLD-FPU-ARITH-NEXT: ret{{.*}}encoding: [0x02]
; OLD-FPU-ARITH-NEXT: illegal{{.*}}encoding: [0x00]
; OLD-MOVCC-STORE: warning: invalid instruction encoding
; OLD-MOVCC-LOAD: warning: invalid instruction encoding
; OLD-CMPXCHG: warning: invalid instruction encoding
; MOVCC-IMMEDIATE-DEST: warning: invalid instruction encoding
; DIRECT-DJ-FALSE: warning: invalid instruction encoding
; DIRECT-REP-FALSE: warning: invalid instruction encoding
; DIRECT-IJ-FALSE: warning: invalid instruction encoding
; MOVUC-IMMEDIATE-DEST: warning: invalid instruction encoding
; IMMEDIATE-DEST: warning: invalid instruction encoding
; RECLAIMED-IMMEDIATE: warning: invalid instruction encoding
; WIDE-IMMEDIATE: warning: invalid instruction encoding
; RECLAIMED-SUB-ZERO: warning: invalid instruction encoding
; FEA-SCALAR-SP: warning: invalid instruction encoding
; FEA-SCALAR-IMM8: warning: invalid instruction encoding
; FEA-SCALAR-IMM16: warning: invalid instruction encoding
; FEA-IMMEDIATE-DEST: warning: invalid instruction encoding

iret
; CHECK: error: invalid instruction mnemonic

encinst [r2]
; CHECK: error: invalid instruction mnemonic

clrf 2
; CHECK: error: invalid instruction mnemonic

clc
; CHECK: error: invalid instruction mnemonic

stc
; CHECK: error: invalid instruction mnemonic

djf r1, r2
; CHECK: error: invalid instruction mnemonic

ijf r1, r2, r3
; CHECK: error: invalid operand for instruction

rep r0, (ret)
; CHECK: error: instruction is not eligible as a repeat body

repeq r0, (movt.q r1, r2)
; CHECK: error: instruction is not eligible as a repeat body

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

fmov.s f3, 1
; CHECK: error: invalid instruction mnemonic

facosa.s f0, f1
; CHECK: error: instruction requires the +fptransa feature

cpuid r1 : LEN 2
; CHECK: error: instruction length must be between 3 and 18

cpuid r1 : LEN 19
; CHECK: error: instruction length must be between 3 and 18

bndsii.q r1, r2, r3 : LEN 3
; CHECK: error: LEN requires an extended instruction and must cover its required length

ret : LEN 3
; CHECK: error: LEN requires an extended instruction and must cover its required length

jmp 1 : LEN 3
; CHECK: error: LEN requires an extended instruction and must cover its required length

cpuid r1 : LEN 6, NOP
; CHECK: error: explicit LEN padding byte count must equal requested length minus required instruction length

cpuid r1 : LEN 6, 256, ILLEGAL, NOP
; CHECK: error: padding byte must be ILLEGAL, NOP, or an integer from 0 through 255

cpuid r1 : LEN 6, padding_symbol, ILLEGAL, NOP
; CHECK: error: padding byte must be ILLEGAL, NOP, or an integer from 0 through 255

LEN 6, cpuid r1
; CHECK: error:
