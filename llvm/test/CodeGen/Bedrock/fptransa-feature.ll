; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -mattr=+fptransa < %s | FileCheck %s --check-prefix=ENABLED
; RUN: llc -mtriple=bedrock < %s | FileCheck %s --check-prefix=DISABLED

target triple = "bedrock"

define double @generic_afn_acos(double %x) {
; ENABLED-LABEL: generic_afn_acos:
; ENABLED: FACOSA.D
; DISABLED-LABEL: generic_afn_acos:
; DISABLED-NOT: FACOSA
; DISABLED: call acos
  %r = call afn double @llvm.acos.f64(double %x)
  ret double %r
}

declare double @llvm.acos.f64(double)
