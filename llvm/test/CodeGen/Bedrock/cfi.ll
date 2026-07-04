; RUN: llc -mtriple=bedrock-unknown-unknown -O0 < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=bedrock-unknown-unknown -O2 < %s | FileCheck %s --check-prefixes=CHECK,O2

target triple = "bedrock-unknown-unknown"

declare void @use(ptr)

define i64 @leaf(i64 %x) uwtable {
; CHECK-LABEL: leaf:
; CHECK-NEXT: .cfi_startproc
; CHECK: RET
; CHECK: .cfi_endproc
entry:
  ret i64 %x
}

define void @many_csr(ptr %p) uwtable {
; CHECK-LABEL: many_csr:
; CHECK-NEXT: .cfi_startproc
; O0: SUB.Q 40, SP
; O0-NEXT: .cfi_def_cfa_offset 48
; O0: MOV.Q D6, [SP + 32]
; O0: MOV.Q D7, [SP + 24]
; O0: MOV.Q A6, [SP + 16]
; O0: MOV.Q A7, [SP + 8]
; O2: PUSHM {D6,D7,A6,A7}
; O2-NEXT: .cfi_def_cfa_offset 48
; CHECK: .cfi_offset D6, -16
; CHECK-NEXT: .cfi_offset D7, -24
; CHECK-NEXT: .cfi_offset A6, -32
; CHECK-NEXT: .cfi_offset A7, -40
; O0: CALL use@PCREL16
; O0: MOV.Q [SP + 8], A7
; O0: MOV.Q [SP + 16], A6
; O0: MOV.Q [SP + 24], D7
; O0: MOV.Q [SP + 32], D6
; O0: ADD.Q 40, SP
; O0: RET
; O2: POPM {D6,D7,A6,A7}
; O2-NEXT: JMP.W use@WORD_PCREL16
; CHECK: .cfi_endproc
entry:
  call void asm sideeffect "", "~{d6},~{d7},~{a6},~{a7}"()
  call void @use(ptr %p)
  ret void
}

define i64 @with_fp(ptr %p) uwtable "frame-pointer"="all" {
; CHECK-LABEL: with_fp:
; CHECK-NEXT: .cfi_startproc
; CHECK: SUB.Q 40, SP
; CHECK-NEXT: .cfi_def_cfa_offset 48
; CHECK: MOV.Q A7, [SP + 0]
; CHECK-NEXT: .cfi_offset A7, -48
; CHECK-NEXT: MOV.Q SP, A7
; CHECK-NEXT: .cfi_def_cfa A7, 48
; CHECK: CALL use@PCREL16
; CHECK: MOV.Q A7, SP
; CHECK-NEXT: MOV.Q [SP + 0], A7
; CHECK-NEXT: ADD.Q 40, SP
; CHECK: RET
; CHECK: .cfi_endproc
entry:
  %slot = alloca i64, align 8
  store i64 42, ptr %slot, align 8
  call void @use(ptr %slot)
  %v = load i64, ptr %slot, align 8
  ret i64 %v
}
