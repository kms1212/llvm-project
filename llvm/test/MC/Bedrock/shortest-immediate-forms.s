# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -s %t.o | FileCheck %s --check-prefix=BYTES
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS

	.text
	.globl shortest_immediate_forms
shortest_immediate_forms:
	MOV.Q	0, D0
	MOV.Q	1, D0
	MOV.L	0, D0
	MOV.Q	0, A0
	MOV.Q	1, A0
	MOV.Q	32767, A0
	MOV.Q	32768, A0
	MOV.Q	65535, A0
	MOV.Q	-1, A0
	MOV.Q	4294967295, A0

# BYTES: Contents of section .text:
# BYTES-NEXT: 0000 200f321a 01003218 0000180f 4a2f3272
# BYTES-NEXT: 0010 01004a2f 3272ff7f 4a3f3372 00800000
# BYTES-NEXT: 0020 4a3f3372 ffff0000 4a2f3272 ffff284f
# BYTES-NEXT: 0030 ffffffff 00000000

# DIS-LABEL: <shortest_immediate_forms>:
# DIS: CLR.Q{{[[:space:]]+}}D0
# DIS: MOV.Q{{[[:space:]]+}}1, D0
# DIS: MOV.L{{[[:space:]]+}}0, D0
# DIS: CLR.Q{{[[:space:]]+}}A0
# DIS: MOV.Q{{[[:space:]]+}}1, A0
# DIS: MOV.Q{{[[:space:]]+}}32767, A0
# DIS: MOV.Q{{[[:space:]]+}}32768, A0
# DIS: MOV.Q{{[[:space:]]+}}65535, A0
# DIS: MOV.Q{{[[:space:]]+}}-1, A0
# DIS: MOV.Q{{[[:space:]]+}}4294967295, A0
