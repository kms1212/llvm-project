# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s
# RUN: llvm-objdump -s %t.o | FileCheck --check-prefix=BYTES %s

	.text
	.space	62
	.balign	64
	REPG	D0, {
		MOV.Q	[A0], D1
		ADD.Q	D1, D2
	}

# CHECK: 40:{{[[:space:]]+}}REPG D0,{{[[:space:]]+}}MOV.Q{{[[:space:]]+}}[A0], D1
# CHECK: 44:{{[[:space:]]+}}ENDG{{[[:space:]]+}}ADD.Q{{[[:space:]]+}}D1, D2

# BYTES: 0030 00000000 00000000 00000000 00004f0f
