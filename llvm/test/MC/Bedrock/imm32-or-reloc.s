# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -dr --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl imm32_or_reloc
imm32_or_reloc:
	OR.L payload@IMM32, D0

# CHECK-LABEL: <imm32_or_reloc>:
# CHECK:       LEN 3, {{[[:space:]]*}}OR.L{{[[:space:]]+}}0, D0
# CHECK-NEXT:  R_BEDROCK_IMM32 payload
