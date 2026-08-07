; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

define double @fp_arith(double %x, double %y) {
; CHECK-LABEL: fp_arith:
; CHECK: FMUL.D
; CHECK: FADD.D
; CHECK: FDIV.D
; CHECK: FSUB.D
; CHECK: ret
; OBJ-LABEL: <fp_arith>:
; OBJ: c2 58 92{{.*}}fmul.d
; OBJ: c2 49 f2{{.*}}fadd.d
; OBJ: c2 58 a2{{.*}}fdiv.d
; OBJ: c2 58 02{{.*}}fsub.d
  %mul = fmul double %x, %y
  %add = fadd double %mul, 7.500000e-01
  %div = fdiv double %add, %y
  %sub = fsub double %div, %x
  ret double %sub
}

define float @fp_arith_float(float %x, float %y) {
; CHECK-LABEL: fp_arith_float:
; CHECK: FMUL.S
; CHECK: FADD.S
; CHECK: FDIV.S
; CHECK: FSUB.S
; CHECK: ret
; OBJ-LABEL: <fp_arith_float>:
; OBJ: c2 50 92{{.*}}fmul.s
; OBJ: c2 41 f2{{.*}}fadd.s
; OBJ: c2 50 a2{{.*}}fdiv.s
; OBJ: c2 50 02{{.*}}fsub.s
  %mul = fmul float %x, %y
  %add = fadd float %mul, 7.500000e-01
  %div = fdiv float %add, %y
  %sub = fsub float %div, %x
  ret float %sub
}

define double @int_to_double(i64 %x) {
; CHECK-LABEL: int_to_double:
; CHECK: FCVT
; CHECK: ret
  %fp = sitofp i64 %x to double
  ret double %fp
}
