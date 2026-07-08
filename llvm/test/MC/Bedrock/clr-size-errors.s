# RUN: not llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s

	.text
	CLR.L	D0

# CHECK: error: CLR only supports implicit .Q size
