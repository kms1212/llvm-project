; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

fmov.s f0, f1
; CHECK-INST: fmov.s	f0, f1
; CHECK-ENCODING: encoding: [0xc2,0x40,0x61]

fmov.d f2, f3
; CHECK-INST: fmov.d	f2, f3
; CHECK-ENCODING: encoding: [0xc2,0x49,0x63]

fadd.s f4, f5
; CHECK-INST: fadd.s	f4, f5
; CHECK-ENCODING: encoding: [0xc2,0x42,0x75]

fadd.d f6, f7
; CHECK-INST: fadd.d	f6, f7
; CHECK-ENCODING: encoding: [0xc2,0x4b,0x77]

fsub.s f8, f9
; CHECK-INST: fsub.s	f8, f9
; CHECK-ENCODING: encoding: [0xc2,0x54,0x09]

fsub.d f10, f11
; CHECK-INST: fsub.d	f10, f11
; CHECK-ENCODING: encoding: [0xc2,0x5d,0x0b]

fmul.s f12, f13
; CHECK-INST: fmul.s	f12, f13
; CHECK-ENCODING: encoding: [0xc2,0x56,0x1d]

fmul.d f14, f15
; CHECK-INST: fmul.d	f14, f15
; CHECK-ENCODING: encoding: [0xc2,0x5f,0x1f]

fdiv.s f1, f2
; CHECK-INST: fdiv.s	f1, f2
; CHECK-ENCODING: encoding: [0xc2,0x50,0xa2]

fdiv.d f3, f4
; CHECK-INST: fdiv.d	f3, f4
; CHECK-ENCODING: encoding: [0xc2,0x59,0xa4]
