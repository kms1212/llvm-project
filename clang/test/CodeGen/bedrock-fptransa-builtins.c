// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -O1 -target-feature +fptransa -internal-isystem %S/../../lib/Headers -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -O1 -target-feature +fptransa -internal-isystem %S/../../lib/Headers -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: not %clang_cc1 -triple bedrock -std=c11 -ffreestanding -target-feature -fptransa -internal-isystem %S/../../lib/Headers -emit-llvm -o /dev/null %s -DTEST_NO_FPTRANSA 2>&1 | FileCheck %s --check-prefix=NOFEATURE

#include <bedrockintrin.h>

#ifndef TEST_NO_FPTRANSA

// CHECK-LABEL: define{{.*}} float @approx_f32
// CHECK: call float @llvm.bedrock.facosa.f32
// CHECK: call float @llvm.bedrock.fasina.f32
// CHECK: call float @llvm.bedrock.fatana.f32
// CHECK: call float @llvm.bedrock.fatanha.f32
// CHECK: call float @llvm.bedrock.fcosa.f32
// CHECK: call float @llvm.bedrock.fcosha.f32
// CHECK: call float @llvm.bedrock.fetoxa.f32
// CHECK: call float @llvm.bedrock.fetoxm1a.f32
// CHECK: call float @llvm.bedrock.flog10a.f32
// CHECK: call float @llvm.bedrock.flog2a.f32
// CHECK: call float @llvm.bedrock.flogna.f32
// CHECK: call float @llvm.bedrock.flognp1a.f32
// CHECK: call float @llvm.bedrock.fsina.f32
// CHECK: call float @llvm.bedrock.fsinha.f32
// CHECK: call float @llvm.bedrock.ftana.f32
// CHECK: call float @llvm.bedrock.ftanha.f32
// CHECK: call float @llvm.bedrock.ftentoxa.f32
// CHECK: call float @llvm.bedrock.ftwotoxa.f32
float approx_f32(float value) {
  value = __bedrock_facosa_f32(value);
  value = __bedrock_fasina_f32(value);
  value = __bedrock_fatana_f32(value);
  value = __bedrock_fatanha_f32(value);
  value = __bedrock_fcosa_f32(value);
  value = __bedrock_fcosha_f32(value);
  value = __bedrock_fetoxa_f32(value);
  value = __bedrock_fetoxm1a_f32(value);
  value = __bedrock_flog10a_f32(value);
  value = __bedrock_flog2a_f32(value);
  value = __bedrock_flogna_f32(value);
  value = __bedrock_flognp1a_f32(value);
  value = __bedrock_fsina_f32(value);
  value = __bedrock_fsinha_f32(value);
  value = __bedrock_ftana_f32(value);
  value = __bedrock_ftanha_f32(value);
  value = __bedrock_ftentoxa_f32(value);
  return __bedrock_ftwotoxa_f32(value);
}

// CHECK-LABEL: define{{.*}} double @approx_f64
// CHECK: call double @llvm.bedrock.facosa.f64
// CHECK: call double @llvm.bedrock.fasina.f64
// CHECK: call double @llvm.bedrock.fatana.f64
// CHECK: call double @llvm.bedrock.fatanha.f64
// CHECK: call double @llvm.bedrock.fcosa.f64
// CHECK: call double @llvm.bedrock.fcosha.f64
// CHECK: call double @llvm.bedrock.fetoxa.f64
// CHECK: call double @llvm.bedrock.fetoxm1a.f64
// CHECK: call double @llvm.bedrock.flog10a.f64
// CHECK: call double @llvm.bedrock.flog2a.f64
// CHECK: call double @llvm.bedrock.flogna.f64
// CHECK: call double @llvm.bedrock.flognp1a.f64
// CHECK: call double @llvm.bedrock.fsina.f64
// CHECK: call double @llvm.bedrock.fsinha.f64
// CHECK: call double @llvm.bedrock.ftana.f64
// CHECK: call double @llvm.bedrock.ftanha.f64
// CHECK: call double @llvm.bedrock.ftentoxa.f64
// CHECK: call double @llvm.bedrock.ftwotoxa.f64
double approx_f64(double value) {
  value = __bedrock_facosa_f64(value);
  value = __bedrock_fasina_f64(value);
  value = __bedrock_fatana_f64(value);
  value = __bedrock_fatanha_f64(value);
  value = __bedrock_fcosa_f64(value);
  value = __bedrock_fcosha_f64(value);
  value = __bedrock_fetoxa_f64(value);
  value = __bedrock_fetoxm1a_f64(value);
  value = __bedrock_flog10a_f64(value);
  value = __bedrock_flog2a_f64(value);
  value = __bedrock_flogna_f64(value);
  value = __bedrock_flognp1a_f64(value);
  value = __bedrock_fsina_f64(value);
  value = __bedrock_fsinha_f64(value);
  value = __bedrock_ftana_f64(value);
  value = __bedrock_ftanha_f64(value);
  value = __bedrock_ftentoxa_f64(value);
  return __bedrock_ftwotoxa_f64(value);
}

// CHECK-LABEL: define{{.*}} void @approx_sincos_f32
// CHECK: call { float, float } @llvm.bedrock.fsincosa.f32
// CHECK: store float
// CHECK: store float
void approx_sincos_f32(float value, float *sin_result, float *cos_result) {
  __bedrock_fsincosa_f32(value, sin_result, cos_result);
}

// CHECK-LABEL: define{{.*}} void @approx_sincos_f64
// CHECK: call { double, double } @llvm.bedrock.fsincosa.f64
// CHECK: store double
// CHECK: store double
void approx_sincos_f64(double value, double *sin_result, double *cos_result) {
  __bedrock_fsincosa_f64(value, sin_result, cos_result);
}

extern float *get_f32_output(void);

// All argument expressions must be evaluated before the architectural
// operation begins.
// CHECK-LABEL: define{{.*}} void @ordered_sincos_arguments
// CHECK: [[SIN_PTR:%.*]] = {{.*}}call ptr @get_f32_output()
// CHECK: [[COS_PTR:%.*]] = {{.*}}call ptr @get_f32_output()
// CHECK: call { float, float } @llvm.bedrock.fsincosa.f32
// CHECK: store float {{.*}}, ptr [[SIN_PTR]]
// CHECK: store float {{.*}}, ptr [[COS_PTR]]
void ordered_sincos_arguments(float value) {
  __builtin_bedrock_fsincosa_f32(value, get_f32_output(), get_f32_output());
}

// CHECK-LABEL: define{{.*}} void @unused_results_are_side_effecting
// CHECK-COUNT-2: call float @llvm.bedrock.fsina.f32
// CHECK: call { float, float } @llvm.bedrock.fsincosa.f32
// ASM-LABEL: unused_results_are_side_effecting:
// ASM-COUNT-2: FSINA.S
// ASM: FSINCOSA.S
void unused_results_are_side_effecting(float value) {
  (void)__bedrock_fsina_f32(value);
  (void)__bedrock_fsina_f32(value);
  float sin_result;
  float cos_result;
  __bedrock_fsincosa_f32(value, &sin_result, &cos_result);
}

#else

// NOFEATURE: error: '__builtin_bedrock_facosa_f32' needs target feature fptransa
float no_fptransa(float value) {
  return __builtin_bedrock_facosa_f32(value);
}

#endif
