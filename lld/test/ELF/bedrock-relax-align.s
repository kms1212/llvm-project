# REQUIRES: bedrock
# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/caller.s -o %t/caller.o
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/target.s -o %t/target.o
# RUN: ld.lld --emit-relocs -T %t/link.ld %t/caller.o %t/target.o -o %t/relaxed
# RUN: llvm-objdump --triple=bedrock -dr %t/relaxed | FileCheck %s --check-prefix=RELAX
# RUN: llvm-readelf -s %t/relaxed | FileCheck %s --check-prefix=SYMBOL
# RUN: ld.lld --no-relax --emit-relocs -T %t/link.ld %t/caller.o %t/target.o -o %t/unrelaxed
# RUN: llvm-objdump --triple=bedrock -dr %t/unrelaxed | FileCheck %s --check-prefix=NORELAX
# RUN: llvm-readelf -s %t/unrelaxed | FileCheck %s --check-prefix=SYMBOL

# RELAX-LABEL: <_start>:
# RELAX-NEXT: {{.*}}cb bc 02 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}target
# RELAX-LABEL: <aligned_call>:
# RELAX-NEXT: {{.*}}d3 bc 03 {{[0-9a-f ]+}}{{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# RELAX-LABEL: <event_entry>:
# RELAX-NEXT: {{.*}}02 {{.*}}ret

# NORELAX-LABEL: <_start>:
# NORELAX-NEXT: {{.*}}d3 bc 03 {{[0-9a-f ]+}}{{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# NORELAX-LABEL: <aligned_call>:
# NORELAX-NEXT: {{.*}}d3 bc 03 {{[0-9a-f ]+}}{{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# NORELAX-LABEL: <event_entry>:
# NORELAX-NEXT: {{.*}}02 {{.*}}ret

# SYMBOL: 0000000000010020 {{.*}} event_entry

#--- caller.s
.section .text.unaligned,"ax",@progbits
.p2align 0
.globl _start
_start:
  call target
  .byte 0xaa

.section .text.aligned,"ax",@progbits
.globl aligned_call
aligned_call:
  call target
  .balign 16
.globl event_entry
event_entry:
  ret

#--- target.s
.section .text.target,"ax",@progbits
.p2align 0
.globl target
target:
  ret

#--- link.ld
SECTIONS {
  .text 0x10000 : {
    *(.text.unaligned)
    *(.text.aligned)
    *(.text.target)
  }
}
