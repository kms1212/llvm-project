# RUN: llvm-mc -triple=bedrock-unknown-unknown -show-encoding %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS

	.text

	MOV.L	[GS4:A1], D0
# ASM: MOV.L [GS4:A1], D0
# ASM-SAME: encoding: {{\[}}0x3f,0x18,0x20,0x27{{\]}}

	MOV.L	[GS4:A1 + 4], D0
# ASM: MOV.L [GS4:A1 + 4], D0
# ASM-SAME: encoding: {{\[}}0x3f,0x28,0x20,0x2f,0x04,0x00{{\]}}

	MOV.L	[GS4:A1 + 70000], D0
# ASM: MOV.L [GS4:A1 + 70000], D0
# ASM-SAME: encoding: {{\[}}0x3f,0x38,0x20,0x37,0x70,0x11,0x01,0x00{{\]}}

	MOV.L	[GS4:4660], D0
# ASM: MOV.L [GS4:4660], D0
# ASM-SAME: encoding: {{\[}}0x3f,0x38,0x00,0x3f,0x34,0x12,0x00,0x00{{\]}}

	MOV.L	[GS4:5000000000], D0
# ASM: MOV.L [GS4:5000000000], D0
# ASM-SAME: encoding: {{\[}}0x3f,0x58,0x00,0x47,0x00,0xf2,0x05,0x2a,0x01,0x00,0x00,0x00{{\]}}

	MOV.L	[GS4:A0++], D0
# ASM: MOV.L [GS4:A0++], D0
# ASM-SAME: encoding: {{\[}}0x3f,0xa8,0x04,0x00,0x00,0x27{{\]}}

	MOV.L	[GS4:++A0], D0
# ASM: MOV.L [GS4:++A0], D0
# ASM-SAME: encoding: {{\[}}0x3f,0xa8,0x05,0x00,0x00,0x27{{\]}}

	MOV.L	[GS4:A0--], D0
# ASM: MOV.L [GS4:A0--], D0
# ASM-SAME: encoding: {{\[}}0x3f,0xa8,0x06,0x00,0x00,0x27{{\]}}

	MOV.L	[GS4:--A0], D0
# ASM: MOV.L [GS4:--A0], D0
# ASM-SAME: encoding: {{\[}}0x3f,0xa8,0x07,0x00,0x00,0x27{{\]}}

	SATURATE ADD.L D1, D2
# ASM: SATURATE ADD.L D1, D2
# ASM-SAME: encoding: {{\[}}0x91,0x90,0x02,0x00{{\]}}

	NOSPEC MOV.L [A0], D0
# ASM: NOSPEC MOV.L [A0], D0
# ASM-SAME: encoding: {{\[}}0x10,0x98,0x01,0x00{{\]}}

	NONTEMPORAL MOV.L [A0], D0
# ASM: NONTEMPORAL MOV.L [A0], D0
# ASM-SAME: encoding: {{\[}}0x10,0x98,0x03,0x00{{\]}}

	MOV.Q U:[A0], D0
# ASM: MOV.Q U:[A0], D0
# ASM-SAME: encoding: {{\[}}0x10,0x9a,0x08,0x00{{\]}}

	MOV.Q D0, U:[A1]
# ASM: MOV.Q D0, U:[A1]
# ASM-SAME: encoding: {{\[}}0x11,0x96,0x09,0x00{{\]}}

	MOV.B U:[A0++], C:[A1++]
# ASM: MOV.B U:[A0++], C:[A1++]
# ASM-SAME: encoding: {{\[}}0x4a,0xaf,0x04,0x08,0x50,0x44{{\]}}

	MOV.B C:[A0++], U:[A1++]
# ASM: MOV.B C:[A0++], U:[A1++]
# ASM-SAME: encoding: {{\[}}0x4a,0xaf,0x04,0x09,0x50,0x44{{\]}}

	MOV.B U:[A0++], U:[A1++]
# ASM: MOV.B U:[A0++], U:[A1++]
# ASM-SAME: encoding: {{\[}}0x4a,0xaf,0x04,0x0a,0x50,0x44{{\]}}

	INC.L U:[A0]
# ASM: INC.L U:[A0]
# ASM-SAME: encoding: {{\[}}0x3c,0xaf,0x0a,0x00,0x90,0x44{{\]}}

# DIS: MOV.L [GS4:A1], D0
# DIS: MOV.L [GS4:A1 + 4], D0
# DIS: MOV.L [GS4:A1 + 70000], D0
# DIS: MOV.L [GS4:4660], D0
# DIS: MOV.L [GS4:5000000000], D0
# DIS: MOV.L [GS4:A0++], D0
# DIS: MOV.L [GS4:++A0], D0
# DIS: MOV.L [GS4:A0--], D0
# DIS: MOV.L [GS4:--A0], D0
# DIS: SATURATE ADD.L D1, D2
# DIS: NOSPEC MOV.L [A0], D0
# DIS: NONTEMPORAL MOV.L [A0], D0
# DIS: MOV.Q U:[A0], D0
# DIS: MOV.Q D0, U:[A1]
# DIS: MOV.B U:[A0++], C:[A1++]
# DIS: MOV.B C:[A0++], U:[A1++]
# DIS: MOV.B U:[A0++], U:[A1++]
# DIS: INC.L U:[A0]
