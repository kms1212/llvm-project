; RUN: llc -mtriple=bedrock -filetype=obj -o %t.o < %s
; RUN: llvm-readobj -h -r %t.o | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=bedrock -o %t.s < %s
; RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.parser.o %t.s
; RUN: llvm-readobj -h -r %t.parser.o | FileCheck %s --check-prefix=OBJ

@g = global i64 5

declare i64 @callee(i64)

define i64 @caller(i64 %a) {
entry:
  %v = load i64, ptr @g, align 8
  %s = add i64 %v, %a
  %c = call i64 @callee(i64 %s)
  %r = add i64 %c, 1
  ret i64 %r
}

; OBJ: Machine: EM_BEDROCK (0xFFB0)
; OBJ: R_BEDROCK_ABS64 g 0x0
; OBJ: R_BEDROCK_PCREL16 callee 0x0
