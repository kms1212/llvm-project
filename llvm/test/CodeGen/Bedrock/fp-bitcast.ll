; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t
; RUN: llvm-objdump -d %t | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

define i64 @double_bits(double %value) {
; CHECK-LABEL: double_bits:
; CHECK: FMOV.D f0, [sp + {{[0-9]+}}]
; CHECK: mov.q [sp + {{[0-9]+}}], r0
; OBJ-LABEL: <double_bits>:
; OBJ: fmov.d f0, [sp + {{[0-9]+}}]
; OBJ: mov.q [sp + {{[0-9]+}}], r0
  %bits = bitcast double %value to i64
  ret i64 %bits
}

define double @double_from_bits(i64 %bits) {
; CHECK-LABEL: double_from_bits:
; CHECK: mov.q r0, [sp + {{[0-9]+}}]
; CHECK: FMOV.D [sp + {{[0-9]+}}], f0
; OBJ-LABEL: <double_from_bits>:
; OBJ: mov.q r0, [sp + {{[0-9]+}}]
; OBJ: fmov.d [sp + {{[0-9]+}}], f0
  %value = bitcast i64 %bits to double
  ret double %value
}

define i32 @float_bits(float %value) {
; CHECK-LABEL: float_bits:
; CHECK: FMOV.S f0, [sp + {{[0-9]+}}]
; CHECK: mov.l [sp + {{[0-9]+}}], r0
; OBJ-LABEL: <float_bits>:
; OBJ: fmov.s f0, [sp + {{[0-9]+}}]
; OBJ: mov.l [sp + {{[0-9]+}}], r0
  %bits = bitcast float %value to i32
  ret i32 %bits
}

define float @float_from_bits(i32 %bits) {
; CHECK-LABEL: float_from_bits:
; CHECK: mov.l r0, [sp + {{[0-9]+}}]
; CHECK: FMOV.S [sp + {{[0-9]+}}], f0
; OBJ-LABEL: <float_from_bits>:
; OBJ: mov.l r0, [sp + {{[0-9]+}}]
; OBJ: fmov.s [sp + {{[0-9]+}}], f0
  %value = bitcast i32 %bits to float
  ret float %value
}

define void @fpu_store_sp_zero(double %value) {
; CHECK-LABEL: fpu_store_sp_zero:
; CHECK: FMOV.D f0, [sp]
; OBJ-LABEL: <fpu_store_sp_zero>:
; OBJ: cb e4 f0 50 00{{.*}}fmov.d f0, [sp + 0]
  %slots = alloca [2 x double], align 16
  %slot = getelementptr inbounds [2 x double], ptr %slots, i64 0, i64 0
  store volatile double %value, ptr %slot, align 8
  ret void
}

define double @fpu_load_sp_zero() {
; CHECK-LABEL: fpu_load_sp_zero:
; CHECK: FMOV.D [sp], f0
; OBJ-LABEL: <fpu_load_sp_zero>:
; OBJ: cb e5 d8 50 00{{.*}}fmov.d [sp + 0], f0
  %slots = alloca [2 x double], align 16
  %slot = getelementptr inbounds [2 x double], ptr %slots, i64 0, i64 0
  %value = load volatile double, ptr %slot, align 8
  ret double %value
}
