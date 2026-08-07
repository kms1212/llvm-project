; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

fmin.s f1, f2
; CHECK-INST: fmin.s	f1, f2
; CHECK-ENCODING: encoding: [0xc2,0x50,0xe2]

fmin.d [r3], f4
; CHECK-INST: fmin.d	[r3], f4
; CHECK-ENCODING: encoding: [0xc7,0xd5,0xca,0x13]

fmax.d f5, f6
; CHECK-INST: fmax.d	f5, f6
; CHECK-ENCODING: encoding: [0xc2,0x5a,0xf6]

fmax.s 2, f7
; CHECK-INST: fmax.s	2, f7
; CHECK-ENCODING: encoding: [0xcb,0xd5,0x53,0xec,0x02]

fint.s f8, f9
; CHECK-INST: fint.s	f8, f9
; CHECK-ENCODING: encoding: [0xc7,0xd5,0x7c,0x88]

fint.d [r10 + 4], f11
; CHECK-INST: fint.d	[r10 + 4], f11
; CHECK-ENCODING: encoding: [0xcb,0xd5,0xfd,0xaa,0x04]

fint.s f12, [r13]
; CHECK-INST: fint.s	f12, [r13]
; CHECK-ENCODING: encoding: [0xc7,0xd8,0x46,0x1d]

fintrz.d f14, f15
; CHECK-INST: fintrz.d	f14, f15
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x87,0x8e]

fintrz.s [sp + 8], f0
; CHECK-INST: fintrz.s	[sp + 8], f0
; CHECK-ENCODING: encoding: [0xcb,0xd6,0x00,0x60,0x08]

fintrz.d f1, [r2]
; CHECK-INST: fintrz.d	f1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xd8,0xc8,0x92]

fgetexp.s f3, f4
; CHECK-INST: fgetexp.s	f3, f4
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x0a,0x03]

fgetexp.d [r5], f6
; CHECK-INST: fgetexp.d	[r5], f6
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x8b,0x15]

fgetman.d f7, f8
; CHECK-INST: fgetman.d	f7, f8
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x94,0x07]

fgetman.s 3, f9
; CHECK-INST: fgetman.s	3, f9
; CHECK-ENCODING: encoding: [0xcb,0xd6,0x14,0xec,0x03]

fmod.s f10, f11
; CHECK-INST: fmod.s	f10, f11
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x1d,0x8a]

fmod.d [r12], f13
; CHECK-INST: fmod.d	[r12], f13
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x9e,0x9c]

frem.d f14, f15
; CHECK-INST: frem.d	f14, f15
; CHECK-ENCODING: encoding: [0xc7,0xd6,0xa7,0x8e]

frem.s [r0 + 16], f1
; CHECK-INST: frem.s	[r0 + 16], f1
; CHECK-ENCODING: encoding: [0xcb,0xd6,0x20,0xa0,0x10]

fscale.s f2, f3
; CHECK-INST: fscale.s	f2, f3
; CHECK-ENCODING: encoding: [0xc7,0xd6,0x29,0x82]

fscale.d [r4], f5
; CHECK-INST: fscale.d	[r4], f5
; CHECK-ENCODING: encoding: [0xc7,0xd6,0xaa,0x94]

fcopysign.d f6, f7, f8
; CHECK-INST: fcopysign.d	f6, f7, f8
; CHECK-ENCODING: encoding: [0xc7,0xd7,0xb3,0xb8]

fbndii.s f1, f2, f3
; CHECK-INST: fbndii.s	f1, f2, f3
; CHECK-ENCODING: encoding: [0xc7,0xd0,0x09,0x82]

fbndii.d f4, [r5 + 8], f6
; CHECK-INST: fbndii.d	f4, [r5 + 8], f6
; CHECK-ENCODING: encoding: [0xcb,0xd1,0xa3,0x25,0x08]

fbndii.s [r7 + 1], f8, [r9 + 2]
; CHECK-INST: fbndii.s	[r7 + 1], f8, [r9 + 2]
; CHECK-ENCODING: encoding: [0xd3,0xf0,0x02,0x13,0xa9,0x01,0x02]

fbndix.d f10, f11, f12
; CHECK-INST: fbndix.d	f10, f11, f12
; CHECK-ENCODING: encoding: [0xc7,0xd0,0xd6,0x1b]

fbndix.s f13, 1, f14
; CHECK-INST: fbndix.s	f13, 1, f14
; CHECK-ENCODING: encoding: [0xcb,0xd2,0x6f,0x6c,0x01]

fbndix.d [sp + 16], f15, [r0]
; CHECK-INST: fbndix.d	[sp + 16], f15, [r0]
; CHECK-ENCODING: encoding: [0xcf,0xf0,0x17,0xf0,0x10,0x10]

fbndxi.s f1, f2, f3
; CHECK-INST: fbndxi.s	f1, f2, f3
; CHECK-ENCODING: encoding: [0xc7,0xd0,0x09,0xa2]

fbndxi.d f4, [r5], f6
; CHECK-INST: fbndxi.d	f4, [r5], f6
; CHECK-ENCODING: encoding: [0xc7,0xd3,0xa3,0x15]

fbndxi.s 0, f7, [r8 + 4]
; CHECK-INST: fbndxi.s	0, f7, [r8 + 4]
; CHECK-ENCODING: encoding: [0xd3,0xf0,0x09,0xf6,0x28,0x00,0x04]

fbndxx.d f9, f10, f11
; CHECK-INST: fbndxx.d	f9, f10, f11
; CHECK-ENCODING: encoding: [0xc7,0xd0,0xcd,0xba]

fbndxx.s f12, [r13], f14
; CHECK-INST: fbndxx.s	f12, [r13], f14
; CHECK-ENCODING: encoding: [0xc7,0xd4,0x67,0x1d]

fbndxx.d [r15], f0, 2
; CHECK-INST: fbndxx.d	[r15], f0, 2
; CHECK-ENCODING: encoding: [0xcf,0xf0,0x1c,0x0f,0xec,0x02]
