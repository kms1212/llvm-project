# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS
# RUN: llvm-objdump -s %t.o | FileCheck %s --check-prefix=BYTES

	.text
	NOP
	.balign	16
	RET

# DIS: 0:{{[[:space:]]+}}NOP
# DIS: 2:{{[[:space:]]+}}LEN 7,{{[[:space:]]+}}NOP
# DIS: 4:{{[[:space:]]+}}LEN 6,{{[[:space:]]+}}NOP
# DIS: 6:{{[[:space:]]+}}LEN 5,{{[[:space:]]+}}NOP
# DIS: 8:{{[[:space:]]+}}LEN 4,{{[[:space:]]+}}NOP
# DIS: {{[aA]}}:{{[[:space:]]+}}LEN 3,{{[[:space:]]+}}NOP
# DIS: {{[cC]}}:{{[[:space:]]+}}LEN 2,{{[[:space:]]+}}NOP
# DIS: {{[eE]}}:{{[[:space:]]+}}NOP
# DIS: 10:{{[[:space:]]+}}RET

# BYTES: Contents of section .text:
# BYTES-NEXT: 0000 4f0f4f6f 4f5f4f4f 4f3f4f2f 4f1f4f0f
# BYTES-NEXT: 0010 300f
