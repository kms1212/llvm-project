# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-readobj -r %t.o | FileCheck %s

	.data
data_relocs:
	.byte	abs8@ABS8
	.short	abs16@ABS16
	.long	abs32@ABS32 + 4
	.quad	abs64@ABS64 + 8
	.long	pc32@PCREL32
	.quad	pc64@PCREL64
	.long	sec32@SECTION_REL32
	.quad	got64@GOT64
	.long	gotpc32@GOTPCREL32
	.quad	gotpc64@GOTPCREL64
	.long	plt32@PLT32
	.quad	plt64@PLT64

# CHECK: R_BEDROCK_ABS8 abs8 0x0
# CHECK: R_BEDROCK_ABS16 abs16 0x0
# CHECK: R_BEDROCK_ABS32 abs32 0x4
# CHECK: R_BEDROCK_ABS64 abs64 0x8
# CHECK: R_BEDROCK_PCREL32 pc32 0x0
# CHECK: R_BEDROCK_PCREL64 pc64 0x0
# CHECK: R_BEDROCK_SECTION_REL32 sec32 0x0
# CHECK: R_BEDROCK_GOT64 got64 0x0
# CHECK: R_BEDROCK_GOTPCREL32 gotpc32 0x0
# CHECK: R_BEDROCK_GOTPCREL64 gotpc64 0x0
# CHECK: R_BEDROCK_PLT32 plt32 0x0
# CHECK: R_BEDROCK_PLT64 plt64 0x0
