; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

eret
; CHECK-INST: eret
; CHECK-ENCODING: encoding: [0x04]

push cs
; CHECK-INST: push	cs
; CHECK-ENCODING: encoding: [0x0d]

push ds
; CHECK-INST: push	ds
; CHECK-ENCODING: encoding: [0xa2,0x80]

push gs5
; CHECK-INST: push	gs5
; CHECK-ENCODING: encoding: [0xa2,0x87]

pop ss
; CHECK-INST: pop	ss
; CHECK-ENCODING: encoding: [0xa2,0x89]

pop gs5
; CHECK-INST: pop	gs5
; CHECK-ENCODING: encoding: [0xa2,0x8f]

rdseg ds, r1
; CHECK-INST: rdseg	ds, r1
; CHECK-ENCODING: encoding: [0xc7,0xef,0x40,0x01]

rdseg gs5, r2
; CHECK-INST: rdseg	gs5, r2
; CHECK-ENCODING: encoding: [0xc7,0xef,0x40,0x72]

rdseg cs, r3
; CHECK-INST: rdseg	cs, r3
; CHECK-ENCODING: encoding: [0xc7,0xef,0x47,0xc3]

wrseg r4, gs5
; CHECK-INST: wrseg	r4, gs5
; CHECK-ENCODING: encoding: [0xc7,0xef,0x40,0xf4]

call [r1]
; CHECK-INST: call	[r1]
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x70,0x11]

callne [r2]
; CHECK-INST: callne	[r2]
; CHECK-ENCODING: encoding: [0xc7,0xc3,0x71,0x92]

jmp.l [r3]
; CHECK-INST: jmp.l	[r3]
; CHECK-ENCODING: encoding: [0xc7,0xc9,0x00,0x13]

jmp.q [r4]
; CHECK-INST: jmp.q	[r4]
; CHECK-ENCODING: encoding: [0xc7,0xc9,0x80,0x14]

jeq.l [r5]
; CHECK-INST: jeq.l	[r5]
; CHECK-ENCODING: encoding: [0xc7,0xc9,0x01,0x15]

jne.q [r6]
; CHECK-INST: jne.q	[r6]
; CHECK-ENCODING: encoding: [0xc7,0xc9,0x81,0x96]

fmovcr.s 1, f2
; CHECK-INST: fmovcr.s	1, f2
; CHECK-ENCODING: encoding: [0xcf,0xd6,0x31,0x02,0x01,0x00]

fmovcr.d 65535, f15
; CHECK-INST: fmovcr.d	65535, f15
; CHECK-ENCODING: encoding: [0xcf,0xd6,0xb1,0x0f,0xff,0xff]
