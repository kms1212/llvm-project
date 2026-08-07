# REQUIRES: bedrock
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.o
# RUN: ld.lld %t.o -e 0 -o %t
# RUN: llvm-readobj -x .refs %t | FileCheck %s

# CHECK:      Hex dump of section '.refs':
# CHECK-NEXT: 0x{{[0-9a-f]+}} 13000000 15000000 00000000

.section .target,"aw",@progbits
.space 16
target:
.byte 0

.section .refs,"a",@progbits
refs32:
.long 0
.reloc refs32, R_BEDROCK_SECTION_REL32, target+3
refs64:
.quad 0
.reloc refs64, R_BEDROCK_SECTION_REL64, target+5
