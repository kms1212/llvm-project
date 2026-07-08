# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl flag_ea_imm
flag_ea_imm:
	CMP.B	127, D1
	TEST.B	127, D1
	CMP.W	32767, D2
	TEST.W	32767, D2
	CMP.Q	199, A7
	RET

# CHECK-LABEL: <flag_ea_imm>:
# CHECK: CMP.B{{[[:space:]]+}}127, D1
# CHECK: TEST.B{{[[:space:]]+}}127, D1
# CHECK: CMP.W{{[[:space:]]+}}32767, D2
# CHECK: TEST.W{{[[:space:]]+}}32767, D2
# CHECK: CMP.Q{{[[:space:]]+}}199, A7
# CHECK: RET
