; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

pushp 3
; CHECK-INST: pushp	3
; CHECK-ENCODING: encoding: [0x13]

popp 4
; CHECK-INST: popp	4
; CHECK-ENCODING: encoding: [0x1c]

trace 7
; CHECK-INST: trace	7
; CHECK-ENCODING: encoding: [0xc8,0x27,0x84,0x07,0x00]

setf 10
; CHECK-INST: setf	10
; CHECK-ENCODING: encoding: [0xc0,0x2d,0x8a]

clrf 5
; CHECK-INST: clrf	5
; CHECK-ENCODING: encoding: [0xc0,0x25,0x85]

stc
; CHECK-INST: setf	2
; CHECK-ENCODING: encoding: [0xc0,0x2d,0x82]

clc
; CHECK-INST: clrf	2
; CHECK-ENCODING: encoding: [0xc0,0x25,0x82]

repg r1, 2
; CHECK-INST: repg	r1, 2
; CHECK-ENCODING: encoding: [0xc8,0x26,0x81,0x02,0x00]

repg r2, {
  nop
}
; CHECK-INST: repg{{[	 ]+}}r2, { nop }
; CHECK-ENCODING: encoding: [0xc8,0x26,0x82,0x01,0x00,0x01]

repg r14, {
  mov.q r2, r3
  add.q r4, r5
}
; CHECK-INST: repg{{[	 ]+}}r14, { mov.q{{[	 ]+}}r2, r3; add.q{{[	 ]+}}r4, r5 }
; CHECK-ENCODING: encoding: [0xc8,0x26,0x8e,0x04,0x00,0x81,0x23,0x83,0x45]

repgf r13, {
  mov.q r2, r3
  add.q r4, r5
}
; CHECK-INST: repg{{[	 ]+}}r13, { mov.q{{[	 ]+}}r2, r3; add.q{{[	 ]+}}r4, r5 }
; CHECK-ENCODING: encoding: [0xc8,0x26,0x8d,0x04,0x00,0x81,0x23,0x83,0x45]

rep r15, (mov.q r2, r3)
; CHECK-INST: rep{{[	 ]+}}r15, (mov.q{{[	 ]+}}r2, r3)
; CHECK-ENCODING: encoding: [0xc2,0x40,0x0f,0x81,0x23]

repeq r9, (cmp.q r2, r3)
; CHECK-INST: repeq{{[	 ]+}}r9, (cmp.q{{[	 ]+}}r2, r3)
; CHECK-ENCODING: encoding: [0xc2,0x40,0x29,0x87,0x23]

cpuid r1
; CHECK-INST: cpuid	r1
; CHECK-ENCODING: encoding: [0xc0,0x27,0x01]

call 1000
; CHECK-INST: call	1000
; CHECK-ENCODING: encoding: [0xc8,0xa6,0x00,0xe8,0x03]

calleq -200
; CHECK-INST: calleq	-200
; CHECK-ENCODING: encoding: [0xc8,0xa6,0x02,0x38,0xff]

callz -200
; CHECK-INST: calleq	-200
; CHECK-ENCODING: encoding: [0xc8,0xa6,0x02,0x38,0xff]

clz.q [r2], r1
; CHECK-INST: clz.q	[r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xc0,0xe0,0x92]

ctz.l 7, r1
; CHECK-INST: ctz.l	7, r1
; CHECK-ENCODING: encoding: [0xcb,0xc0,0xa8,0xec,0x07]

minu.q [r2], r1
; CHECK-INST: minu.q	[r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xc1,0xc0,0x92]

maxs.q r1, [r2]
; CHECK-INST: maxs.q	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc1,0xf8,0x92]

popcnt.q [r2], r1
; CHECK-INST: popcnt.q	[r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xc2,0xc0,0x92]

mul.q [r2], r1
; CHECK-INST: mul.q	[r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xc2,0xd0,0x92]

divu.q [r2], r1
; CHECK-INST: divu.q	[r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xc2,0xe0,0x92]

bset r1, [r2]
; CHECK-INST: bset	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x08,0x92]

djeq r1, [r2]
; CHECK-INST: djeq	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x90,0x92]

djt r1, [r2]
; CHECK-INST: djt	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x80,0x92]

cmpjeq.l r1, r2, 1000
; CHECK-INST: cmpjeq.l	r1, r2, 1000
; CHECK-ENCODING: encoding: [0xcf,0xc5,0x10,0x92,0xe8,0x03]

testjne.q r3, r4, -200
; CHECK-INST: testjne.q	r3, r4, -200
; CHECK-ENCODING: encoding: [0xcf,0xc5,0x99,0xb4,0x38,0xff]

cmpjlt.b r1, r2, 100
; CHECK-INST: cmpjlt.b	r1, r2, 100
; CHECK-ENCODING: encoding: [0xcb,0xc4,0x60,0x82,0x64]

testjeq.q r3, r4, -5
; CHECK-INST: testjeq.q	r3, r4, -5
; CHECK-ENCODING: encoding: [0xcb,0xc5,0x91,0xa4,0xfb]

lcall r1, [r2]
; CHECK-INST: lcall	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x20,0x92]

ljmp r1, [r2]
; CHECK-INST: ljmp	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x28,0x92]

clmulh.q [r2], r1
; CHECK-INST: clmulh.q	[r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x30,0x92]

seglea.q [ds:r2 + r3], r1
; CHECK-INST: seglea.q	[ds:r2 + r3], r1
; CHECK-ENCODING: encoding: [0xcf,0xc7,0x80,0xf4,0x82,0x23]

movnt.q r1, [r2]
; CHECK-INST: movnt.q	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc8,0xc0,0x92]

movuc.q [r2], r1
; CHECK-INST: movuc.q	[r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xe2,0xc9,0x01]

movcu.q r1, [r2]
; CHECK-INST: movcu.q	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xe3,0xc0,0x92]

movuu.q [r1], [r2]
; CHECK-INST: movuu.q	[r1], [r2]
; CHECK-ENCODING: encoding: [0xc7,0xe4,0xc8,0x92]

prefetchnt [r2]
; CHECK-INST: prefetchnt	[r2]
; CHECK-ENCODING: encoding: [0xc7,0xef,0x45,0x92]

incf.q r1
; CHECK-INST: incf.q	r1
; CHECK-ENCODING: encoding: [0xc0,0x2a,0x81]

decf.q r1
; CHECK-INST: decf.q	r1
; CHECK-ENCODING: encoding: [0xc0,0x2b,0x81]

cmp.q [r1], [r2]
; CHECK-INST: cmp.q	[r1], [r2]
; CHECK-ENCODING: encoding: [0xc7,0xe1,0xc8,0x92]

extsq.b [r1], [r2]
; CHECK-INST: extsq.b	[r1], [r2]
; CHECK-ENCODING: encoding: [0xc7,0xe8,0x48,0x92]

rol.q 4, [r2]
; CHECK-INST: rol.q	4, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xec,0xc2,0x12]

btest 3, [r2]
; CHECK-INST: btest	3, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xee,0x21,0x92]

ptquery 3, [r2], r1
; CHECK-INST: ptquery	3, [r2], r1
; CHECK-ENCODING: encoding: [0xc7,0xef,0x18,0x92]

vtop r1, r2
; CHECK-INST: vtop	r1, r2
; CHECK-ENCODING: encoding: [0xc7,0xef,0x48,0x82]

swpta r1, r2
; CHECK-INST: swpta	r1, r2
; CHECK-ENCODING: encoding: [0xc7,0xef,0x49,0x11]

wrseg r1, ds
; CHECK-INST: wrseg	r1, ds
; CHECK-ENCODING: encoding: [0xc7,0xef,0x40,0x81]

rdcr 1, r2
; CHECK-INST: rdcr	1, r2
; CHECK-ENCODING: encoding: [0xcf,0xef,0x47,0x02,0x01,0x00]

wrcr r2, 1
; CHECK-INST: wrcr	r2, 1
; CHECK-ENCODING: encoding: [0xcf,0xef,0x47,0x12,0x01,0x00]

rdflags r1
; CHECK-INST: rdflags	r1
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x21]

wrflags r1
; CHECK-INST: wrflags	r1
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x31]

rdfflags r1
; CHECK-INST: rdfflags	r1
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x41]

wrfflags r2
; CHECK-INST: wrfflags	r2
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x52]

rdstatus r3
; CHECK-INST: rdstatus	r3
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x63]

wrstatus r4
; CHECK-INST: wrstatus	r4
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x74]

rdfstatus r5
; CHECK-INST: rdfstatus	r5
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x85]

wrfstatus r6
; CHECK-INST: wrfstatus	r6
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0x96]

rdpmc 1, r2
; CHECK-INST: rdpmc	1, r2
; CHECK-ENCODING: encoding: [0xcf,0xef,0x47,0xa2,0x01,0x00]

swpt r1
; CHECK-INST: swpt	r1
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0xb1]

invasid 1
; CHECK-INST: invasid	1
; CHECK-ENCODING: encoding: [0xcf,0xef,0x46,0x00,0x01,0x00]

mulhu.q r1, r2
; CHECK-INST: mulhu.q	r1, r2
; CHECK-ENCODING: encoding: [0xc7,0xef,0x48,0xc2]

mulhs.q r1, r2
; CHECK-INST: mulhs.q	r1, r2
; CHECK-ENCODING: encoding: [0xc7,0xef,0x48,0xd2]

mulhsu.q r1, r2
; CHECK-INST: mulhsu.q	r1, r2
; CHECK-ENCODING: encoding: [0xc7,0xef,0x48,0xe2]

extract.q 4, r1, r2
; CHECK-INST: extract.q	4, r1, r2
; CHECK-ENCODING: encoding: [0xc7,0xcb,0x89,0x04]

movne.q r1, [r2]
; CHECK-INST: movne.q	r1, [r2]
; CHECK-ENCODING: encoding: [0xcb,0xf0,0xf0,0xc0,0x92]

moveq.l [r2], r1
; CHECK-INST: moveq.l	[r2], r1
; CHECK-ENCODING: encoding: [0xcb,0xf0,0xe0,0x88,0x92]

ijt r1, r2, [r3]
; CHECK-INST: ijt	r1, r2, [r3]
; CHECK-ENCODING: encoding: [0xcb,0xf1,0x00,0x41,0x13]

ijne r1, r2, [r3]
; CHECK-INST: ijne	r1, r2, [r3]
; CHECK-ENCODING: encoding: [0xcb,0xf1,0x0c,0x41,0x13]

bndsii.q r1, [r2], r3
; CHECK-INST: bndsii.q	r1, [r2], r3
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x70,0x41,0x92]

bnduxx.l r1, [r2], r3
; CHECK-INST: bnduxx.l	r1, [r2], r3
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x60,0x79,0x92]

divmodu.q [r2], r1, r3
; CHECK-INST: divmodu.q	[r2], r1, r3
; CHECK-ENCODING: encoding: [0xcb,0xf0,0xb0,0x41,0x92]

divmods.l [r2], r1, r3
; CHECK-INST: divmods.l	[r2], r1, r3
; CHECK-ENCODING: encoding: [0xcb,0xf0,0xa0,0x49,0x92]

fetchadd.q/relaxed r1, [r2]
; CHECK-INST: fetchadd.q/relaxed	r1, [r2]
; CHECK-ENCODING: encoding: [0xcb,0xf1,0x70,0x00,0x92]

fetchxor.l/seqcst r1, [r2]
; CHECK-INST: fetchxor.l/seqcst	r1, [r2]
; CHECK-ENCODING: encoding: [0xcb,0xf1,0x61,0x20,0x92]

cmpxchg.q/seqcst r1, r3, [r2]
; CHECK-INST: cmpxchg.q/seqcst	r1, r3, [r2]
; CHECK-ENCODING: encoding: [0xcb,0xf1,0x74,0x61,0x92]
