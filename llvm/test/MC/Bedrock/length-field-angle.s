# RUN: llvm-mc -triple=bedrock-unknown-unknown -show-encoding %s | FileCheck %s

	.text
	LEN	<4>, AND.W 4660, D0
	LEN	<4, 1..8>, AND.W 4660, D0

# CHECK: LEN 4, AND.W 0x1234, D0
# CHECK-SAME: encoding: {{\[}}0x10,0x30,0x34,0x12{{\]}}
# CHECK: LEN 4, AND.W 0x1234, D0
# CHECK-SAME: encoding: {{\[}}0x10,0x30,0x34,0x12{{\]}}
