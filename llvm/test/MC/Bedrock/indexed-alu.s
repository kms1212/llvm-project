# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl indexed_alu
indexed_alu:
	ADD.L	[A0 + D3.L * 4], D0
	SUB.L	[A0 + D5.L * 4], D6
	XOR.L	[A2 + D2.L * 4], D1
	MULU.L	[A4 + D7.L * 4], D2
	INC.L	[A6 + D1.L * 4]
	DEC.L	[A6 + D1.L * 4]
	CMP.L	D3, [A6 + D4.L * 4]
	CMP.L	[A6 + D4.L * 4], D3
	TEST.L	D3, [A6 + D4.L * 4]
	TEST.L	[A6 + D4.L * 4], D3
	SUB.L	[A6 + D3 * 1 - 4], D6
	RET

# CHECK-LABEL: <indexed_alu>:
# CHECK: ADD.L{{[[:space:]]+}}[A0 + D3.L * 4], D0
# CHECK: SUB.L{{[[:space:]]+}}[A0 + D5.L * 4], D6
# CHECK: XOR.L{{[[:space:]]+}}[A2 + D2.L * 4], D1
# CHECK: MULU.L{{[[:space:]]+}}[A4 + D7.L * 4], D2
# CHECK: INC.L{{[[:space:]]+}}[A6 + D1.L * 4]
# CHECK: DEC.L{{[[:space:]]+}}[A6 + D1.L * 4]
# CHECK: CMP.L{{[[:space:]]+}}D3, [A6 + D4.L * 4]
# CHECK: CMP.L{{[[:space:]]+}}[A6 + D4.L * 4], D3
# CHECK: TEST.L{{[[:space:]]+}}D3, [A6 + D4.L * 4]
# CHECK: TEST.L{{[[:space:]]+}}[A6 + D4.L * 4], D3
# CHECK: SUB.L{{[[:space:]]+}}[A6 + D3 * 1 - 4], D6
# CHECK: RET
