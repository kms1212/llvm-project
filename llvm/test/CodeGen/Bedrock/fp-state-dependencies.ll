; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -mattr=+fptransa -O2 -verify-machineinstrs \
; RUN:   -stop-after=finalize-isel < %s | FileCheck %s

target triple = "bedrock"

declare i64 @llvm.bedrock.read.fstatus()
declare void @llvm.bedrock.write.fstatus(i64)
declare i64 @llvm.bedrock.read.fflags()
declare void @llvm.bedrock.write.fflags(i64)
declare float @llvm.sqrt.f32(float)
declare float @llvm.fabs.f32(float)
declare float @llvm.fma.f32(float, float, float)
declare float @llvm.bedrock.fetoxa.f32(float)
declare float @llvm.experimental.constrained.fadd.f32(float, float, metadata,
                                                       metadata)
declare float @llvm.experimental.constrained.fsub.f32(float, float, metadata,
                                                       metadata)
declare float @llvm.experimental.constrained.fmul.f32(float, float, metadata,
                                                       metadata)
declare float @llvm.experimental.constrained.fdiv.f32(float, float, metadata,
                                                       metadata)
declare float @llvm.experimental.constrained.frem.f32(float, float, metadata,
                                                       metadata)
declare float @llvm.experimental.constrained.sqrt.f32(float, metadata,
                                                       metadata)
declare float @llvm.experimental.constrained.fma.f32(float, float, float,
                                                      metadata, metadata)
declare float @llvm.experimental.constrained.sitofp.f32.i64(i64, metadata,
                                                             metadata)
declare i64 @llvm.experimental.constrained.fptosi.i64.f32(float, metadata)
declare float @llvm.experimental.constrained.fptrunc.f32.f64(double, metadata,
                                                              metadata)
declare double @llvm.experimental.constrained.fpext.f64.f32(float, metadata)
declare float @llvm.experimental.constrained.roundeven.f32(float, metadata)
declare float @llvm.experimental.constrained.ldexp.f32.i32(float, i32,
                                                            metadata,
                                                            metadata)
declare double @llvm.ldexp.f64.i64(double, i64)
declare i1 @llvm.experimental.constrained.fcmp.f32(float, float, metadata,
                                                    metadata)
declare i1 @llvm.experimental.constrained.fcmps.f32(float, float, metadata,
                                                     metadata)
declare i1 @llvm.experimental.constrained.fcmps.f64(double, double, metadata,
                                                     metadata)

define i64 @state_access(i64 %status, i64 %flags) {
; CHECK-LABEL: name: state_access
; CHECK: BEDROCK_WRFSTATUS {{.*}}, implicit-def dead $fstatus
; CHECK: BEDROCK_WRFFLAGS {{.*}}, implicit-def dead $fflags
; CHECK: BEDROCK_RDFSTATUS implicit $fstatus
; CHECK: BEDROCK_RDFFLAGS implicit $fflags
  call void @llvm.bedrock.write.fstatus(i64 %status)
  call void @llvm.bedrock.write.fflags(i64 %flags)
  %read_status = call i64 @llvm.bedrock.read.fstatus()
  %read_flags = call i64 @llvm.bedrock.read.fflags()
  %result = xor i64 %read_status, %read_flags
  ret i64 %result
}

define float @arithmetic(float %lhs, float %rhs) {
; CHECK-LABEL: name: arithmetic
; CHECK: FADDSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = fadd float %lhs, %rhs
  ret float %result
}

define float @strict_arithmetic(float %lhs, float %rhs) strictfp {
; CHECK-LABEL: name: strict_arithmetic
; CHECK-NOT: nofpexcept
; CHECK: FADDSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.experimental.constrained.fadd.f32(
      float %lhs, float %rhs, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  ret float %result
}

define float @strict_binary_chain(float %lhs, float %rhs) strictfp {
; CHECK-LABEL: name: strict_binary_chain
; CHECK-NOT: nofpexcept
; CHECK: FSUBSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK: FMULSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK: FDIVSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK: FMODSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %sub = call float @llvm.experimental.constrained.fsub.f32(
      float %lhs, float %rhs, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  %mul = call float @llvm.experimental.constrained.fmul.f32(
      float %sub, float %rhs, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  %div = call float @llvm.experimental.constrained.fdiv.f32(
      float %mul, float %rhs, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  %result = call float @llvm.experimental.constrained.frem.f32(
      float %div, float %rhs, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  ret float %result
}

define float @strict_sqrt(float %value) strictfp {
; CHECK-LABEL: name: strict_sqrt
; CHECK-NOT: nofpexcept
; CHECK: BEDROCK_FSQRT_S {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.experimental.constrained.sqrt.f32(
      float %value, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %result
}

define float @strict_fma(float %lhs, float %rhs, float %acc) strictfp {
; CHECK-LABEL: name: strict_fma
; CHECK-NOT: nofpexcept
; CHECK: BEDROCK_FMADD_S {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.experimental.constrained.fma.f32(
      float %lhs, float %rhs, float %acc, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  ret float %result
}

define float @strict_int_to_fp(i64 %value) strictfp {
; CHECK-LABEL: name: strict_int_to_fp
; CHECK-NOT: nofpexcept
; CHECK: FCVTSQSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.experimental.constrained.sitofp.f32.i64(
      i64 %value, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %result
}

define i64 @strict_fp_to_int(float %value) strictfp {
; CHECK-LABEL: name: strict_fp_to_int
; CHECK-NOT: nofpexcept
; CHECK: FCVTStoQrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call i64 @llvm.experimental.constrained.fptosi.i64.f32(
      float %value, metadata !"fpexcept.strict")
  ret i64 %result
}

define float @strict_narrow(double %value) strictfp {
; CHECK-LABEL: name: strict_narrow
; CHECK-NOT: nofpexcept
; CHECK: FCVTDtoSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.experimental.constrained.fptrunc.f32.f64(
      double %value, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %result
}

define double @strict_extend(float %value) strictfp {
; CHECK-LABEL: name: strict_extend
; CHECK-NOT: nofpexcept
; CHECK: FCVTStoDrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call double @llvm.experimental.constrained.fpext.f64.f32(
      float %value, metadata !"fpexcept.strict")
  ret double %result
}

define float @strict_round(float %value) strictfp {
; CHECK-LABEL: name: strict_round
; CHECK-NOT: nofpexcept
; CHECK: BEDROCK_FROUND_S {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.experimental.constrained.roundeven.f32(
      float %value, metadata !"fpexcept.strict")
  ret float %result
}

define double @ldexp_state(double %value, i64 %exponent) {
; CHECK-LABEL: name: ldexp_state
; CHECK: MAXSQ3ri {{.*}}, -4096
; CHECK-NEXT: {{.*}}MINSQ3ri {{.*}}, 4096
; CHECK: nofpexcept FCVTSQDrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK: nofpexcept FSCALEDrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call double @llvm.ldexp.f64.i64(double %value, i64 %exponent)
  ret double %result
}

define float @strict_ldexp_state(float %value, i32 %exponent) strictfp {
; CHECK-LABEL: name: strict_ldexp_state
; CHECK-NOT: nofpexcept
; CHECK: MAXSL3ri {{.*}}, -512
; CHECK-NEXT: {{.*}}MINSL3ri {{.*}}, 512
; CHECK: FCVTSQSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK: FSCALESrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.experimental.constrained.ldexp.f32.i32(
      float %value, i32 %exponent, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  ret float %result
}

define i64 @comparison(float %lhs, float %rhs) {
; CHECK-LABEL: name: comparison
; CHECK: FCMPSrr {{.*}}, implicit-def $flags, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %condition = fcmp olt float %lhs, %rhs
  %result = zext i1 %condition to i64
  ret i64 %result
}

define i64 @strict_comparison(float %lhs, float %rhs) strictfp {
; CHECK-LABEL: name: strict_comparison
; CHECK-NOT: nofpexcept
; CHECK: FCMPSrr {{.*}}, implicit-def $flags, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK-NEXT: {{.*}}SETCC 4, implicit $flags
  %condition = call i1 @llvm.experimental.constrained.fcmp.f32(
      float %lhs, float %rhs, metadata !"olt", metadata !"fpexcept.strict")
  %result = zext i1 %condition to i64
  ret i64 %result
}

define i64 @strict_compound_comparison(float %lhs, float %rhs) strictfp {
; CHECK-LABEL: name: strict_compound_comparison
; CHECK-NOT: nofpexcept
; CHECK: FCMPSrr {{.*}}, implicit-def $flags, implicit-def $fflags, implicit $fstatus, implicit $fflags
; CHECK-NEXT: {{.*}}SETCC 3, implicit $flags
; CHECK-NEXT: {{.*}}SETCC 9, implicit $flags
; CHECK-NEXT: {{.*}}ANDQ3rr
  %condition = call i1 @llvm.experimental.constrained.fcmp.f32(
      float %lhs, float %rhs, metadata !"one", metadata !"fpexcept.strict")
  %result = zext i1 %condition to i64
  ret i64 %result
}

define i64 @strict_signaling_comparison(float %lhs, float %rhs) strictfp {
; CHECK-LABEL: name: strict_signaling_comparison
; CHECK-NOT: nofpexcept
; CHECK: BEDROCK_FCLASS_S
; CHECK: BEDROCK_FCLASS_S
; CHECK: FCVTStoQrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK-NEXT: FCMPSrr {{.*}}, implicit-def $flags, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %condition = call i1 @llvm.experimental.constrained.fcmps.f32(
      float %lhs, float %rhs, metadata !"oeq", metadata !"fpexcept.strict")
  %result = zext i1 %condition to i64
  ret i64 %result
}

define i64 @strict_signaling_compound(double %lhs, double %rhs) strictfp {
; CHECK-LABEL: name: strict_signaling_compound
; CHECK-NOT: nofpexcept
; CHECK: BEDROCK_FCLASS_D
; CHECK: BEDROCK_FCLASS_D
; CHECK: FCVTDtoQrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
; CHECK-NEXT: FCMPDrr {{.*}}, implicit-def $flags, implicit-def $fflags, implicit $fstatus, implicit $fflags
  %condition = call i1 @llvm.experimental.constrained.fcmps.f64(
      double %lhs, double %rhs, metadata !"one", metadata !"fpexcept.strict")
  %result = zext i1 %condition to i64
  ret i64 %result
}

define float @conversion(i64 %value) {
; CHECK-LABEL: name: conversion
; CHECK: FCVTSQSrr {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = sitofp i64 %value to float
  ret float %result
}

define float @stateful_unary(float %value) {
; CHECK-LABEL: name: stateful_unary
; CHECK: BEDROCK_FSQRT_S {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.sqrt.f32(float %value)
  ret float %result
}

define float @pure_unary(float %value) {
; CHECK-LABEL: name: pure_unary
; CHECK: BEDROCK_FABS_S {{[^,]+$}}
  %result = call float @llvm.fabs.f32(float %value)
  ret float %result
}

define float @fused(float %lhs, float %rhs, float %acc) {
; CHECK-LABEL: name: fused
; CHECK: BEDROCK_FMADD_S {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.fma.f32(float %lhs, float %rhs, float %acc)
  ret float %result
}

define float @approximate(float %value) {
; CHECK-LABEL: name: approximate
; CHECK: BEDROCK_FETOXA_S {{.*}}, implicit-def dead $fflags, implicit $fstatus, implicit $fflags
  %result = call float @llvm.bedrock.fetoxa.f32(float %value)
  ret float %result
}
