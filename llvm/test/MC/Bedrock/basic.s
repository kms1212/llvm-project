; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

nop
; CHECK-INST: nop
; CHECK-ENCODING: encoding: [0x20,0x40]

ret
; CHECK-INST: ret
; CHECK-ENCODING: encoding: [0x20,0x41]

mov.q r1, r2
; CHECK-INST: mov.q	r1, r2
; CHECK-ENCODING: encoding: [0x01,0x12]

mov.l r1, r2
; CHECK-INST: mov.l	r1, r2
; CHECK-ENCODING: encoding: [0x00,0x12]

add.q r3, r4
; CHECK-INST: add.q	r3, r4
; CHECK-ENCODING: encoding: [0x03,0x34]

mov.q r5, sp
; CHECK-INST: mov.q	r5, sp
; CHECK-ENCODING: encoding: [0x20,0x05]

mov.q sp, r6
; CHECK-INST: mov.q	sp, r6
; CHECK-ENCODING: encoding: [0x20,0x16]

push r7
; CHECK-INST: push	r7
; CHECK-ENCODING: encoding: [0x20,0x27]

pop r8
; CHECK-INST: pop	r8
; CHECK-ENCODING: encoding: [0x20,0x38]

set.eq r9
; CHECK-INST: set.eq	r9
; CHECK-ENCODING: encoding: [0x21,0x92]

inc.q r10
; CHECK-INST: inc.q	r10
; CHECK-ENCODING: encoding: [0x23,0x0a]

revbyte.q r11
; CHECK-INST: revbyte.q	r11
; CHECK-ENCODING: encoding: [0x22,0xbb]

add.q 255, sp
; CHECK-INST: add.q	255, sp
; CHECK-ENCODING: encoding: [0x2f,0xff]

jmp -1
; CHECK-INST: jmp	-1
; CHECK-ENCODING: encoding: [0x30,0xff]

sub.q 1, sp
; CHECK-INST: sub.q	1, sp
; CHECK-ENCODING: encoding: [0x31,0x01]

j.eq -2
; CHECK-INST: j.eq	-2
; CHECK-ENCODING: encoding: [0x32,0xfe]

trap.gt
; CHECK-INST: trap.gt
; CHECK-ENCODING: encoding: [0x20,0x5f]

jmp 1000
; CHECK-INST: jmp	1000
; CHECK-ENCODING: encoding: [0x48,0x26,0x00,0xe8,0x03]

inc.b r1
; CHECK-INST: inc.b	r1
; CHECK-ENCODING: encoding: [0x40,0x20,0x01]

mov.b r1, r2
; CHECK-INST: mov.b	r1, r2
; CHECK-ENCODING: encoding: [0x40,0x00,0x82]

mov.q r1, [r2]
; CHECK-INST: mov.q	r1, [r2]
; CHECK-ENCODING: encoding: [0x40,0x18,0x92]

mov.q r1, [ds:r2]
; CHECK-INST: mov.q	r1, [ds:r2]
; CHECK-ENCODING: encoding: [0x44,0x18,0xf4,0x12]

mov.q r1, [r2++]
; CHECK-INST: mov.q	r1, [r2++]
; CHECK-ENCODING: encoding: [0x44,0x18,0xf4,0x94]

mov.q r1, [--r2]
; CHECK-INST: mov.q	r1, [--r2]
; CHECK-ENCODING: encoding: [0x44,0x18,0xf4,0x95]

mov.q r1, [r2 + r3]
; CHECK-INST: mov.q	r1, [ds:r2 + r3]
; CHECK-ENCODING: encoding: [0x48,0x18,0xf4,0x92,0x23]

mov.q r1, [r2 + r3++ + 4]
; CHECK-INST: mov.q	r1, [ds:r2 + r3++ + 4]
; CHECK-ENCODING: encoding: [0x4c,0x18,0xf0,0x90,0x23,0x04]

mov.q r1, [ds:0 + --r3 - 5]
; CHECK-INST: mov.q	r1, [ds:0 + --r3 - 5]
; CHECK-ENCODING: encoding: [0x4c,0x18,0xf0,0x99,0x13,0xfb]

mov.q r1, [sp + r3]
; CHECK-INST: mov.q	r1, [sp + r3]
; CHECK-ENCODING: encoding: [0x48,0x18,0xf4,0x8a,0x23]

mov.q r1, [pc + r3 + 4]
; CHECK-INST: mov.q	r1, [pc + r3 + 4]
; CHECK-ENCODING: encoding: [0x4c,0x18,0xf0,0x8b,0x23,0x04]

adc.q r1, [r2]
; CHECK-INST: adc.q	r1, [r2]
; CHECK-ENCODING: encoding: [0x47,0xc0,0xc0,0x92]

mov.q [r1], [r2]
; CHECK-INST: mov.q	[r1], [r2]
; CHECK-ENCODING: encoding: [0x47,0xe0,0xc8,0x92]

rol.q 3, [r8]
; CHECK-INST: rol.q	3, [r8]
; CHECK-ENCODING: encoding: [0x47,0xec,0xc1,0x98]

rdseg cs, r5
; CHECK-INST: rdseg	cs, r5
; CHECK-ENCODING: encoding: [0x47,0xef,0x42,0x05]

invtlb
; CHECK-INST: invtlb
; CHECK-ENCODING: encoding: [0x47,0xef,0x4f,0x01]

nospec, nop
; CHECK-INST: nospec, nop
; CHECK-ENCODING: encoding: [0xa0,0x40,0x01,0x00]

rep r3, mov.q r1, [r2]
; CHECK-INST: rep r3, mov.q	r1, [r2]
; CHECK-ENCODING: encoding: [0xc0,0x18,0x83,0x00,0x92]

nospec, saturate, adc.q r1, [r2]
; CHECK-INST: nospec, saturate, adc.q	r1, [r2]
; CHECK-ENCODING: encoding: [0xc7,0xc0,0x01,0x02,0xc0,0x92]

repeq r0, invtlb
; CHECK-INST: repeq r0, invtlb
; CHECK-ENCODING: encoding: [0xc7,0xef,0x90,0x00,0x4f,0x01]

nospec, mov.q r1, [ds:r2 + r3++ + 4]
; CHECK-INST: nospec, mov.q	r1, [ds:r2 + r3++ + 4]
; CHECK-ENCODING: encoding: [0xcc,0x18,0x01,0x00,0xf0,0x90,0x23,0x04]
