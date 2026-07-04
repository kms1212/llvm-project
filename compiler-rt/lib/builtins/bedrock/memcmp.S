//===-- memcmp.S - Bedrock freestanding memcmp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

	.text
	.globl	memcmp
memcmp:
	MOV.Q	D0, D1
	CLR.Q	D0
.Lloop:
	TEST.Q	D1, D1
	JEQ.W	.Ldone@WORD_PCREL16
	DEC.Q	D1
	MOV.B	[A1++], D3
	MOV.B	[A0++], D4
	CMP.B	D3, D4
	JEQ.W	.Lloop@WORD_PCREL16
	EXTZL.B	D3, D1
	EXTZL.B	D4, D0
	SUB.L	D1, D0
.Ldone:
	RET
