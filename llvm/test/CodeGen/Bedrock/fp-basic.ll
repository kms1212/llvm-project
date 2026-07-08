; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

define double @fp_arith(double %x, double %y) {
; CHECK-LABEL: fp_arith:
; CHECK: FMUL.D
; CHECK: FADD.D
; CHECK: FDIV.D
; CHECK: FSUB.D
; CHECK: ret
  %mul = fmul double %x, %y
  %add = fadd double %mul, 7.500000e-01
  %div = fdiv double %add, %y
  %sub = fsub double %div, %x
  ret double %sub
}

define double @int_to_double(i64 %x) {
; CHECK-LABEL: int_to_double:
; CHECK: FCVT
; CHECK: ret
  %fp = sitofp i64 %x to double
  ret double %fp
}
