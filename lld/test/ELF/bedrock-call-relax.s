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
# RELAX-NEXT: {{.*}}cb bc 02 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}target
# RELAX-NEXT: {{.*}}cb b8 02 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}calleq
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}target
# RELAX-NEXT: {{.*}}cb bc 02 01 00 {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}local_target
# RELAX: {{.*}}cb bc 02 05 00 {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}adjusted_local_target
# RELAX-NEXT: {{.*}}cb bc 02 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}call
# RELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}target
# RELAX: {{.*}}cb bc 06 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}jmp
# RELAX-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}target
# RELAX-NEXT: {{.*}}cb b8 22 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}jeq
# RELAX-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}target
# RELAX-NEXT: {{.*}}cb bc 06 01 00 {{.*}}jmp
# RELAX-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}local_jump_target
# RELAX: {{.*}}cb bc 06 05 00 {{.*}}jmp
# RELAX-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}adjusted_local_jump_target
# RELAX-NEXT: {{.*}}cb bc 06 {{[0-9a-f][0-9a-f]}} {{[0-9a-f][0-9a-f]}} {{.*}}jmp
# RELAX-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}target
# RELAX-LABEL: <short_before_relaxed_call>:
# RELAX-NEXT: {{.*}}a4 05 {{.*}}jmp{{.*}}5
# RELAX-NEXT: {{.*}}R_BEDROCK_BRDISP8S{{.*}}.Lshort_after_relaxed_call
# RELAX-LABEL: <ij_before_relaxed_call>:
# RELAX-NEXT: {{.*}}db f1 32 11 d6 0e 00 00 00 {{.*}}ijult{{.*}}14
# RELAX-NEXT: {{.*}}R_BEDROCK_PCREL32S{{.*}}.Lij_after_relaxed_call

# NORELAX-LABEL: <_start>:
# NORELAX-NEXT: {{.*}}d3 bc 03 {{[0-9a-f ]+}}{{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# NORELAX-NEXT: {{.*}}d3 b8 12 {{[0-9a-f ]+}}{{.*}}calleq
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# NORELAX-NEXT: {{.*}}cb bc 02 01 00 {{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}local_target
# NORELAX: {{.*}}cb bc 02 07 00 {{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}adjusted_local_target
# NORELAX-NEXT: {{.*}}d3 bc 03 {{[0-9a-f ]+}}{{.*}}call
# NORELAX-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# NORELAX: {{.*}}d3 bc 07 {{[0-9a-f ]+}}{{.*}}jmp
# NORELAX-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}target
# NORELAX-NEXT: {{.*}}d3 b8 32 {{[0-9a-f ]+}}{{.*}}jeq
# NORELAX-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}target
# NORELAX-NEXT: {{.*}}cb bc 06 01 00 {{.*}}jmp
# NORELAX-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}local_jump_target
# NORELAX: {{.*}}cb bc 06 07 00 {{.*}}jmp
# NORELAX-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}adjusted_local_jump_target
# NORELAX-NEXT: {{.*}}d3 bc 07 {{[0-9a-f ]+}}{{.*}}jmp
# NORELAX-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}target
# NORELAX-LABEL: <short_before_relaxed_call>:
# NORELAX-NEXT: {{.*}}a4 07 {{.*}}jmp{{.*}}7
# NORELAX-NEXT: {{.*}}R_BEDROCK_BRDISP8S{{.*}}.Lshort_after_relaxed_call
# NORELAX-LABEL: <ij_before_relaxed_call>:
# NORELAX-NEXT: {{.*}}db f1 32 11 d6 10 00 00 00 {{.*}}ijult{{.*}}16
# NORELAX-NEXT: {{.*}}R_BEDROCK_PCREL32S{{.*}}.Lij_after_relaxed_call

# FAR-LABEL: <_start>:
# FAR-NEXT: {{.*}}d3 bc 03 {{[0-9a-f ]+}}{{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# FAR-NEXT: {{.*}}d3 b8 12 {{[0-9a-f ]+}}{{.*}}calleq
# FAR-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# FAR-NEXT: {{.*}}cb bc 02 01 00 {{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}local_target
# FAR: {{.*}}cb bc 02 07 00 {{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}adjusted_local_target
# FAR-NEXT: {{.*}}d3 bc 03 {{[0-9a-f ]+}}{{.*}}call
# FAR-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}target
# FAR: {{.*}}d3 bc 07 {{[0-9a-f ]+}}{{.*}}jmp
# FAR-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}target
# FAR-NEXT: {{.*}}d3 b8 32 {{[0-9a-f ]+}}{{.*}}jeq
# FAR-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}target
# FAR-NEXT: {{.*}}cb bc 06 01 00 {{.*}}jmp
# FAR-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}local_jump_target
# FAR: {{.*}}cb bc 06 07 00 {{.*}}jmp
# FAR-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}adjusted_local_jump_target
# FAR-NEXT: {{.*}}d3 bc 07 {{[0-9a-f ]+}}{{.*}}jmp
# FAR-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}target

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

  jmp target
  jeq target
  jmp local_jump_target
  nop

local_jump_target:
  ret

  jmp adjusted_local_jump_target
  jmp target
adjusted_local_jump_target:
  ret

short_before_relaxed_call:
  .byte 0xa4
.Lshort_field:
  .byte 0
  .reloc .Lshort_field, R_BEDROCK_BRDISP8S, .Lshort_after_relaxed_call
  call target
.Lshort_after_relaxed_call:
  ret

ij_before_relaxed_call:
  .byte 0xdb, 0xf1, 0x32, 0x11, 0xd6
.Lij_field:
  .long 0
  .reloc .Lij_field, R_BEDROCK_PCREL32S, .Lij_after_relaxed_call+5
  call target
.Lij_after_relaxed_call:
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
