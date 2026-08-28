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

int current_rounding(void) {
// IR-LABEL: define{{.*}} i32 @current_rounding()
// IR: call i32 @llvm.get.rounding()
// ASM-LABEL: current_rounding:
// ASM: lea.q{{[ \t]+}}5,
// ASM: rdfstatus
// ASM: shr.q
// ASM: and.q{{[ \t]+}}3,
// ASM: bchg{{[ \t]+}}0,
// ASM: ret
  return __builtin_flt_rounds();
}

void set_rounding(int mode) {
// IR-LABEL: define{{.*}} void @set_rounding(i32
// IR: call void @llvm.set.rounding(i32
// ASM-LABEL: set_rounding:
// ASM: rdfstatus
// ASM: wrfstatus
// ASM: ret
  __builtin_set_flt_rounds(mode);
}
