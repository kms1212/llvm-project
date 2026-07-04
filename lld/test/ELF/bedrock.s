# REQUIRES: bedrock
# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj %s -o %t.o
# RUN: ld.lld -m elf64bedrock %t.o --image-base=0 -Ttext=0x1000 -o %t
# RUN: llvm-readobj -h -r %t | FileCheck %s

# CHECK:      Format: elf64-littlebedrock
# CHECK:      Arch: bedrock
# CHECK:      Machine: EM_BEDROCK
# CHECK:      Relocations [
# CHECK-NEXT: ]

	.text
	.globl	_start
_start:
	CALL	callee@PCREL32
	JMP.W	done@WORD_PCREL16
callee:
	RET
done:
	MOV.Q	target@ABS64, A0
	RET

	.data
	.globl	target
target:
	.quad	0x1122334455667788
ptr_to_target:
	.quad	target@ABS64
