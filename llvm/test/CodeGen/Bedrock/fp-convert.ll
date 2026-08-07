; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

define float @signed_to_float(i64 %x) {
; CHECK-LABEL: signed_to_float:
; CHECK: FCVT.S	r0, f0
; OBJ-LABEL: <signed_to_float>:
; OBJ: fcvt.s	r0, f0
  %result = sitofp i64 %x to float
  ret float %result
}

define double @signed_to_double(i64 %x) {
; CHECK-LABEL: signed_to_double:
; CHECK: FCVT.D	r0, f0
  %result = sitofp i64 %x to double
  ret double %result
}

define float @unsigned_to_float(i64 %x) {
; CHECK-LABEL: unsigned_to_float:
; CHECK: FCVTU.S	r0, f0
  %result = uitofp i64 %x to float
  ret float %result
}

define double @unsigned_to_double(i64 %x) {
; CHECK-LABEL: unsigned_to_double:
; CHECK: FCVTU.D	r0, f0
; OBJ-LABEL: <unsigned_to_double>:
; OBJ: fcvtu.d	r0, f0
  %result = uitofp i64 %x to double
  ret double %result
}

define i64 @float_to_signed(float %x) {
; CHECK-LABEL: float_to_signed:
; CHECK: FCVT.S	f0, r0
; OBJ-LABEL: <float_to_signed>:
; OBJ: fcvt.s	f0, r0
  %result = fptosi float %x to i64
  ret i64 %result
}

define i64 @double_to_signed(double %x) {
; CHECK-LABEL: double_to_signed:
; CHECK: FCVT.D	f0, r0
  %result = fptosi double %x to i64
  ret i64 %result
}

define i64 @float_to_unsigned(float %x) {
; CHECK-LABEL: float_to_unsigned:
; CHECK: FCVTU.S	f0, r0
  %result = fptoui float %x to i64
  ret i64 %result
}

define i64 @double_to_unsigned(double %x) {
; CHECK-LABEL: double_to_unsigned:
; CHECK: FCVTU.D	f0, r0
; OBJ-LABEL: <double_to_unsigned>:
; OBJ: fcvtu.d	f0, r0
  %result = fptoui double %x to i64
  ret i64 %result
}

define i32 @double_to_signed_i32(double %x) {
; CHECK-LABEL: double_to_signed_i32:
; CHECK: FCVT.D	f0, r0
  %result = fptosi double %x to i32
  ret i32 %result
}

define i32 @float_to_unsigned_i32(float %x) {
; CHECK-LABEL: float_to_unsigned_i32:
; CHECK: FCVTU.S	f0, r0
  %result = fptoui float %x to i32
  ret i32 %result
}

define float @double_to_float(double %x) {
; CHECK-LABEL: double_to_float:
; CHECK: FCVT.S	f0, f0
; OBJ-LABEL: <double_to_float>:
; OBJ: fcvt.s	f0, f0
  %result = fptrunc double %x to float
  ret float %result
}

define double @float_to_double(float %x) {
; CHECK-LABEL: float_to_double:
; CHECK: FCVT.D	f0, f0
; OBJ-LABEL: <float_to_double>:
; OBJ: fcvt.d	f0, f0
  %result = fpext float %x to double
  ret double %result
}
