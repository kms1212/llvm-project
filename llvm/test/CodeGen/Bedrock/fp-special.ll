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
declare float @llvm.ldexp.f32.i32(float, i32)
declare double @llvm.ldexp.f64.i32(double, i32)
declare float @llvm.ldexp.f32.i64(float, i64)
declare double @llvm.ldexp.f64.i64(double, i64)
declare { float, i32 } @llvm.frexp.f32.i32(float)
declare { double, i64 } @llvm.frexp.f64.i64(double)
declare i1 @llvm.is.fpclass.f32(float, i32 immarg)
declare i1 @llvm.is.fpclass.f64(double, i32 immarg)

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

define float @ldexp_s_i32(float %value, i32 %scale) {
; CHECK-LABEL: ldexp_s_i32:
; CHECK: FCVT.S{{[ \t]+}}r0, [[SCALE:f[0-9]+]]
; CHECK: FSCALE.S{{[ \t]+}}[[SCALE]], f0
; OBJ-LABEL: <ldexp_s_i32>:
; OBJ: c7 d7 28 21{{.*}}fcvt.s{{[ \t]+}}r0, [[OBJ_SCALE:f[0-9]+]]
; OBJ: c7 d6 28 01{{.*}}fscale.s{{[ \t]+}}[[OBJ_SCALE]], f0
  %result = call float @llvm.ldexp.f32.i32(float %value, i32 %scale)
  ret float %result
}

define double @ldexp_d_i32(double %value, i32 %scale) {
; CHECK-LABEL: ldexp_d_i32:
; CHECK: FCVT.D{{[ \t]+}}r0, [[SCALE:f[0-9]+]]
; CHECK: FSCALE.D{{[ \t]+}}[[SCALE]], f0
  %result = call double @llvm.ldexp.f64.i32(double %value, i32 %scale)
  ret double %result
}

define float @ldexp_s_i64(float %value, i64 %scale) {
; CHECK-LABEL: ldexp_s_i64:
; CHECK: FCVT.S{{[ \t]+}}r0, [[SCALE:f[0-9]+]]
; CHECK: FSCALE.S{{[ \t]+}}[[SCALE]], f0
  %result = call float @llvm.ldexp.f32.i64(float %value, i64 %scale)
  ret float %result
}

define double @ldexp_d_i64(double %value, i64 %scale) {
; CHECK-LABEL: ldexp_d_i64:
; CHECK: FCVT.D{{[ \t]+}}r0, [[SCALE:f[0-9]+]]
; CHECK: FSCALE.D{{[ \t]+}}[[SCALE]], f0
  %result = call double @llvm.ldexp.f64.i64(double %value, i64 %scale)
  ret double %result
}

define { float, i32 } @frexp_s_i32(float %value) {
; CHECK-LABEL: frexp_s_i32:
; CHECK-NOT: call{{.*}}frexp
; CHECK-NOT: FSCALE
; CHECK: fclass.s{{[ \t]+}}f0,
; CHECK-NOT: FSCALE
; CHECK: FGETEXP.S
; CHECK-NOT: FSCALE
; CHECK: FGETMAN.S
; CHECK-NOT: FSCALE
; OBJ-LABEL: <frexp_s_i32>:
; OBJ: fclass.s
; OBJ: fgetexp.s
; OBJ: fgetman.s
  %result = call { float, i32 } @llvm.frexp.f32.i32(float %value)
  ret { float, i32 } %result
}

define { double, i64 } @frexp_d_i64(double %value) {
; CHECK-LABEL: frexp_d_i64:
; CHECK-NOT: call{{.*}}frexp
; CHECK-NOT: FSCALE
; CHECK: fclass.d{{[ \t]+}}f0,
; CHECK-NOT: FSCALE
; CHECK: FGETEXP.D
; CHECK-NOT: FSCALE
; CHECK: FGETMAN.D
; CHECK-NOT: FSCALE
; OBJ-LABEL: <frexp_d_i64>:
; OBJ: fclass.d
; OBJ: fgetexp.d
; OBJ: fgetman.d
  %result = call { double, i64 } @llvm.frexp.f64.i64(double %value)
  ret { double, i64 } %result
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

define double @negative_zero_d() {
; CHECK-LABEL: negative_zero_d:
; CHECK: FMOVCR.D{{[ \t]+}}1, f0
; OBJ-LABEL: <negative_zero_d>:
; OBJ: cf d6 b1 00 01 00{{.*}}fmovcr.d{{[ \t]+}}1, f0
  ret double -0.0
}

define double @positive_one_d() {
; CHECK-LABEL: positive_one_d:
; CHECK: FMOVCR.D{{[ \t]+}}2, f0
; OBJ-LABEL: <positive_one_d>:
; OBJ: cf d6 b1 00 02 00{{.*}}fmovcr.d{{[ \t]+}}2, f0
  ret double 1.0
}

define double @pi_d() {
; CHECK-LABEL: pi_d:
; CHECK: FMOVCR.D{{[ \t]+}}16, f0
; OBJ-LABEL: <pi_d>:
; OBJ: cf d6 b1 00 10 00{{.*}}fmovcr.d{{[ \t]+}}16, f0
  ret double 0x400921FB54442D18
}

define i1 @is_nan_s(float %value) {
; CHECK-LABEL: is_nan_s:
; CHECK: fclass.s{{[ \t]+}}f0, [[CLASS:r[0-9]+]]
; CHECK-NOT: fcmp
; OBJ-LABEL: <is_nan_s>:
; OBJ: c7 d7 00 20{{.*}}fclass.s{{[ \t]+}}f0,
  %result = call i1 @llvm.is.fpclass.f32(float %value, i32 3)
  ret i1 %result
}

define i1 @is_finite_d(double %value) {
; CHECK-LABEL: is_finite_d:
; CHECK: fclass.d{{[ \t]+}}f0, [[CLASS:r[0-9]+]]
; CHECK-NOT: fcmp
  %result = call i1 @llvm.is.fpclass.f64(double %value, i32 504)
  ret i1 %result
}

define float @negative_zero_s() {
; CHECK-LABEL: negative_zero_s:
; CHECK-NOT: FCLR
; CHECK: FMOV.S
  ret float -0.0
}
