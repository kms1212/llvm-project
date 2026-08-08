# REQUIRES: bedrock
# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld --emit-relocs -e _start %t/input.o -o %t/relaxed
# RUN: llvm-objdump --triple=bedrock -dr %t/relaxed | FileCheck %s --check-prefix=RELAX
# RUN: ld.lld --no-relax --emit-relocs -e _start %t/input.o -o %t/unrelaxed
# RUN: llvm-objdump --triple=bedrock -dr %t/unrelaxed | FileCheck %s --check-prefix=NORELAX
# RUN: ld.lld --emit-relocs -T %t/far.ld %t/input.o -o %t/far
# RUN: llvm-objdump --triple=bedrock -dr %t/far | FileCheck %s --check-prefix=FAR

# RELAX-LABEL: <_start>:
# RELAX-NEXT: {{.*}}c8 a6 00 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}target
# RELAX-NEXT: {{.*}}c8 a6 02 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}calleq
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}target
# RELAX-NEXT: {{.*}}c8 a6 00 01 00 {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}local_target
# RELAX: {{.*}}c8 a6 00 05 00 {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}adjusted_local_target
# RELAX-NEXT: {{.*}}c8 a6 00 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}target

# NORELAX-LABEL: <_start>:
# NORELAX-NEXT: {{.*}}d0 e6 00 {{[0-9a-f ]+}}{{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# NORELAX-NEXT: {{.*}}d0 e6 02 {{[0-9a-f ]+}}{{.*}}calleq
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# NORELAX-NEXT: {{.*}}c8 a6 00 01 00 {{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}local_target
# NORELAX: {{.*}}c8 a6 00 07 00 {{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}adjusted_local_target
# NORELAX-NEXT: {{.*}}d0 e6 00 {{[0-9a-f ]+}}{{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target

# FAR-LABEL: <_start>:
# FAR-NEXT: {{.*}}d0 e6 00 {{[0-9a-f ]+}}{{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# FAR-NEXT: {{.*}}d0 e6 02 {{[0-9a-f ]+}}{{.*}}calleq
# FAR-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# FAR-NEXT: {{.*}}c8 a6 00 01 00 {{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}local_target
# FAR: {{.*}}c8 a6 00 07 00 {{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}adjusted_local_target
# FAR-NEXT: {{.*}}d0 e6 00 {{[0-9a-f ]+}}{{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target

#--- input.s
.section .text.entry,"ax",@progbits
.p2align 0
.globl _start
_start:
  call target
  calleq target
  call local_target
  nop

local_target:
  ret

  call adjusted_local_target
  call target
adjusted_local_target:
  ret

.section .text.target,"ax",@progbits
.p2align 0
.globl target
target:
  ret

#--- far.ld
SECTIONS {
  .text 0x10000 : { *(.text.entry) }
  .text.target 0x20000 : { *(.text.target) }
}
