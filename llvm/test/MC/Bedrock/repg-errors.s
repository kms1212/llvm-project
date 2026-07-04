# RUN: not llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s

	.text
	.space	62
	REPG	D0, {
		MOV.Q	[A0], D1
		ADD.Q	D1, D2
	}

# CHECK: error: REPG group crosses a 64-byte I-cache line; align it explicitly before the block
