# RUN: not llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s

	.text
	.globl imm6_reserved
imm6_reserved:
	XOR.L	51, [SP + 16]

# CHECK: error: cannot encode Bedrock instruction
