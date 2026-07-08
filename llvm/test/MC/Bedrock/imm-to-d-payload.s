# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -s %t.o | FileCheck %s --check-prefix=BYTES
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS

	.text
	.globl imm_to_d
imm_to_d:
	ADD.L	63, D0
	SUB.L	63, D0
	CMP.L	63, D0
	TEST.L	63, D0
	ADD.W	-1, D1
	SUB.W	-1, D1
	CMP.W	-1, D1
	TEST.W	-1, D1

# BYTES: Contents of section .text:
# BYTES-NEXT: 0000 00163f00 02163f00 01163f00 03163f00
# BYTES-NEXT: 0010 4014ffff 4214ffff 4114ffff 4314ffff

# DIS-LABEL: <imm_to_d>:
# DIS: ADD.L{{[[:space:]]+}}63, D0
# DIS: SUB.L{{[[:space:]]+}}63, D0
# DIS: CMP.L{{[[:space:]]+}}63, D0
# DIS: TEST.L{{[[:space:]]+}}63, D0
# DIS: ADD.W{{[[:space:]]+}}-1, D1
# DIS: SUB.W{{[[:space:]]+}}-1, D1
# DIS: CMP.W{{[[:space:]]+}}-1, D1
# DIS: TEST.W{{[[:space:]]+}}-1, D1
