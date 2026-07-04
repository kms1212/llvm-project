# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl bitops
bitops:
	BCHG.L	5, D0
	BCLR.L	6, D1
	BSET.L	7, D2
	BTEST.L	52, D3
	BCHG.L	1, [A0 + 4]
	BCLR.L	2, [SP + 16]
	BSET.L	51, [A6 + 1132]
	RET

# CHECK-LABEL: <bitops>:
# CHECK: BCHG.L{{[[:space:]]+}}5, D0
# CHECK: BCLR.L{{[[:space:]]+}}6, D1
# CHECK: BSET.L{{[[:space:]]+}}7, D2
# CHECK: BTEST.L{{[[:space:]]+}}52, D3
# CHECK: BCHG.L{{[[:space:]]+}}1, [A0 + 4]
# CHECK: BCLR.L{{[[:space:]]+}}2, [SP + 16]
# CHECK: BSET.L{{[[:space:]]+}}51, [A6 + 1132]
# CHECK: RET
