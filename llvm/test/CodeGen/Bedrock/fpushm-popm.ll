; RUN: llc -mtriple=bedrock-unknown-unknown -O1 < %s | FileCheck %s

target triple = "bedrock-unknown-unknown"

declare void @use(double)

define void @save_fcsr(double %x) nounwind {
; CHECK-LABEL: save_fcsr:
; CHECK: FPUSHM
; CHECK-NOT: FMOV.D{{[[:space:]]+}}F8,
; CHECK: FPOPM
; CHECK: RET
entry:
  call void asm sideeffect "", "~{f8},~{f9},~{f10},~{f11},~{f12},~{f13},~{f14},~{f15}"()
  call void @use(double %x)
  ret void
}

define void @save_fcsr_cfi(double %x) nounwind uwtable {
; CHECK-LABEL: save_fcsr_cfi:
; CHECK: .cfi_startproc
; CHECK: FPUSHM
; CHECK: .cfi_def_cfa_offset
; CHECK: .cfi_offset F8
; CHECK: FPOPM
; CHECK: .cfi_endproc
entry:
  call void asm sideeffect "", "~{f8},~{f9},~{f10},~{f11},~{f12},~{f13},~{f14},~{f15}"()
  call void @use(double %x)
  ret void
}

define void @save_mixed_csr(double %x) nounwind {
; CHECK-LABEL: save_mixed_csr:
; CHECK: PUSHM
; CHECK: FPUSHM
; CHECK: FPOPM
; CHECK: POPM
; CHECK: RET
entry:
  call void asm sideeffect "", "~{d6},~{d7},~{a6},~{a7},~{f8},~{f9},~{f10},~{f11},~{f12},~{f13},~{f14},~{f15}"()
  call void @use(double %x)
  ret void
}
