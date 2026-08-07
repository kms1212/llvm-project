# REQUIRES: bedrock
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.o
# RUN: not ld.lld -z max-page-size=0x800 %t.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=ALIGN
# RUN: echo "PHDRS { bad PT_LOAD FLAGS(2); } SECTIONS { . = 0x10000; .text : { *(.text) } :bad }" > %t.script
# RUN: not ld.lld -T %t.script %t.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAGS

# ALIGN: error: Bedrock PT_LOAD segment alignment 2048 is below the 4096-byte minimum
# FLAGS: error: Bedrock PT_LOAD segment must set PF_R

.text
.byte 0
