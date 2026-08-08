// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -Werror -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple bedrock -Werror -O2 -S -o - %s | FileCheck %s --check-prefix=ASM

#pragma STDC FENV_ACCESS ON

float strict_add(float lhs, float rhs) {
// IR-LABEL: define{{.*}} float @strict_add(
// IR: call float @llvm.experimental.constrained.fadd.f32(float {{.*}}, float {{.*}}, metadata !"round.dynamic", metadata !"fpexcept.strict")
// ASM-LABEL: strict_add:
// ASM: FADD.S
// ASM: ret
  return lhs + rhs;
}

double strict_multiply(double lhs, double rhs) {
// IR-LABEL: define{{.*}} double @strict_multiply(
// IR: call double @llvm.experimental.constrained.fmul.f64(double {{.*}}, double {{.*}}, metadata !"round.dynamic", metadata !"fpexcept.strict")
// ASM-LABEL: strict_multiply:
// ASM: FMUL.D
// ASM: ret
  return lhs * rhs;
}
