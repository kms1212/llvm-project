# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -r --no-show-raw-insn %t.o | FileCheck %s

# CHECK-LABEL: <f>:
# CHECK-NEXT: 0:{{[ \t]+}}MOV.Q{{[ \t]+}}[4294967296], D0
# CHECK-NEXT: a:{{[ \t]+}}EXTZL.B{{[ \t]+}}[4294967296], D1
# CHECK-NEXT: 16:{{[ \t]+}}MOV.Q{{[ \t]+}}[0], D2
# CHECK-NEXT: {{[0-9a-f]+}}:{{[ \t]+}}R_BEDROCK_ABS64
# CHECK-NEXT: 20:{{[ \t]+}}MOV.B{{[ \t]+}}D0, [0]
# CHECK-NEXT: {{[0-9a-f]+}}:{{[ \t]+}}R_BEDROCK_ABS64
# CHECK-NEXT: 2c:{{[ \t]+}}INC.Q{{[ \t]+}}[0]
# CHECK-NEXT: {{[0-9a-f]+}}:{{[ \t]+}}R_BEDROCK_ABS64
# CHECK-NOT: HALT

.text
.globl f
f:
  MOV.Q [0x100000000], D0
  EXTZL.B [0x100000000], D1
  MOV.Q [foo@ABS64], D2
  MOV.B D0, [foo@ABS64]
  INC.Q [foo@ABS64]
foo:
  .byte 0
