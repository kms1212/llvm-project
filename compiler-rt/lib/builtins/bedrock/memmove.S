//===-- memmove.S - Bedrock freestanding memmove -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

	.text
	.globl	memmove
memmove:
	CMP.Q	A1, A0
	JEQ.W	.Ldone@WORD_PCREL16
	TEST.Q	D0, D0
	JEQ.W	.Ldone@WORD_PCREL16
	CMP.Q	A1, A0
	JUGE.W	.Lreverse@WORD_PCREL16

	MOV.Q	A0, A2
	MOV.Q	A1, A3
	REP	D0, MOV.B	[A3++], [A2++]
	RET

.Lreverse:
	MOV.Q	A0, A2
	MOV.Q	A1, A3
	ADD.Q	D0, A2
	ADD.Q	D0, A3
	REP	D0, MOV.B	[--A3], [--A2]
.Ldone:
	RET
