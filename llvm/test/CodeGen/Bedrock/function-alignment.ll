; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=bedrock -filetype=obj %s -o %t.o
; RUN: llvm-readobj --sections --symbols %t.o | FileCheck %s --check-prefix=OBJ

; ASM: .globl first
; ASM-NEXT: .p2align 1
; ASM: .globl second
; ASM-NEXT: .p2align 1
; ASM: .globl third
; ASM-NEXT: .p2align 1

; OBJ: Name: .text
; OBJ: AddressAlignment: 4
; OBJ: Name: first
; OBJ: Value: 0x0
; OBJ: Name: second
; OBJ: Value: 0x4
; OBJ: Name: third
; OBJ: Value: 0xA

define i32 @first() {
  ret i32 1
}

define i32 @second() {
  ret i32 2
}

define i32 @third() {
  ret i32 3
}
