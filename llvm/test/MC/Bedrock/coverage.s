; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

pushm 3
; CHECK-INST: pushm	3
; CHECK-ENCODING: encoding: [0x48,0x27,0x84,0x03,0x00]

popm 3
; CHECK-INST: popm	3
; CHECK-ENCODING: encoding: [0x48,0x27,0x85,0x03,0x00]

trace 7
; CHECK-INST: trace	7
; CHECK-ENCODING: encoding: [0x48,0x27,0x86,0x07,0x00]

repg r1, 2
; CHECK-INST: repg	r1, 2
; CHECK-ENCODING: encoding: [0x48,0x26,0x81,0x02,0x00]

cpuid r1
; CHECK-INST: cpuid	r1
; CHECK-ENCODING: encoding: [0x40,0x27,0x01]

sum.q 255, r2
; CHECK-INST: sum.q	255, r2
; CHECK-ENCODING: encoding: [0x49,0xe6,0x02,0xff,0x00]

call 1000
; CHECK-INST: call	1000
; CHECK-ENCODING: encoding: [0x48,0xa6,0x00,0xe8,0x03]

call.eq -200
; CHECK-INST: call.eq	-200
; CHECK-ENCODING: encoding: [0x48,0xa6,0x02,0x38,0xff]

clz.q [r2], r1
; CHECK-INST: clz.q	[r2], r1
; CHECK-ENCODING: encoding: [0x47,0xc0,0xe0,0x92]

ctz.l 7, r1
; CHECK-INST: ctz.l	7, r1
; CHECK-ENCODING: encoding: [0x4b,0xc0,0xa8,0xec,0x07]

minu.q [r2], r1
; CHECK-INST: minu.q	[r2], r1
; CHECK-ENCODING: encoding: [0x47,0xc1,0xc0,0x92]

maxs.q r1, [r2]
; CHECK-INST: maxs.q	r1, [r2]
; CHECK-ENCODING: encoding: [0x47,0xc1,0xf8,0x92]

popcnt.q [r2], r1
; CHECK-INST: popcnt.q	[r2], r1
; CHECK-ENCODING: encoding: [0x47,0xc2,0xc0,0x92]

mul.q [r2], r1
; CHECK-INST: mul.q	[r2], r1
; CHECK-ENCODING: encoding: [0x47,0xc2,0xd0,0x92]

divu.q [r2], r1
; CHECK-INST: divu.q	[r2], r1
; CHECK-ENCODING: encoding: [0x47,0xc2,0xe0,0x92]

bset r1, [r2]
; CHECK-INST: bset	r1, [r2]
; CHECK-ENCODING: encoding: [0x47,0xc3,0x08,0x92]

dj.eq [r2]
; CHECK-INST: dj.eq	[r2]
; CHECK-ENCODING: encoding: [0x47,0xc3,0x21,0x12]

lcall r1, [r2]
; CHECK-INST: lcall	r1, [r2]
; CHECK-ENCODING: encoding: [0x47,0xc3,0x28,0x92]

ljmp r1, [r2]
; CHECK-INST: ljmp	r1, [r2]
; CHECK-ENCODING: encoding: [0x47,0xc3,0x30,0x92]

clmulh.q [r2], r1
; CHECK-INST: clmulh.q	[r2], r1
; CHECK-ENCODING: encoding: [0x47,0xc3,0x38,0x92]

seglea.q [ds:r2 + r3]
; CHECK-INST: seglea.q	[ds:r2 + r3]
; CHECK-ENCODING: encoding: [0x4f,0xc4,0xf0,0x74,0x92,0x23]

incn.q r1
; CHECK-INST: incn.q	r1
; CHECK-ENCODING: encoding: [0x47,0xc4,0xf0,0x81]

decn.q r1
; CHECK-INST: decn.q	r1
; CHECK-ENCODING: encoding: [0x47,0xc4,0xf0,0x91]

cmp.q [r1], [r2]
; CHECK-INST: cmp.q	[r1], [r2]
; CHECK-ENCODING: encoding: [0x47,0xe1,0xc8,0x92]

extsq.b [r1], [r2]
; CHECK-INST: extsq.b	[r1], [r2]
; CHECK-ENCODING: encoding: [0x47,0xe8,0x48,0x92]

rol.q 4, [r2]
; CHECK-INST: rol.q	4, [r2]
; CHECK-ENCODING: encoding: [0x47,0xec,0xc2,0x12]

btest 3, [r2]
; CHECK-INST: btest	3, [r2]
; CHECK-ENCODING: encoding: [0x47,0xee,0x21,0x92]

ptquery 3, [r2], r1
; CHECK-INST: ptquery	3, [r2], r1
; CHECK-ENCODING: encoding: [0x47,0xef,0x18,0x92]

vtop r1, r2
; CHECK-INST: vtop	r1, r2
; CHECK-ENCODING: encoding: [0x47,0xef,0x40,0x12]

swpta r1, r2
; CHECK-INST: swpta	r1, r2
; CHECK-ENCODING: encoding: [0x47,0xef,0x41,0x12]

wrseg r1, ds
; CHECK-INST: wrseg	r1, ds
; CHECK-ENCODING: encoding: [0x47,0xef,0x42,0x91]

rdcr 1, r2
; CHECK-INST: rdcr	1, r2
; CHECK-ENCODING: encoding: [0x4f,0xef,0x48,0x02,0x01,0x00]

wrcr r2, 1
; CHECK-INST: wrcr	r2, 1
; CHECK-ENCODING: encoding: [0x4f,0xef,0x48,0x12,0x01,0x00]

rdflags r1
; CHECK-INST: rdflags	r1
; CHECK-ENCODING: encoding: [0x47,0xef,0x48,0x21]

wrflags r1
; CHECK-INST: wrflags	r1
; CHECK-ENCODING: encoding: [0x47,0xef,0x48,0x31]

rdpmc 1, r2
; CHECK-INST: rdpmc	1, r2
; CHECK-ENCODING: encoding: [0x4f,0xef,0x48,0xa2,0x01,0x00]

swpt r1
; CHECK-INST: swpt	r1
; CHECK-ENCODING: encoding: [0x47,0xef,0x48,0xb1]

invasid 1
; CHECK-INST: invasid	1
; CHECK-ENCODING: encoding: [0x4f,0xef,0x4f,0x00,0x01,0x00]

mulhu.q r1, r2
; CHECK-INST: mulhu.q	r1, r2
; CHECK-ENCODING: encoding: [0x47,0xef,0x80,0x12]

mulhs.q r1, r2
; CHECK-INST: mulhs.q	r1, r2
; CHECK-ENCODING: encoding: [0x47,0xef,0x81,0x12]

mulhsu.q r1, r2
; CHECK-INST: mulhsu.q	r1, r2
; CHECK-ENCODING: encoding: [0x47,0xef,0x82,0x12]
