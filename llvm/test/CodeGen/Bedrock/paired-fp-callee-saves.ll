; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

declare void @clobber()
declare void @consume(double, double, double, double, double, double, double,
                      double)

define double @preserve_eight(double %a0, double %a1, double %a2, double %a3,
                              double %a4, double %a5, double %a6,
                              double %a7) uwtable {
; CHECK-LABEL: preserve_eight:
; CHECK: fpushp 0
; CHECK: .cfi_def_cfa_offset 24
; CHECK: .cfi_offset f14, -16
; CHECK: .cfi_offset f15, -24
; CHECK: fpushp 1
; CHECK: .cfi_def_cfa_offset 40
; CHECK: .cfi_offset f12, -32
; CHECK: .cfi_offset f13, -40
; CHECK: fpushp 2
; CHECK: .cfi_def_cfa_offset 56
; CHECK: .cfi_offset f10, -48
; CHECK: .cfi_offset f11, -56
; CHECK: fpushp 3
; CHECK: .cfi_def_cfa_offset 72
; CHECK: .cfi_offset f8, -64
; CHECK: .cfi_offset f9, -72
; CHECK: call clobber
; CHECK: fpopp 3
; CHECK: fpopp 2
; CHECK: fpopp 1
; CHECK: fpopp 0
; CHECK: ret
  call void @clobber()
  call void @consume(double %a0, double %a1, double %a2, double %a3,
                     double %a4, double %a5, double %a6, double %a7)
  ret double %a0
}

define double @preserve_one(double %value) uwtable {
; CHECK-LABEL: preserve_one:
; CHECK: fpushp 3
; CHECK: .cfi_def_cfa_offset 24
; CHECK: .cfi_offset f8, -16
; CHECK: .cfi_offset f9, -24
; CHECK: call clobber
; CHECK: fpopp 3
; CHECK: .cfi_restore f8
; CHECK: .cfi_restore f9
; CHECK: ret
  call void @clobber()
  ret double %value
}
