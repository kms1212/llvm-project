//===-- memcpy.S - Bedrock freestanding memcpy ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

	.text
	.globl	memcpy
memcpy:
	MOV.Q	A0, A2
	MOV.Q	A1, A3
	REP	D0, MOV.B	[A3++], [A2++]
	RET
