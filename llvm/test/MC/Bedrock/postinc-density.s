# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl postinc_density
postinc_density:
	MOV.L	[A0++], D1
	MOV.L	D1, [A2++]
	ADD.L	[A3++], D4
	MADD.L	[A1++], D3, D0
	MOV.L	[A1++], [A0++]
	RET

# CHECK-LABEL: <postinc_density>:
# CHECK: MOV.L{{[[:space:]]+}}[A0++], D1
# CHECK: MOV.L{{[[:space:]]+}}D1, [A2++]
# CHECK: ADD.L{{[[:space:]]+}}[A3++], D4
# CHECK: MADD.L{{[[:space:]]+}}[A1++], D3, D0
# CHECK: MOV.L{{[[:space:]]+}}[A1++], [A0++]
# CHECK: RET
