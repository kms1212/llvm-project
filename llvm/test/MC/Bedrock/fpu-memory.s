; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

fclr f15
; CHECK-INST: fclr	f15
; CHECK-ENCODING: encoding: [0xc2,0x40,0x8f]

fxchg f1, f14
; CHECK-INST: fxchg	f1, f14
; CHECK-ENCODING: encoding: [0xc2,0xd0,0x8e]

fmov.s [r1], f2
; CHECK-INST: fmov.s	[r1], f2
; CHECK-ENCODING: encoding: [0xc7,0xd5,0x01,0x11]

fmov.d f3, [r4 + 8]
; CHECK-INST: fmov.d	f3, [r4 + 8]
; CHECK-ENCODING: encoding: [0xcb,0xd8,0x81,0xa4,0x08]

fadd.s 1, f4
; CHECK-INST: fadd.s	1, f4
; CHECK-ENCODING: encoding: [0xcb,0xd5,0x0a,0x6c,0x01]

fsub.d [sp + 16], f5
; CHECK-INST: fsub.d	[sp + 16], f5
; CHECK-ENCODING: encoding: [0xcb,0xd5,0x92,0xe0,0x10]

fmul.s [r6], f7
; CHECK-INST: fmul.s	[r6], f7
; CHECK-ENCODING: encoding: [0xc7,0xd5,0x1b,0x96]

fdiv.d [r8 - 4], f9
; CHECK-INST: fdiv.d	[r8 - 4], f9
; CHECK-ENCODING: encoding: [0xcb,0xd5,0xa4,0xa8,0xfc]

fabs.s [r10], f11
; CHECK-INST: fabs.s	[r10], f11
; CHECK-ENCODING: encoding: [0xc7,0xd5,0x35,0x9a]

fabs.d f12, [r13]
; CHECK-INST: fabs.d	f12, [r13]
; CHECK-ENCODING: encoding: [0xc7,0xd8,0x8e,0x1d]

fneg.d [r14], f15
; CHECK-INST: fneg.d	[r14], f15
; CHECK-ENCODING: encoding: [0xc7,0xd5,0xbf,0x9e]

fneg.s f0, [r1 + 2]
; CHECK-INST: fneg.s	f0, [r1 + 2]
; CHECK-ENCODING: encoding: [0xcb,0xd8,0x10,0x21,0x02]

fsqrt.s [r2], f3
; CHECK-INST: fsqrt.s	[r2], f3
; CHECK-ENCODING: encoding: [0xc7,0xd5,0x41,0x92]

fsqrt.d f4, [sp + 24]
; CHECK-INST: fsqrt.d	f4, [sp + 24]
; CHECK-ENCODING: encoding: [0xcb,0xd8,0x9a,0x60,0x18]

fround.d [r5], f6
; CHECK-INST: fround.d	[r5], f6
; CHECK-ENCODING: encoding: [0xc7,0xd5,0xdb,0x15]

fround.s f7, [r8]
; CHECK-INST: fround.s	f7, [r8]
; CHECK-ENCODING: encoding: [0xc7,0xd8,0x23,0x98]

ftrunc.s [r9], f10
; CHECK-INST: ftrunc.s	[r9], f10
; CHECK-ENCODING: encoding: [0xc7,0xd5,0x65,0x19]

ftrunc.d f11, [r12 + 32]
; CHECK-INST: ftrunc.d	f11, [r12 + 32]
; CHECK-ENCODING: encoding: [0xcb,0xd8,0xad,0xac,0x20]

fceil.d [r13], f14
; CHECK-INST: fceil.d	[r13], f14
; CHECK-ENCODING: encoding: [0xc7,0xd5,0xef,0x1d]

fceil.s f15, [r0]
; CHECK-INST: fceil.s	f15, [r0]
; CHECK-ENCODING: encoding: [0xc7,0xd8,0x37,0x90]

ffloor.s [r1 + 1], f2
; CHECK-INST: ffloor.s	[r1 + 1], f2
; CHECK-ENCODING: encoding: [0xcb,0xd5,0x71,0x21,0x01]

ffloor.d f3, [r4]
; CHECK-INST: ffloor.d	f3, [r4]
; CHECK-ENCODING: encoding: [0xc7,0xd8,0xb9,0x94]
