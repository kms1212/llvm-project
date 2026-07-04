# RUN: not llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s

	.text
start:
	JMP.W	far@WORD_PCREL16
	.space	200000, 0
far:
	RET

# CHECK: error: out of range Bedrock word-relative fixup
