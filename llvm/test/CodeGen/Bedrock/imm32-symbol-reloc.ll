; RUN: llc -mtriple=bedrock -filetype=obj -o %t.o < %s
; RUN: llvm-readobj -r %t.o | FileCheck %s
; RUN: llc -mtriple=bedrock -o %t.s < %s
; RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.parser.o %t.s
; RUN: llvm-readobj -r %t.parser.o | FileCheck %s

@payload = internal constant [1 x i8] zeroinitializer

define i64 @pack_payload_ptr() #0 {
entry:
  %p32 = ptrtoint ptr @payload to i32
  %p64 = zext i32 %p32 to i64
  %packed = or i64 %p64, 60129542144
  ret i64 %packed
}

attributes #0 = { minsize optsize }

; CHECK: R_BEDROCK_IMM32 .rodata 0x0
