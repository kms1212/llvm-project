# RUN: not llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s

	.text
bad_a_imm:
	OR.Q	12884901888, A6

# CHECK: error: cannot encode Bedrock instruction
