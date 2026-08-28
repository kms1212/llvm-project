; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -filetype=asm %s | FileCheck %s --check-prefix=ASM
; RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.o
; RUN: llvm-nm --format=posix %t.o | FileCheck %s --check-prefix=SYMBOLS

.text
.globl `r1`
.type `r1`, @function
`r1`:
  nop

.globl ordinary
.type ordinary, @function
ordinary:
  call `r1`
  mov.q [`r1`], r2
  mov.q [r1], r2
  lea.q `r1`@abs64, r3

.globl `tick\`slash\\name`
`tick\`slash\\name`:
  ret

; ASM: .globl `r1`
; ASM: .type `r1`,@function
; ASM: `r1`:
; ASM: ordinary:
; ASM: call 0
; ASM: mov.q [0], r2
; ASM: mov.q [r1], r2
; ASM: `tick\`slash\\name`:

; SYMBOLS: ordinary
; SYMBOLS: r1
; SYMBOLS: tick`slash\name
