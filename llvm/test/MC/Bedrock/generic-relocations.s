# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-readobj -r %t.o | FileCheck %s

	.text
symrel:
	CALL	ext_func@PLT32
	JMP.L	far_target@PLT32
	MOV.Q	got_object@GOT64, A0
	ADD.L	imm_value@IMM32 + 12, D1
	MOV.L	[PC + pc_value@PCREL32], D2
	MOV.L	[PC + D0.L * 4 + pc_index_value@PCREL32], D3
	CALL	__tls_get_addr@TLSDESC_CALL
	MOV.L	[PC + src@DISP32], [PC + dst@DISP32]
	CMP.L	[PC + lhs@DISP32], [PC + rhs@DISP32]

# CHECK: R_BEDROCK_PLT32 ext_func 0x0
# CHECK: R_BEDROCK_PLT32 far_target 0x0
# CHECK: R_BEDROCK_GOT64 got_object 0x0
# CHECK: R_BEDROCK_IMM32 imm_value 0xC
# CHECK: R_BEDROCK_PCREL32 pc_value 0x0
# CHECK: R_BEDROCK_PCREL32 pc_index_value 0x0
# CHECK: R_BEDROCK_TLSDESC_CALL __tls_get_addr 0x0
# CHECK: R_BEDROCK_DISP32 src 0x0
# CHECK: R_BEDROCK_DISP32 dst 0x0
# CHECK: R_BEDROCK_DISP32 lhs 0x0
# CHECK: R_BEDROCK_DISP32 rhs 0x0
