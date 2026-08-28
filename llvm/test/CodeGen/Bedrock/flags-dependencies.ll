; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs \
; RUN:   -stop-after=finalize-isel < %s | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | \
; RUN:   FileCheck %s --check-prefix=ASM

target triple = "bedrock"

declare { i64, i1 } @llvm.uadd.with.overflow.i64(i64, i64)

define i128 @wide_add(i128 %lhs, i128 %rhs) {
; MIR-LABEL: name: wide_add
; MIR: ADDCQ3rr {{.*}}, implicit-def $flags
; MIR-NEXT: {{.*}}ADCQ3rr {{.*}}, implicit-def dead $flags, implicit $flags
; ASM-LABEL: wide_add:
; ASM: setf 0
; ASM-NEXT: adc.q
; ASM-NEXT: adc.q
  %result = add i128 %lhs, %rhs
  ret i128 %result
}

define i1 @unsigned_add_overflow(i64 %lhs, i64 %rhs) {
; MIR-LABEL: name: unsigned_add_overflow
; MIR: ADDCQ3rr {{.*}}, implicit-def $flags
; MIR-NEXT: {{.*}}SETCC 4, implicit $flags
  %pair = call { i64, i1 } @llvm.uadd.with.overflow.i64(i64 %lhs, i64 %rhs)
  %overflow = extractvalue { i64, i1 } %pair, 1
  ret i1 %overflow
}

define i64 @integer_compare(i64 %lhs, i64 %rhs) {
; MIR-LABEL: name: integer_compare
; MIR: CMPQrr {{.*}}, implicit-def $flags
; MIR-NEXT: {{.*}}SETCC 4, implicit $flags
  %condition = icmp ult i64 %lhs, %rhs
  %result = zext i1 %condition to i64
  ret i64 %result
}

define i64 @integer_branch(i64 %lhs, i64 %rhs) {
; MIR-LABEL: name: integer_branch
; MIR: CMPQrr {{.*}}, implicit-def $flags
; MIR-NEXT: BRCC {{.*}}, implicit $flags
  %condition = icmp ult i64 %lhs, %rhs
  br i1 %condition, label %true, label %false

true:
  ret i64 1

false:
  ret i64 0
}

define i64 @bit_update_clobbers_flags(i64 %value) {
; MIR-LABEL: name: bit_update_clobbers_flags
; MIR: BSETQ3ri {{.*}}, implicit-def dead $flags
  %result = or i64 %value, 8
  ret i64 %result
}

define i64 @floating_compare(double %lhs, double %rhs) {
; MIR-LABEL: name: floating_compare
; MIR: FCMPDrr {{.*}}, implicit-def $flags
; MIR-NEXT: {{.*}}SETCC 4, implicit $flags
  %condition = fcmp olt double %lhs, %rhs
  %result = zext i1 %condition to i64
  ret i64 %result
}
