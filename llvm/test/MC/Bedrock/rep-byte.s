# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl rep_byte
rep_byte:
	REP	D0, MOV.B	[A1++], [A2++]
	REP	D1, MOV.B	D0, [A3++]
	REP	D0, MOV.B	[--A3], [--A2]
	RET

# CHECK-LABEL: <rep_byte>:
# CHECK: REP{{[[:space:]]+}}D0, MOV.B{{[[:space:]]+}}[A1++], [A2++]
# CHECK: REP{{[[:space:]]+}}D1, MOV.B{{[[:space:]]+}}D0, [A3++]
# CHECK: REP{{[[:space:]]+}}D0, MOV.B{{[[:space:]]+}}[--A3], [--A2]
# CHECK: RET
