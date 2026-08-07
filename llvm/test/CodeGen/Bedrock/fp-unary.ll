; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

declare float @llvm.fabs.f32(float)
declare double @llvm.fabs.f64(double)
declare float @llvm.sqrt.f32(float)
declare double @llvm.sqrt.f64(double)
declare float @llvm.roundeven.f32(float)
declare double @llvm.roundeven.f64(double)
declare float @llvm.trunc.f32(float)
declare double @llvm.trunc.f64(double)
declare float @llvm.ceil.f32(float)
declare double @llvm.ceil.f64(double)
declare float @llvm.floor.f32(float)
declare double @llvm.floor.f64(double)

define float @fabs_float(float %x) {
; CHECK-LABEL: fabs_float:
; CHECK: FABS.S
; OBJ-LABEL: <fabs_float>:
; OBJ: fabs.s
  %result = call float @llvm.fabs.f32(float %x)
  ret float %result
}

define double @fabs_double(double %x) {
; CHECK-LABEL: fabs_double:
; CHECK: FABS.D
; OBJ-LABEL: <fabs_double>:
; OBJ: fabs.d
  %result = call double @llvm.fabs.f64(double %x)
  ret double %result
}

define float @fneg_float(float %x) {
; CHECK-LABEL: fneg_float:
; CHECK: FNEG.S
; OBJ-LABEL: <fneg_float>:
; OBJ: fneg.s
  %result = fneg float %x
  ret float %result
}

define double @fneg_double(double %x) {
; CHECK-LABEL: fneg_double:
; CHECK: FNEG.D
; OBJ-LABEL: <fneg_double>:
; OBJ: fneg.d
  %result = fneg double %x
  ret double %result
}

define float @sqrt_float(float %x) {
; CHECK-LABEL: sqrt_float:
; CHECK: FSQRT.S
; OBJ-LABEL: <sqrt_float>:
; OBJ: fsqrt.s
  %result = call float @llvm.sqrt.f32(float %x)
  ret float %result
}

define double @sqrt_double(double %x) {
; CHECK-LABEL: sqrt_double:
; CHECK: FSQRT.D
; OBJ-LABEL: <sqrt_double>:
; OBJ: fsqrt.d
  %result = call double @llvm.sqrt.f64(double %x)
  ret double %result
}

define float @round_float(float %x) {
; CHECK-LABEL: round_float:
; CHECK: FROUND.S
  %result = call float @llvm.roundeven.f32(float %x)
  ret float %result
}

define double @round_double(double %x) {
; CHECK-LABEL: round_double:
; CHECK: FROUND.D
  %result = call double @llvm.roundeven.f64(double %x)
  ret double %result
}

define float @trunc_float(float %x) {
; CHECK-LABEL: trunc_float:
; CHECK: FTRUNC.S
  %result = call float @llvm.trunc.f32(float %x)
  ret float %result
}

define double @trunc_double(double %x) {
; CHECK-LABEL: trunc_double:
; CHECK: FTRUNC.D
  %result = call double @llvm.trunc.f64(double %x)
  ret double %result
}

define float @ceil_float(float %x) {
; CHECK-LABEL: ceil_float:
; CHECK: FCEIL.S
  %result = call float @llvm.ceil.f32(float %x)
  ret float %result
}

define double @ceil_double(double %x) {
; CHECK-LABEL: ceil_double:
; CHECK: FCEIL.D
  %result = call double @llvm.ceil.f64(double %x)
  ret double %result
}

define float @floor_float(float %x) {
; CHECK-LABEL: floor_float:
; CHECK: FFLOOR.S
  %result = call float @llvm.floor.f32(float %x)
  ret float %result
}

define double @floor_double(double %x) {
; CHECK-LABEL: floor_double:
; CHECK: FFLOOR.D
  %result = call double @llvm.floor.f64(double %x)
  ret double %result
}
