; REQUIRES: bedrock-registered-target
; RUN: llvm-as %s -o /dev/null
; RUN: llc -mtriple=bedrock -mattr=+fptransa -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

define float @fma_f32(float %a, float %b, float %c) {
; CHECK-LABEL: fma_f32:
; CHECK: FMADD.S
; CHECK: ret
  %r = call float @llvm.fma.f32(float %a, float %b, float %c)
  ret float %r
}

define double @fma_f64(double %a, double %b, double %c) {
; CHECK-LABEL: fma_f64:
; CHECK: FMADD.D
; CHECK: ret
  %r = call double @llvm.fma.f64(double %a, double %b, double %c)
  ret double %r
}

define float @fmsub_f32(float %a, float %b, float %c) {
; CHECK-LABEL: fmsub_f32:
; CHECK: FMSUB.S
  %neg = fneg float %c
  %r = call float @llvm.fma.f32(float %a, float %b, float %neg)
  ret float %r
}

define double @fnmadd_f64(double %a, double %b, double %c) {
; CHECK-LABEL: fnmadd_f64:
; CHECK: FNMADD.D
  %neg = fneg double %a
  %r = call double @llvm.fma.f64(double %neg, double %b, double %c)
  ret double %r
}

define double @fnmsub_f64(double %a, double %b, double %c) {
; CHECK-LABEL: fnmsub_f64:
; CHECK: FNMSUB.D
  %nega = fneg double %a
  %negc = fneg double %c
  %r = call double @llvm.fma.f64(double %nega, double %b, double %negc)
  ret double %r
}

define double @contract_f64(double %a, double %b, double %c) {
; CHECK-LABEL: contract_f64:
; CHECK: FMADD.D
  %mul = fmul contract double %a, %b
  %r = fadd contract double %mul, %c
  ret double %r
}

define float @approx_unary_f32(float %x) {
; CHECK-LABEL: approx_unary_f32:
; CHECK: FACOSA.S
; CHECK: FASINA.S
; CHECK: FATANA.S
; CHECK: FATANHA.S
; CHECK: FCOSA.S
; CHECK: FCOSHA.S
; CHECK: FETOXA.S
; CHECK: FETOXM1A.S
; CHECK: FLOG10A.S
; CHECK: FLOG2A.S
; CHECK: FLOGNA.S
; CHECK: FLOGNP1A.S
; CHECK: FSINA.S
; CHECK: FSINHA.S
; CHECK: FTANA.S
; CHECK: FTANHA.S
; CHECK: FTENTOXA.S
; CHECK: FTWOTOXA.S
  %v0 = call float @llvm.bedrock.facosa.f32(float %x)
  %v1 = call float @llvm.bedrock.fasina.f32(float %v0)
  %v2 = call float @llvm.bedrock.fatana.f32(float %v1)
  %v3 = call float @llvm.bedrock.fatanha.f32(float %v2)
  %v4 = call float @llvm.bedrock.fcosa.f32(float %v3)
  %v5 = call float @llvm.bedrock.fcosha.f32(float %v4)
  %v6 = call float @llvm.bedrock.fetoxa.f32(float %v5)
  %v7 = call float @llvm.bedrock.fetoxm1a.f32(float %v6)
  %v8 = call float @llvm.bedrock.flog10a.f32(float %v7)
  %v9 = call float @llvm.bedrock.flog2a.f32(float %v8)
  %v10 = call float @llvm.bedrock.flogna.f32(float %v9)
  %v11 = call float @llvm.bedrock.flognp1a.f32(float %v10)
  %v12 = call float @llvm.bedrock.fsina.f32(float %v11)
  %v13 = call float @llvm.bedrock.fsinha.f32(float %v12)
  %v14 = call float @llvm.bedrock.ftana.f32(float %v13)
  %v15 = call float @llvm.bedrock.ftanha.f32(float %v14)
  %v16 = call float @llvm.bedrock.ftentoxa.f32(float %v15)
  %v17 = call float @llvm.bedrock.ftwotoxa.f32(float %v16)
  ret float %v17
}

define double @approx_overload_f64(double %x) {
; CHECK-LABEL: approx_overload_f64:
; CHECK: FACOSA.D
  %r = call double @llvm.bedrock.facosa.f64(double %x)
  ret double %r
}

define float @approx_sincos_f32(float %x) {
; CHECK-LABEL: approx_sincos_f32:
; CHECK: FSINCOSA.S
  %pair = call {float, float} @llvm.bedrock.fsincosa.f32(float %x)
  %sin = extractvalue {float, float} %pair, 0
  ret float %sin
}

define double @approx_sincos_f64(double %x) {
; CHECK-LABEL: approx_sincos_f64:
; CHECK: FSINCOSA.D
  %pair = call {double, double} @llvm.bedrock.fsincosa.f64(double %x)
  %sin = extractvalue {double, double} %pair, 0
  %cos = extractvalue {double, double} %pair, 1
  %r = fadd double %sin, %cos
  ret double %r
}

define float @generic_afn_acos(float %x) {
; CHECK-LABEL: generic_afn_acos:
; CHECK: FACOSA.S
  %r = call afn float @llvm.acos.f32(float %x)
  ret float %r
}

define double @generic_afn_transcendentals(double %x) {
; CHECK-LABEL: generic_afn_transcendentals:
; CHECK: FASINA.D
; CHECK: FATANA.D
; CHECK: FCOSHA.D
; CHECK: FETOXA.D
; CHECK: FTWOTOXA.D
; CHECK: FTENTOXA.D
; CHECK: FLOGNA.D
; CHECK: FLOG2A.D
; CHECK: FLOG10A.D
; CHECK: FSINHA.D
; CHECK: FTANHA.D
  %v0 = call afn double @llvm.asin.f64(double %x)
  %v1 = call afn double @llvm.atan.f64(double %v0)
  %v2 = call afn double @llvm.cosh.f64(double %v1)
  %v3 = call afn double @llvm.exp.f64(double %v2)
  %v4 = call afn double @llvm.exp2.f64(double %v3)
  %v5 = call afn double @llvm.exp10.f64(double %v4)
  %v6 = call afn double @llvm.log.f64(double %v5)
  %v7 = call afn double @llvm.log2.f64(double %v6)
  %v8 = call afn double @llvm.log10.f64(double %v7)
  %v9 = call afn double @llvm.sinh.f64(double %v8)
  %v10 = call afn double @llvm.tanh.f64(double %v9)
  ret double %v10
}

define double @generic_without_afn(double %x) {
; CHECK-LABEL: generic_without_afn:
; CHECK-NOT: FETOXA
; CHECK: call exp
  %r = call double @llvm.exp.f64(double %x)
  ret double %r
}

define double @generic_range_rejected(double %x) {
; CHECK-LABEL: generic_range_rejected:
; CHECK-NOT: FSINA
; CHECK: call sin
  %r = call afn double @llvm.sin.f64(double %x)
  ret double %r
}

define double @strict_exp(double %x) strictfp {
; CHECK-LABEL: strict_exp:
; CHECK-NOT: FETOXA
; CHECK: call exp
  %r = call double @llvm.experimental.constrained.exp.f64(
      double %x, metadata !"round.tonearest", metadata !"fpexcept.strict")
  ret double %r
}

define double @generic_constant_sin() {
; CHECK-LABEL: generic_constant_sin:
; CHECK: FSINA.D
  %r = call afn double @llvm.sin.f64(double 5.000000e-01)
  ret double %r
}

define double @generic_constant_cos() {
; CHECK-LABEL: generic_constant_cos:
; CHECK: FCOSA.D
  %r = call afn double @llvm.cos.f64(double -5.000000e-01)
  ret double %r
}

define double @generic_constant_tan() {
; CHECK-LABEL: generic_constant_tan:
; CHECK: FTANA.D
  %r = call afn double @llvm.tan.f64(double 2.500000e-01)
  ret double %r
}

define double @generic_constant_sincos() {
; CHECK-LABEL: generic_constant_sincos:
; CHECK: FSINCOSA.D
  %pair = call afn {double, double} @llvm.sincos.f64(double 2.500000e-01)
  %sin = extractvalue {double, double} %pair, 0
  ret double %sin
}

declare float @llvm.fma.f32(float, float, float)
declare double @llvm.fma.f64(double, double, double)

declare float @llvm.bedrock.facosa.f32(float)
declare float @llvm.bedrock.fasina.f32(float)
declare float @llvm.bedrock.fatana.f32(float)
declare float @llvm.bedrock.fatanha.f32(float)
declare float @llvm.bedrock.fcosa.f32(float)
declare float @llvm.bedrock.fcosha.f32(float)
declare float @llvm.bedrock.fetoxa.f32(float)
declare float @llvm.bedrock.fetoxm1a.f32(float)
declare float @llvm.bedrock.flog10a.f32(float)
declare float @llvm.bedrock.flog2a.f32(float)
declare float @llvm.bedrock.flogna.f32(float)
declare float @llvm.bedrock.flognp1a.f32(float)
declare float @llvm.bedrock.fsina.f32(float)
declare float @llvm.bedrock.fsinha.f32(float)
declare float @llvm.bedrock.ftana.f32(float)
declare float @llvm.bedrock.ftanha.f32(float)
declare float @llvm.bedrock.ftentoxa.f32(float)
declare float @llvm.bedrock.ftwotoxa.f32(float)
declare double @llvm.bedrock.facosa.f64(double)
declare {float, float} @llvm.bedrock.fsincosa.f32(float)
declare {double, double} @llvm.bedrock.fsincosa.f64(double)

declare float @llvm.acos.f32(float)
declare double @llvm.asin.f64(double)
declare double @llvm.atan.f64(double)
declare double @llvm.cosh.f64(double)
declare double @llvm.exp.f64(double)
declare double @llvm.exp2.f64(double)
declare double @llvm.exp10.f64(double)
declare double @llvm.log.f64(double)
declare double @llvm.log2.f64(double)
declare double @llvm.log10.f64(double)
declare double @llvm.sinh.f64(double)
declare double @llvm.tanh.f64(double)
declare double @llvm.sin.f64(double)
declare double @llvm.cos.f64(double)
declare double @llvm.tan.f64(double)
declare {double, double} @llvm.sincos.f64(double)
declare double @llvm.experimental.constrained.exp.f64(double, metadata, metadata)
