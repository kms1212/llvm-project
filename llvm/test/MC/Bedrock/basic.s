# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-readobj -h -r %t.o | FileCheck %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS

	.text
	.globl caller
caller:
	REPG	D0, {
		MOV.Q	[A0], D1
		ADD.Q	D1, D2
	}
	MOV.Q	g@ABS64, A0
	MOV.Q	[A0], D0
	MOV.L	[A0++], A5
	REP	D2, MOV.L	D0, [A1++]
	MOV.Q	[SP + 0], D0
	MOV.Q	D0, [SP + 0]
	MOV.L	1.W, [SP + 32]
	LEA	[SP], A1
	LEA	[SP + 32], A0
	LEA	[A6 + D2 * 4], A0
	LEA	[A6 + D2.L * 4], A0
	MOV.L	[SP + D2 * 1 + 32], D0
	MOV.L	D0, [SP + D2 * 1 + 32]
	MOV.L	[SP + D2 * 1 + 32], [A0]
	MOV.L	[A0], [SP + D2 * 1 + 32]
	MOV.L	[A0 - 4], D0
	FMOV.D	F1, F0
	FMOV.D	[A0], F0
	FMOV.D	F0, [SP + 0]
	FMOVEQ	F1, F2
	FMOVEQ.S	[A0], F1
	FMOVNE.D	F1, [A0]
	FADD.D	F1, F0
	FADD.S	[A0], F1
	FDIV.D	[A0], F1
	FABS.D	F1, F0
	FABS.S	[A0], F2
	FNEG.S	F1, F0
	FNEG.D	F2, [A0]
	FSQRT.D	F1, F2
	FSQRT.S	[A0], F3
	FCLR	F4
	FCLR	[A0]
	FCOPYSIGN.D	F1, F0, F0
	FMADD.S	F1, F2, F3
	FMSUB.D	[A0], F1, F2
	FNMADD.S	F1, [A0], F2
	FNMSUB.D	F1, F2, F3
	FCMP.S	[A0], F1
	FCMP.D	F1, F0
	FCVT	D0, F0
	FCVT	F0, D0
	FCVTU	D0, F0
	EXTSW.B	D1, D2
	EXTZW.B	[A0], D3
	EXTZL.W	[A0], D3
	EXTSQ.L	D1, [SP + 0]
	AFENCE
	FETCHADD.L/SEQCST	D0, [A0]
	FETCHXOR.Q/RELAXED	D0, [SP + 0]
	CMPXCHG.L/ACQREL	D0, D1, [A0]
	CALL	callee@PCREL32
	JGT.W	done@WORD_PCREL16
	DJT.L	D0, done@WORD_PCREL16
	IJT.L	D1, D2, done@WORD_PCREL16
	MOVT.L	D1, D0
	MOVNE.Q	A0, D0
	MOVGT.L	[A0], D2
	MOVLT.L	D2, [A0]
	RET
	HALT
	NOP
	MOV.Q	[SP + 352], D0
	ADD.Q	368, A7
	XOR.Q	-142, D1
	AND.Q	6148914691236517205, D0
	AND.L	-993, D1
	MULU.L	97, D0
	DIVS.Q	4, D1
	SHL.Q	D1, D0
	SHL.Q	63, [A0]
	SHR.L	D1, [A0]
	ROL.Q	D1, D0
	ROL.Q	63, [A0]
	ROR.L	D1, [A0]
	CMP.B	D2, D1
	CMP.Q	D1, A7
	CMP.Q	A2, D5

	.data
	.globl g
g:
	.quad	5

# CHECK: Machine: EM_BEDROCK
# CHECK: R_BEDROCK_ABS64 g 0x0
# CHECK: R_BEDROCK_PCREL32 callee 0x0
# CHECK: R_BEDROCK_WORD_PCREL16 done 0x0

# DIS-LABEL: <caller>:
# DIS: REPG D0, MOV.Q{{[[:space:]]+}}[A0], D1
# DIS: ENDG ADD.Q{{[[:space:]]+}}D1, D2
# DIS: MOV.Q{{[[:space:]]+}}0, A0
# DIS: MOV.L{{[[:space:]]+}}[A0++], A5
# DIS: REP{{[[:space:]]+}}D2, MOV.L{{[[:space:]]+}}D0, [A1++]
# DIS: MOV.L{{[[:space:]]+}}1, [SP + 32]
# DIS: LEA{{[[:space:]]+}}[SP + 0], A1
# DIS: LEA{{[[:space:]]+}}[SP + 32], A0
# DIS: LEA{{[[:space:]]+}}[A6 + D2 * 4], A0
# DIS: LEA{{[[:space:]]+}}[A6 + D2.L * 4], A0
# DIS: MOV.L{{[[:space:]]+}}[SP + D2 * 1 + 32], D0
# DIS: MOV.L{{[[:space:]]+}}D0, [SP + D2 * 1 + 32]
# DIS: MOV.L{{[[:space:]]+}}[SP + D2 * 1 + 32], [A0]
# DIS: MOV.L{{[[:space:]]+}}[A0], [SP + D2 * 1 + 32]
# DIS: MOV.L{{[[:space:]]+}}[A0 + -4], D0
# DIS: FMOVEQ.D{{[[:space:]]+}}F1, F2
# DIS: FMOVEQ.S{{[[:space:]]+}}[A0], F1
# DIS: FMOVNE.D{{[[:space:]]+}}F1, [A0]
# DIS: FADD.S{{[[:space:]]+}}[A0], F1
# DIS: FDIV.D{{[[:space:]]+}}[A0], F1
# DIS: FABS.S{{[[:space:]]+}}[A0], F2
# DIS: FNEG.D{{[[:space:]]+}}F2, [A0]
# DIS: FSQRT.D{{[[:space:]]+}}F1, F2
# DIS: FSQRT.S{{[[:space:]]+}}[A0], F3
# DIS: FCLR{{[[:space:]]+}}F4
# DIS: FCLR{{[[:space:]]+}}[A0]
# DIS: FMADD.S{{[[:space:]]+}}F1, F2, F3
# DIS: FMSUB.D{{[[:space:]]+}}[A0], F1, F2
# DIS: FNMADD.S{{[[:space:]]+}}F1, [A0], F2
# DIS: FNMSUB.D{{[[:space:]]+}}F1, F2, F3
# DIS: FCMP.S{{[[:space:]]+}}[A0], F1
# DIS: EXTSW.B{{[[:space:]]+}}D1, D2
# DIS: EXTZW.B{{[[:space:]]+}}[A0], D3
# DIS: EXTZL.W{{[[:space:]]+}}[A0], D3
# DIS: EXTSQ.L{{[[:space:]]+}}D1, [SP + 0]
# DIS: AFENCE
# DIS: FETCHADD.L/SEQCST{{[[:space:]]+}}D0, [A0]
# DIS: CMPXCHG.L/ACQREL{{[[:space:]]+}}D0, D1, [A0]
# DIS: CALL{{[[:space:]]+}}0@PCREL32
# DIS: JGT.W{{[[:space:]]+}}0@WORD_PCREL16
# DIS: DJT.L{{[[:space:]]+}}D0, 0@WORD_PCREL16
# DIS: IJT.L{{[[:space:]]+}}D1, D2, 0@WORD_PCREL16
# DIS: MOVT.L{{[[:space:]]+}}D1, D0
# DIS: MOVNE.Q{{[[:space:]]+}}A0, D0
# DIS: MOVGT.L{{[[:space:]]+}}[A0], D2
# DIS: MOVLT.L{{[[:space:]]+}}D2, [A0]
# DIS: RET
# DIS: HALT
# DIS: NOP
# DIS: MOV.Q{{[[:space:]]+}}[SP + 352], D0
# DIS: ADD.Q{{[[:space:]]+}}368, A7
# DIS: XOR.Q{{[[:space:]]+}}-142, D1
# DIS: AND.Q{{[[:space:]]+}}6148914691236517205, D0
# DIS: AND.L{{[[:space:]]+}}-993, D1
# DIS: MULU.L{{[[:space:]]+}}97, D0
# DIS: DIVS.Q{{[[:space:]]+}}4, D1
# DIS: SHL.Q{{[[:space:]]+}}D1, D0
# DIS: SHL.Q{{[[:space:]]+}}63, [A0]
# DIS: SHR.L{{[[:space:]]+}}D1, [A0]
# DIS: ROL.Q{{[[:space:]]+}}D1, D0
# DIS: ROL.Q{{[[:space:]]+}}63, [A0]
# DIS: ROR.L{{[[:space:]]+}}D1, [A0]
# DIS: CMP.B{{[[:space:]]+}}D2, D1
# DIS: CMP.Q{{[[:space:]]+}}D1, A7
# DIS: CMP.Q{{[[:space:]]+}}A2, D5
