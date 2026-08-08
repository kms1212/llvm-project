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
; ASM-SAME: encoding: [0xc8,0x26,0x00,A,A]
; ASM: fixup A - offset: 3, value: local_target, kind: fixup_bedrock_brdisp16

call local_target
; ASM: call	0
; ASM-SAME: encoding: [0xc8,0xa6,0x00,A,A]
; ASM: fixup A - offset: 3, value: local_target, kind: fixup_bedrock_call16

jeq ext_target
; ASM: jeq	0
; ASM-SAME: encoding: [0xc8,0x26,0x02,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_brdisp16

callne ext_target
; ASM: callne	0
; ASM-SAME: encoding: [0xc8,0xa6,0x03,A,A]
; ASM: fixup A - offset: 3, value: ext_target, kind: fixup_bedrock_call16

mov.q [ext_data], r1
; ASM: mov.q	[0], r1
; ASM-SAME: encoding: [0xd0,0x38,0xea,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_data, kind: FK_Data_4

mov.q [pc + ext_target], r2
; ASM: mov.q	[pc + 0], r2
; ASM-SAME: encoding: [0xd0,0x39,0x66,A,A,A,A]
; ASM: fixup A - offset: 3, value: ext_target+3, kind: fixup_bedrock_pcrel32

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

mov.q [pc + local_target], r0
; ASM: mov.q	[pc + 0], r0
; ASM-SAME: encoding: [0xd0,0x38,0x66,A,A,A,A]
; ASM: fixup A - offset: 3, value: local_target+3, kind: fixup_bedrock_pcrel32

; HEADER: OS/ABI:                            UNIX - System V
; HEADER: ABI Version:                       0
; HEADER: Machine:                           Bedrock
; HEADER: Version:                           0x1
; HEADER: Entry point address:               0x0
; HEADER: Flags:                             0x0
; HEADER: Size of this header:               64 (bytes)
; HEADER: Size of program headers:           56 (bytes)
; HEADER: Size of section headers:           64 (bytes)

; RELOC: Relocation section '.rela.text'
; RELOC-DAG: 0000000000000003  {{[0-9a-f]+}}00000012 R_BEDROCK_BRDISP16S {{.*}} local_target + 0
; RELOC-DAG: 0000000000000008  {{[0-9a-f]+}}00000014 R_BEDROCK_CALL16S {{.*}} local_target + 0
; RELOC-DAG: 000000000000000d  {{[0-9a-f]+}}00000013 R_BEDROCK_BRDISP32S {{.*}} ext_target + 0
; RELOC-DAG: 0000000000000014  {{[0-9a-f]+}}00000015 R_BEDROCK_CALL32S {{.*}} ext_target + 0
; RELOC-DAG: 000000000000001b  {{[0-9a-f]+}}00000003 R_BEDROCK_ABS32S {{.*}} ext_data + 0
; RELOC-DAG: 0000000000000022  {{[0-9a-f]+}}0000000f R_BEDROCK_PCREL32S {{.*}} ext_target + 3
; RELOC-DAG: 0000000000000029  {{[0-9a-f]+}}00000007 R_BEDROCK_IMM32S {{.*}} ext_data + 0
; RELOC-DAG: 0000000000000031  {{[0-9a-f]+}}0000000b R_BEDROCK_DISP32S {{.*}} ext_data + 0

; DISASM: 0: c8 26 00 00 00 jmp	0
; DISASM: 5: c8 a6 00 00 00 call	0
; DISASM: a: d0 66 02 00 00 00 00 jeq	0
; DISASM: 11: d0 e6 03 00 00 00 00 callne	0
; DISASM: 18: d0 38 ea 00 00 00 00 mov.q	[0], r1
; DISASM: 1f: d0 39 66 00 00 00 00 mov.q	[pc + 0], r2
; DISASM: 26: d1 b9 8e 00 00 00 00 lea.q	0, r3
; DISASM: 2d: d4 3a f2 04 00 00 00 00      mov.q	[ds:r4 + 0], r5
; DISASM: 35: 02           	ret
; DISASM: 36: d0 38 66 ff ff ff ff mov.q	[pc - 1], r0
