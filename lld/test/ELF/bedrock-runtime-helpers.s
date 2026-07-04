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
	CALL	memcpy@PCREL32
	CALL	memmove@PCREL32
	CALL	memset@PCREL32
	CALL	memcmp@PCREL32
	CALL	__divti3@PCREL32
	CALL	__udivti3@PCREL32
	CALL	__modti3@PCREL32
	CALL	__umodti3@PCREL32
	CALL	__atomic_load_16@PCREL32
	CALL	__atomic_store_16@PCREL32
	RET

	.globl	memcpy
memcpy:
	RET
	.globl	memmove
memmove:
	RET
	.globl	memset
memset:
	RET
	.globl	memcmp
memcmp:
	RET
	.globl	__divti3
__divti3:
	RET
	.globl	__udivti3
__udivti3:
	RET
	.globl	__modti3
__modti3:
	RET
	.globl	__umodti3
__umodti3:
	RET
	.globl	__atomic_load_16
__atomic_load_16:
	RET
	.globl	__atomic_store_16
__atomic_store_16:
	RET
