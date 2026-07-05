# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl bitwise_a_imm
bitwise_a_imm:
	AND.Q	63, A6
	OR.Q	63, A6
	XOR.Q	63, A6
	RET

# CHECK-LABEL: <bitwise_a_imm>:
# CHECK: AND.Q{{[[:space:]]+}}63, A6
# CHECK: OR.Q{{[[:space:]]+}}63, A6
# CHECK: XOR.Q{{[[:space:]]+}}63, A6
# CHECK: RET
