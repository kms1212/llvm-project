; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.o
; RUN: llvm-dwarfdump --eh-frame %t.o | FileCheck %s

.text
.globl dwarf_registers
.type dwarf_registers,@function
dwarf_registers:
.cfi_startproc
.cfi_def_cfa sp, 16
.cfi_offset r0, -8
.cfi_offset r15, -16
.cfi_offset f0, -24
.cfi_offset f15, -32
.cfi_offset pc, -40
.cfi_offset flags, -48
.cfi_offset status, -56
.cfi_offset cs, -64
.cfi_offset ds, -72
.cfi_offset ss, -80
.cfi_offset gs0, -88
.cfi_offset gs4, -96
.cfi_offset fstatus, -104
.cfi_offset fflags, -112
ret
.cfi_endproc

; CHECK: Return address column: 33
; CHECK: DW_CFA_def_cfa: SP +16
; CHECK-NEXT: DW_CFA_offset: R0 -8
; CHECK-NEXT: DW_CFA_offset: R15 -16
; CHECK-NEXT: DW_CFA_offset: F0 -24
; CHECK-NEXT: DW_CFA_offset: F15 -32
; CHECK-NEXT: DW_CFA_offset: PC -40
; CHECK-NEXT: DW_CFA_offset: FLAGS -48
; CHECK-NEXT: DW_CFA_offset: STATUS -56
; CHECK-NEXT: DW_CFA_offset: CS -64
; CHECK-NEXT: DW_CFA_offset: DS -72
; CHECK-NEXT: DW_CFA_offset: SS -80
; CHECK-NEXT: DW_CFA_offset: GS0 -88
; CHECK-NEXT: DW_CFA_offset: GS4 -96
; CHECK-NEXT: DW_CFA_offset: FSTATUS -104
; CHECK-NEXT: DW_CFA_offset: FFLAGS -112
