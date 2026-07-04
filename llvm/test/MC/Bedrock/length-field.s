# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	LEN	4, AND.W 4660, D0
	NOP
	.short	0x3010
	.short	0x1234
	NOP

# CHECK: 0:{{[[:space:]]+}}LEN 4, AND.W{{[[:space:]]+}}4660, D0
# CHECK: 4:{{[[:space:]]+}}NOP
# CHECK: 6:{{[[:space:]]+}}LEN 4, AND.W{{[[:space:]]+}}4660, D0
# CHECK: {{[aA]}}:{{[[:space:]]+}}NOP
