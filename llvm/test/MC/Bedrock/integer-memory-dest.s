# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s
# RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=RAW

	.text
	.globl integer_memory_dest
integer_memory_dest:
	CLR	D0
	CLR.Q	A1
	INC.L	[SP + 16]
	DEC.L	[SP + 16]
	ADD.L	63, [A0]
	ADC.L	1, D0
	ADC.L	1, [SP + 16]
	ADD.L	1, [SP + 16]
	SBB.L	2, D1
	SBB.L	2, [SP + 16]
	SUB.L	2, [SP + 16]
	AND.L	15, D1
	AND.L	3, [SP + 16]
	OR.L	4, D2
	OR.L	4, [SP + 16]
	TEST.L	6, D2
	TEST.L	6, [SP + 16]
	XOR.L	5, D3
	XOR.L	5, [SP + 16]
	CMP.B	84, [A0]
	CMP.B	80, [A0 + 1]
	CMP.L	7, D3
	CMP.L	305419896, D1
	CMP.L	7, [SP + 16]
	TEST.L	305419896, D1
	ADD.L	D0, [SP + 16]
	SUB.L	D0, [SP + 16]
	AND.L	D0, [SP + 16]
	OR.L	D0, [SP + 16]
	XOR.L	D0, [SP + 16]
	CMP.L	[A0 + 4], D0
	CMP.L	D0, [A0 + 4]
	TEST.L	[A0 + 4], D0
	TEST.L	D0, [A0 + 4]
	RET

# CHECK-LABEL: <integer_memory_dest>:
# CHECK: CLR.Q{{[[:space:]]+}}D0
# CHECK: CLR.Q{{[[:space:]]+}}A1
# CHECK: INC.L{{[[:space:]]+}}[SP + 16]
# CHECK: DEC.L{{[[:space:]]+}}[SP + 16]
# CHECK: ADD.L{{[[:space:]]+}}63, [A0]
# CHECK: ADC.L{{[[:space:]]+}}1, D0
# CHECK: ADC.L{{[[:space:]]+}}1, [SP + 16]
# CHECK: ADD.L{{[[:space:]]+}}1, [SP + 16]
# CHECK: SBB.L{{[[:space:]]+}}2, D1
# CHECK: SBB.L{{[[:space:]]+}}2, [SP + 16]
# CHECK: SUB.L{{[[:space:]]+}}2, [SP + 16]
# CHECK: AND.L{{[[:space:]]+}}15, D1
# CHECK: AND.L{{[[:space:]]+}}3, [SP + 16]
# CHECK: OR.L{{[[:space:]]+}}4, D2
# CHECK: OR.L{{[[:space:]]+}}4, [SP + 16]
# CHECK: TEST.L{{[[:space:]]+}}6, D2
# CHECK: TEST.L{{[[:space:]]+}}6, [SP + 16]
# CHECK: XOR.L{{[[:space:]]+}}5, D3
# CHECK: XOR.L{{[[:space:]]+}}5, [SP + 16]
# CHECK: CMP.B{{[[:space:]]+}}84, [A0]
# CHECK: CMP.B{{[[:space:]]+}}80, [A0 + 1]
# CHECK: CMP.L{{[[:space:]]+}}7, D3
# CHECK: CMP.L{{[[:space:]]+}}305419896, D1
# CHECK: CMP.L{{[[:space:]]+}}7, [SP + 16]
# CHECK: TEST.L{{[[:space:]]+}}305419896, D1
# CHECK: ADD.L{{[[:space:]]+}}D0, [SP + 16]
# CHECK: SUB.L{{[[:space:]]+}}D0, [SP + 16]
# CHECK: AND.L{{[[:space:]]+}}D0, [SP + 16]
# CHECK: OR.L{{[[:space:]]+}}D0, [SP + 16]
# CHECK: XOR.L{{[[:space:]]+}}D0, [SP + 16]
# CHECK: CMP.L{{[[:space:]]+}}[A0 + 4], D0
# CHECK: CMP.L{{[[:space:]]+}}D0, [A0 + 4]
# CHECK: TEST.L{{[[:space:]]+}}[A0 + 4], D0
# CHECK: TEST.L{{[[:space:]]+}}D0, [A0 + 4]
# CHECK: RET

# RAW: 3e 1f 90 7f{{[[:space:]]+}}ADD.L{{[[:space:]]+}}63, [A0]
# RAW: 3e 1f 80 01{{[[:space:]]+}}ADC.L{{[[:space:]]+}}1, D0
# RAW: 3f 1f 81 0f{{[[:space:]]+}}AND.L{{[[:space:]]+}}15, D1
# RAW: 3f 1f 82 44{{[[:space:]]+}}OR.L{{[[:space:]]+}}4, D2
# RAW: 3f 1f 83 c5{{[[:space:]]+}}XOR.L{{[[:space:]]+}}5, D3
# RAW: 41 26 78 56 34 12{{[[:space:]]+}}CMP.L{{[[:space:]]+}}305419896, D1
# RAW: 43 26 78 56 34 12{{[[:space:]]+}}TEST.L{{[[:space:]]+}}305419896, D1
