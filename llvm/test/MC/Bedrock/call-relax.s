; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.o
; RUN: llvm-objdump -dr --triple=bedrock %t.o | FileCheck %s

near:
  call near_target
  call external
  call far_target
  jmp near_target
  jeq external_jump
  jmp far_target

near_target:
  ret

  .space 32768
far_target:
  ret

; CHECK-LABEL: <near>:
; CHECK-NEXT: {{.*}}c8 a6 00 00 00 {{.*}}call{{.*}}0
; CHECK-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}near_target
; CHECK-NEXT: {{.*}}d0 e6 00 00 00 00 00 {{.*}}call{{.*}}0
; CHECK-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}external
; CHECK-NEXT: {{.*}}d0 e6 00 {{[0-9a-f ]+}}{{.*}}call
; CHECK-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}far_target
; CHECK-NEXT: {{.*}}c8 26 00 00 00 {{.*}}jmp{{.*}}0
; CHECK-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}near_target
; CHECK-NEXT: {{.*}}d0 66 02 00 00 00 00 {{.*}}jeq{{.*}}0
; CHECK-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}external_jump
; CHECK-NEXT: {{.*}}d0 66 00 {{[0-9a-f ]+}}{{.*}}jmp
; CHECK-NEXT: {{.*}}R_BEDROCK_BRDISP32S{{.*}}far_target

; CHECK-LABEL: <near_target>:
; CHECK-NEXT: {{.*}}02 {{.*}}ret

; CHECK-LABEL: <far_target>:
; CHECK-NEXT: {{.*}}02 {{.*}}ret
