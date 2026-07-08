; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llvm-mc -triple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=HEADER
; RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llvm-objdump -d --triple=bedrock %t.o \
; RUN:   | FileCheck %s --check-prefix=DISASM

jmp local_target
; ASM: jmp	0
; ASM-SAME: encoding: [0x50,0x66,0x00,A,A,A,A]
; ASM: fixup A - offset: 3, value: local_target, kind: fixup_bedrock_brdisp32

call local_target
; ASM: call	0
; ASM-SAME: encoding: [0x50,0xe6,0x00,A,A,A,A]
; ASM: fixup A - offset: 3, value: local_target, kind: fixup_bedrock_call32

j.eq ext_target
; ASM: j.eq	0
; ASM-SAME: encoding: [0x50,0x66,0x02,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_brdisp32

call.ne ext_target
; ASM: call.ne	0
; ASM-SAME: encoding: [0x50,0xe6,0x03,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_call32

mov.q [ext_data], r1
; ASM: mov.q	[0], r1
; ASM-SAME: encoding: [0x50,0x38,0xea,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_data, kind: FK_Data_4

mov.q [pc + ext_target], r2
; ASM: mov.q	[pc + 0], r2
; ASM-SAME: encoding: [0x50,0x39,0x66,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_pcrel32

lea.q ext_data, r3
; ASM: lea.q	0, r3
; ASM-SAME: encoding: [0x51,0xb9,0x8e,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_data, kind: fixup_bedrock_imm32

mov.q [ds:r4 + ext_data], r5
; ASM: mov.q	[ds:r4 + 0], r5
; ASM-SAME: encoding: [0x54,0x3a,0xf2,0x14,A,A,A,A]
; ASM: fixup A - offset: 4, value: ext_data, kind: fixup_bedrock_disp32

nospec, mov.q [pc + ext_target], r6
; ASM: nospec, mov.q	[pc + 0], r6
; ASM-SAME: encoding: [0xd0,0x3b,0x01,0x00,0x66,A,A,A,A]
; ASM: fixup A - offset: 5, value: ext_target, kind: fixup_bedrock_pcrel32

nospec, jmp ext_target
; ASM: nospec, jmp	0
; ASM-SAME: encoding: [0xd0,0x66,0x01,0x00,0x00,A,A,A,A]
; ASM: fixup A - offset: 5, value: ext_target, kind: fixup_bedrock_brdisp32

nospec, call ext_target
; ASM: nospec, call	0
; ASM-SAME: encoding: [0xd0,0xe6,0x01,0x00,0x00,A,A,A,A]
; ASM: fixup A - offset: 5, value: ext_target, kind: fixup_bedrock_call32

local_target:
ret

; HEADER: OS/ABI:                            UNIX - System V
; HEADER: Machine:                           Bedrock

; RELOC: Relocation section '.rela.text'
; RELOC-DAG: 0000000000000011  {{[0-9a-f]+}}00000013 R_BEDROCK_BRDISP32S {{.*}} ext_target + 0
; RELOC-DAG: 0000000000000018  {{[0-9a-f]+}}00000015 R_BEDROCK_CALL32S {{.*}} ext_target + 0
; RELOC-DAG: 000000000000001f  {{[0-9a-f]+}}00000003 R_BEDROCK_ABS32S {{.*}} ext_data + 0
; RELOC-DAG: 0000000000000026  {{[0-9a-f]+}}0000000f R_BEDROCK_PCREL32S {{.*}} ext_target + 0
; RELOC-DAG: 000000000000002d  {{[0-9a-f]+}}00000007 R_BEDROCK_IMM32S {{.*}} ext_data + 0
; RELOC-DAG: 0000000000000035  {{[0-9a-f]+}}0000000b R_BEDROCK_DISP32S {{.*}} ext_data + 0
; RELOC-DAG: 000000000000003e  {{[0-9a-f]+}}0000000f R_BEDROCK_PCREL32S {{.*}} ext_target + 0
; RELOC-DAG: 0000000000000047  {{[0-9a-f]+}}00000013 R_BEDROCK_BRDISP32S {{.*}} ext_target + 0
; RELOC-DAG: 0000000000000050  {{[0-9a-f]+}}00000015 R_BEDROCK_CALL32S {{.*}} ext_target + 0

; DISASM: 0: 50 66 00 4d 00 00 00 jmp	77
; DISASM: 7: 50 e6 00 46 00 00 00 call	70
; DISASM: e: 50 66 02 00 00 00 00 j.eq	0
; DISASM: 15: 50 e6 03 00 00 00 00 call.ne	0
; DISASM: 1c: 50 38 ea 00 00 00 00 mov.q	[0], r1
; DISASM: 23: 50 39 66 00 00 00 00 mov.q	[pc + 0], r2
; DISASM: 2a: 51 b9 8e 00 00 00 00 lea.q	0, r3
; DISASM: 31: 54 3a f2 14 00 00 00 00      mov.q	[ds:r4 + 0], r5
; DISASM: 39: d0 3b 01 00 66 00 00 00 00   nospec, mov.q	[pc + 0], r6
; DISASM: 42: d0 66 01 00 00 00 00 00 00   nospec, jmp	0
; DISASM: 4b: d0 e6 01 00 00 00 00 00 00   nospec, call	0
; DISASM: 54: 20 41        	ret
