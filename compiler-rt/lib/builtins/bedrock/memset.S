//===-- memset.S - Bedrock freestanding memset ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

	.text
	.globl	memset
memset:
	MOV.Q	A0, A1
	REP	D1, MOV.B	D0, [A1++]
	RET
