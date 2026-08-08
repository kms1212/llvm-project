; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -relocation-model=static -code-model=small %s -o %t.s
; RUN: FileCheck %s --check-prefix=STATIC < %t.s
; RUN: llvm-mc -triple=bedrock -filetype=obj %t.s -o %t.from-asm.o
; RUN: llvm-readobj -r %t.from-asm.o | FileCheck %s --check-prefix=RELOC
; RUN: llc -mtriple=bedrock -O2 -relocation-model=static -code-model=tiny %s -o - | FileCheck %s --check-prefix=TINY
; RUN: llc -mtriple=bedrock -O2 -relocation-model=pic -code-model=small %s -o - | FileCheck %s --check-prefix=PIC

target triple = "bedrock"

@local_i64 = internal global i64 1
@external_i64 = external global i64
@local_double = internal global double 1.0

define i64 @load_local() {
; STATIC-LABEL: load_local:
; STATIC-NEXT: .cfi_startproc
; STATIC: lea.q [pc + local_i64], r0
; STATIC-NEXT: mov.q [r0], r0
; TINY-LABEL: load_local:
; TINY: mov.q [local_i64], r0
; TINY-NOT: lea.q
  %v = load i64, ptr @local_i64
  ret i64 %v
}

define void @store_local(i64 %v) {
; STATIC-LABEL: store_local:
; STATIC: lea.q [pc + local_i64], r1
; STATIC-NEXT: mov.q r0, [r1]
; TINY-LABEL: store_local:
; TINY: mov.q r0, [local_i64]
; TINY-NOT: lea.q
  store i64 %v, ptr @local_i64
  ret void
}

define void @store_constant() {
; STATIC-LABEL: store_constant:
; STATIC: lea.q [pc + local_i64], r0
; STATIC-NEXT: mov.q 7, [r0]
; TINY-LABEL: store_constant:
; TINY: mov.q 7, [local_i64]
; TINY-NOT: lea.q
  store i64 7, ptr @local_i64
  ret void
}

define i64 @load_external() {
; STATIC-LABEL: load_external:
; STATIC: lea.q [pc + external_i64], r0
; STATIC-NEXT: mov.q [r0], r0
; TINY-LABEL: load_external:
; TINY: mov.q [external_i64], r0
; TINY-NOT: lea.q
; PIC-LABEL: load_external:
; PIC: lea.q [pc + external_i64], r0
; PIC-NEXT: mov.q [r0], r0
; PIC-NEXT: mov.q [r0], r0
  %v = load i64, ptr @external_i64
  ret i64 %v
}

define double @load_float() {
; STATIC-LABEL: load_float:
; STATIC: lea.q [pc + local_double], r0
; STATIC-NEXT: FMOV.D [r0], f0
; TINY-LABEL: load_float:
; TINY: FMOV.D [local_double], f0
; TINY-NOT: lea.q
  %v = load double, ptr @local_double
  ret double %v
}

define void @store_float(double %v) {
; STATIC-LABEL: store_float:
; STATIC: lea.q [pc + local_double], r0
; STATIC-NEXT: FMOV.D f0, [r0]
; TINY-LABEL: store_float:
; TINY: FMOV.D f0, [local_double]
; TINY-NOT: lea.q
  store double %v, ptr @local_double
  ret void
}

; RELOC-COUNT-6: R_BEDROCK_PCREL32S
