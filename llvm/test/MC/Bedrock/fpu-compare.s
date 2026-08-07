; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

fcmp.s f1, f2
; CHECK-INST: fcmp.s	f1, f2
; CHECK-ENCODING: encoding: [0xc2,0x21,0x01]

fcmp.d [r3 + 4], f4
; CHECK-INST: fcmp.d	[r3 + 4], f4
; CHECK-ENCODING: encoding: [0xcb,0xd5,0xaa,0x23,0x04]

ftest.s f5
; CHECK-INST: ftest.s	f5
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x30,0x05]

ftest.d [r6]
; CHECK-INST: ftest.d	[r6]
; CHECK-ENCODING: encoding: [0xc7,0xd6,0xb0,0x16]

fclass.s f7, r8
; CHECK-INST: fclass.s	f7, r8
; CHECK-ENCODING: encoding: [0xc7,0xd7,0x03,0xa8]

fmoveq f9, f10
; CHECK-INST: fmoveq	f9, f10
; CHECK-ENCODING: encoding: [0xc7,0xd9,0x14,0x8a]

fmovne.s [r1], f2
; CHECK-INST: fmovne.s	[r1], f2
; CHECK-ENCODING: encoding: [0xc7,0xda,0x19,0x11]

fmovvc.d f3, [r4 + 8]
; CHECK-INST: fmovvc.d	f3, [r4 + 8]
; CHECK-ENCODING: encoding: [0xcb,0xd9,0xc9,0xa4,0x08]
