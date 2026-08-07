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
; ASM-SAME: encoding: [0xd0,0x66,0x00,A,A,A,A]
; ASM: fixup A - offset: 3, value: local_target, kind: fixup_bedrock_brdisp32

call local_target
; ASM: call	0
; ASM-SAME: encoding: [0xd0,0xe6,0x00,A,A,A,A]
; ASM: fixup A - offset: 3, value: local_target, kind: fixup_bedrock_call32

jeq ext_target
; ASM: jeq	0
; ASM-SAME: encoding: [0xd0,0x66,0x02,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_brdisp32

callne ext_target
; ASM: callne	0
; ASM-SAME: encoding: [0xd0,0xe6,0x03,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_call32

mov.q [ext_data], r1
; ASM: mov.q	[0], r1
; ASM-SAME: encoding: [0xd0,0x38,0xea,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_data, kind: FK_Data_4

mov.q [pc + ext_target], r2
; ASM: mov.q	[pc + 0], r2
; ASM-SAME: encoding: [0xd0,0x39,0x66,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_pcrel32

lea.q ext_data, r3
; ASM: lea.q	0, r3
; ASM-SAME: encoding: [0xd1,0xb9,0x8e,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_data, kind: fixup_bedrock_imm32

mov.q [ds:r4 + ext_data], r5
; ASM: mov.q	[ds:r4 + 0], r5
; ASM-SAME: encoding: [0xd4,0x3a,0xf2,0x04,A,A,A,A]
; ASM: fixup A - offset: 4, value: ext_data, kind: fixup_bedrock_disp32

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

; DISASM: 0: d0 66 00 32 00 00 00 jmp	50
; DISASM: 7: d0 e6 00 2b 00 00 00 call	43
; DISASM: e: d0 66 02 00 00 00 00 jeq	0
; DISASM: 15: d0 e6 03 00 00 00 00 callne	0
; DISASM: 1c: d0 38 ea 00 00 00 00 mov.q	[0], r1
; DISASM: 23: d0 39 66 00 00 00 00 mov.q	[pc + 0], r2
; DISASM: 2a: d1 b9 8e 00 00 00 00 lea.q	0, r3
; DISASM: 31: d4 3a f2 04 00 00 00 00      mov.q	[ds:r4 + 0], r5
; DISASM: 39: 02           	ret
