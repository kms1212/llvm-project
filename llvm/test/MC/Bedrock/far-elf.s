; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -mattr=+far-elf -filetype=obj %s -o %t.o
; RUN: llvm-readobj --sections --relocations --symbols --notes %t.o | FileCheck %s
; RUN: not llvm-mc -triple=bedrock -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOFEATURE

.data
.balign 16
.globl object
.type object,@object
object:
  .quad 0
.size object,8

.section .far,"aw",@progbits
.farptr object+8
.bedrock_segdomain 3, 7, 0
.bedrock_far_func far_entry
.bedrock_far_ifunc far_resolver

.text
.globl far_entry
far_entry:
  nop
.globl far_resolver
far_resolver:
  nop

; CHECK: Name: .far
; CHECK: AddressAlignment: 16
; CHECK: Name: .note.bedrock
; CHECK: Type: SHT_NOTE
; CHECK: Size: 28
; CHECK: Name: .bedrock.segdomains
; CHECK: Flags [ (0x0)
; CHECK: EntrySize: 16
; CHECK: 0x0 R_BEDROCK_FAR_ADDR64 object 0x8
; CHECK-NEXT: 0x8 R_BEDROCK_FAR_SEGMENT64 object 0x0
; CHECK: Name: far_entry
; CHECK: Type: BEDROCK_FAR_FUNC (0xD)
; CHECK: Name: far_resolver
; CHECK: Type: BEDROCK_FAR_IFUNC (0xE)
; CHECK: Owner: BEDROCK
; CHECK: Data size: 0x8
; CHECK: 0000: 01000000 00000000

; NOFEATURE: error: .farptr requires the +far-elf target feature
