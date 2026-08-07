; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

fcvt.s f1, f2
; CHECK-INST: fcvt.s	f1, f2
; CHECK-ENCODING: encoding: [0xc7,0xd7,0x08,0xa2]

fcvt.d f3, r4
; CHECK-INST: fcvt.d	f3, r4
; CHECK-ENCODING: encoding: [0xc7,0xd7,0x99,0xa4]

fcvt.s r5, f6
; CHECK-INST: fcvt.s	r5, f6
; CHECK-ENCODING: encoding: [0xc7,0xd7,0x2a,0xa6]

fcvtu.d f7, f8
; CHECK-INST: fcvtu.d	f7, f8
; CHECK-ENCODING: encoding: [0xc7,0xd7,0x93,0xa8]

fcvtu.s f9, r10
; CHECK-INST: fcvtu.s	f9, r10
; CHECK-ENCODING: encoding: [0xc7,0xd7,0x24,0xaa]

fcvtu.d r11, f12
; CHECK-INST: fcvtu.d	r11, f12
; CHECK-ENCODING: encoding: [0xc7,0xd7,0xb5,0xac]
