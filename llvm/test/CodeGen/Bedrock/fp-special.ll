; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs -filetype=obj < %s \
; RUN:   -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

declare float @llvm.copysign.f32(float, float)
declare double @llvm.copysign.f64(double, double)
declare float @llvm.minimumnum.f32(float, float)
declare double @llvm.minimumnum.f64(double, double)
declare float @llvm.maximumnum.f32(float, float)
declare double @llvm.maximumnum.f64(double, double)
declare float @llvm.rint.f32(float)
declare double @llvm.rint.f64(double)

define float @frem_s(float %lhs, float %rhs) {
; CHECK-LABEL: frem_s:
; CHECK: FMOD.S{{[ \t]+}}f1, f0
; OBJ-LABEL: <frem_s>:
; OBJ: c7 d6 18 01{{.*}}fmod.s{{[ \t]+}}f1, f0
  %result = frem float %lhs, %rhs
  ret float %result
}

define double @frem_d(double %lhs, double %rhs) {
; CHECK-LABEL: frem_d:
; CHECK: FMOD.D{{[ \t]+}}f1, f0
; OBJ-LABEL: <frem_d>:
; OBJ: c7 d6 98 01{{.*}}fmod.d{{[ \t]+}}f1, f0
  %result = frem double %lhs, %rhs
  ret double %result
}

define float @copysign_s(float %magnitude, float %sign) {
; CHECK-LABEL: copysign_s:
; CHECK: FCOPYSIGN.S{{[ \t]+}}f1, f0, f0
; OBJ-LABEL: <copysign_s>:
; OBJ: c7 d7 08 30{{.*}}fcopysign.s{{[ \t]+}}f1, f0, f0
  %result = call float @llvm.copysign.f32(float %magnitude, float %sign)
  ret float %result
}

define double @copysign_d(double %magnitude, double %sign) {
; CHECK-LABEL: copysign_d:
; CHECK: FCOPYSIGN.D{{[ \t]+}}f1, f0, f0
; OBJ-LABEL: <copysign_d>:
; OBJ: c7 d7 88 30{{.*}}fcopysign.d{{[ \t]+}}f1, f0, f0
  %result = call double @llvm.copysign.f64(double %magnitude, double %sign)
  ret double %result
}

define float @minimumnum_s(float %lhs, float %rhs) {
; CHECK-LABEL: minimumnum_s:
; CHECK: FMIN.S{{[ \t]+}}f1, f0
; OBJ-LABEL: <minimumnum_s>:
; OBJ: c2 50 e0{{.*}}fmin.s{{[ \t]+}}f1, f0
  %result = call float @llvm.minimumnum.f32(float %lhs, float %rhs)
  ret float %result
}

define double @minimumnum_d(double %lhs, double %rhs) {
; CHECK-LABEL: minimumnum_d:
; CHECK: FMIN.D{{[ \t]+}}f1, f0
; OBJ-LABEL: <minimumnum_d>:
; OBJ: c2 58 e0{{.*}}fmin.d{{[ \t]+}}f1, f0
  %result = call double @llvm.minimumnum.f64(double %lhs, double %rhs)
  ret double %result
}

define float @maximumnum_s(float %lhs, float %rhs) {
; CHECK-LABEL: maximumnum_s:
; CHECK: FMAX.S{{[ \t]+}}f1, f0
; OBJ-LABEL: <maximumnum_s>:
; OBJ: c2 50 f0{{.*}}fmax.s{{[ \t]+}}f1, f0
  %result = call float @llvm.maximumnum.f32(float %lhs, float %rhs)
  ret float %result
}

define double @maximumnum_d(double %lhs, double %rhs) {
; CHECK-LABEL: maximumnum_d:
; CHECK: FMAX.D{{[ \t]+}}f1, f0
; OBJ-LABEL: <maximumnum_d>:
; OBJ: c2 58 f0{{.*}}fmax.d{{[ \t]+}}f1, f0
  %result = call double @llvm.maximumnum.f64(double %lhs, double %rhs)
  ret double %result
}

define float @rint_s(float %value) {
; CHECK-LABEL: rint_s:
; CHECK: FINT.S{{[ \t]+}}f0, f0
; OBJ-LABEL: <rint_s>:
; OBJ: c7 d5 78 00{{.*}}fint.s{{[ \t]+}}f0, f0
  %result = call float @llvm.rint.f32(float %value)
  ret float %result
}

define double @rint_d(double %value) {
; CHECK-LABEL: rint_d:
; CHECK: FINT.D{{[ \t]+}}f0, f0
; OBJ-LABEL: <rint_d>:
; OBJ: c7 d5 f8 00{{.*}}fint.d{{[ \t]+}}f0, f0
  %result = call double @llvm.rint.f64(double %value)
  ret double %result
}

define float @positive_zero_s() {
; CHECK-LABEL: positive_zero_s:
; CHECK: FCLR{{[ \t]+}}f0
; OBJ-LABEL: <positive_zero_s>:
; OBJ: c2 40 80{{.*}}fclr{{[ \t]+}}f0
  ret float 0.0
}

define double @positive_zero_d() {
; CHECK-LABEL: positive_zero_d:
; CHECK: FCLR{{[ \t]+}}f0
; OBJ-LABEL: <positive_zero_d>:
; OBJ: c2 40 80{{.*}}fclr{{[ \t]+}}f0
  ret double 0.0
}

define float @negative_zero_s() {
; CHECK-LABEL: negative_zero_s:
; CHECK-NOT: FCLR
; CHECK: FMOV.S
  ret float -0.0
}
