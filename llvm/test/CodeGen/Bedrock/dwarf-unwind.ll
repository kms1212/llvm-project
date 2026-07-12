; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=bedrock -frame-pointer=all -verify-machineinstrs < %s | FileCheck %s --check-prefix=FP
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-dwarfdump --eh-frame %t.o | FileCheck %s --check-prefix=CFI

declare void @escape(ptr)

define i64 @near(i64 %x) uwtable {
; ASM-LABEL: near:
; ASM: .cfi_def_cfa sp, 8
; ASM-NEXT: .cfi_offset pc, -8
; ASM: sub.q 16, sp
; ASM-NEXT: .cfi_def_cfa_offset 24
; ASM: add.q 16, sp
; ASM-NEXT: .cfi_def_cfa_offset 8
; ASM-NEXT: ret
;
; FP-LABEL: near:
; FP: .cfi_def_cfa sp, 8
; FP: mov.q r15, [sp + 8]
; FP-NEXT: .cfi_offset r15, -16
; FP: mov.q sp, r15
; FP-NEXT: .cfi_def_cfa r15, 24
; FP: mov.q r15, sp
; FP-NEXT: .cfi_def_cfa sp, 24
entry:
  %slot = alloca i64, align 16
  store volatile i64 %x, ptr %slot, align 16
  %v = load volatile i64, ptr %slot, align 16
  ret i64 %v
}

define bedrock_farcc i64 @far(i64 %x) addrspace(1) uwtable {
; ASM-LABEL: far:
; ASM: .cfi_def_cfa sp, 16
; ASM-NEXT: .cfi_offset pc, -16
; ASM-NEXT: .cfi_offset cs, -8
; ASM: sub.q 16, sp
; ASM-NEXT: .cfi_def_cfa_offset 32
; ASM: add.q 16, sp
; ASM-NEXT: .cfi_def_cfa_offset 16
; ASM-NEXT: lret
;
; FP-LABEL: far:
; FP: .cfi_def_cfa sp, 16
; FP: mov.q r15, [sp + 8]
; FP-NEXT: .cfi_offset r15, -24
; FP: mov.q sp, r15
; FP-NEXT: .cfi_def_cfa r15, 32
entry:
  %slot = alloca i64, align 16
  store volatile i64 %x, ptr %slot, align 16
  %v = load volatile i64, ptr %slot, align 16
  ret i64 %v
}

define void @realign(i64 %x) uwtable {
; ASM-LABEL: realign:
; ASM: mov.q r14, [sp + 24]
; ASM-NEXT: mov.q r15, [sp + 16]
; ASM: .cfi_offset r14, -16
; ASM-NEXT: .cfi_offset r15, -24
; ASM: mov.q sp, r15
; ASM-NEXT: .cfi_def_cfa r15, 40
; ASM-NEXT: mov.q sp, r14
; ASM-NEXT: and.q -32, r14
; ASM-NEXT: mov.q r14, sp
; ASM: mov.q r0, [r14]
; ASM: mov.q r15, sp
; ASM-NEXT: .cfi_def_cfa sp, 40
entry:
  %slot = alloca i64, align 32
  store volatile i64 %x, ptr %slot, align 32
  call void @escape(ptr %slot)
  ret void
}

define void @dynamic(i64 %n) uwtable {
; ASM-LABEL: dynamic:
; ASM: .cfi_def_cfa r15, 40
; ASM: and.q -32, r14
; ASM: mov.q sp, r0
; ASM-NEXT: sub.q r1, r0
; ASM-NEXT: and.q -32, r0
; ASM-NEXT: mov.q r0, sp
; ASM: mov.q r15, sp
; ASM-NEXT: .cfi_def_cfa sp, 40
entry:
  %slot = alloca i8, i64 %n, align 32
  call void @escape(ptr %slot)
  ret void
}

; CFI: Return address column: 33
; CFI: DW_CFA_def_cfa: SP +8
; CFI-NEXT: DW_CFA_offset: PC -8
; CFI: CFA=SP+8: PC=[CFA-8]
; CFI: DW_CFA_def_cfa: SP +16
; CFI-NEXT: DW_CFA_offset: PC -16
; CFI-NEXT: DW_CFA_offset: CS -8
; CFI: CFA=SP+16: PC=[CFA-16], CS=[CFA-8]
