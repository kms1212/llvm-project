; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

%BigResult = type { i64, i128, double }
%Pair = type { i64, i64 }

declare i64 @sink_pair_exhaustion(i64, i64, i64, i64, i64, i64, i64,
                                  i128, i64)
declare i64 @sink_narrow(i8 signext, i16 zeroext, i32 signext)
declare i64 @sink_byval(ptr byval(%Pair) align 16)
declare void @clobber_sret_registers()
declare i64 @sink_nine_float(float, float, float, float, float, float, float,
                             float, float)

define i64 @general_pair_even_alignment(i64 %tag, i128 %wide) {
; CHECK-LABEL: general_pair_even_alignment:
; CHECK: mov.q r2, r0
; CHECK-NEXT: add.q r3, r0
; CHECK-NEXT: ret
  %low = trunc i128 %wide to i64
  %shift = lshr i128 %wide, 64
  %high = trunc i128 %shift to i64
  %sum = add i64 %low, %high
  ret i64 %sum
}

define i64 @pair_exhausts_general_class(i64 %a0, i64 %a1, i64 %a2,
                                        i64 %a3, i64 %a4, i64 %a5,
                                        i64 %a6, i128 %wide, i64 %tail) {
; CHECK-LABEL: pair_exhausts_general_class:
; CHECK: mov.q [sp + 24], r0
; CHECK-NEXT: add.q [sp + 16], r0
; CHECK-NEXT: add.q [sp + 32], r0
; CHECK: ret
  %low = trunc i128 %wide to i64
  %shift = lshr i128 %wide, 64
  %high = trunc i128 %shift to i64
  %sum0 = add i64 %low, %high
  %sum1 = add i64 %sum0, %tail
  ret i64 %sum1
}

define i64 @call_pair_exhaustion() {
; CHECK-LABEL: call_pair_exhaustion:
; CHECK: sub.q 40, sp
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 24]
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 16]
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 8]
; CHECK: call sink_pair_exhaustion
; CHECK-NEXT: add.q 40, sp
; CHECK-NEXT: inc.q r0
; CHECK-NEXT: ret
  %value = call i64 @sink_pair_exhaustion(
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6,
      i128 22685491128062564232121423433698511445, i64 9)
  %result = add i64 %value, 1
  ret i64 %result
}

define void @sret_reserves_r0(ptr sret(%BigResult) %out, i64 %tag,
                              i128 %wide, double %factor) {
; CHECK-LABEL: sret_reserves_r0:
; CHECK: mov.q r3, [r0 + 24]
; CHECK-NEXT: mov.q r2, [r0 + 16]
; CHECK: FMOV.D f0, [r0 + 32]
; CHECK: mov.q r1, [r0]
; CHECK: ret
  %tag.addr = getelementptr inbounds %BigResult, ptr %out, i64 0, i32 0
  %wide.addr = getelementptr inbounds %BigResult, ptr %out, i64 0, i32 1
  %factor.addr = getelementptr inbounds %BigResult, ptr %out, i64 0, i32 2
  store i64 %tag, ptr %tag.addr, align 8
  store i128 %wide, ptr %wide.addr, align 16
  store double %factor, ptr %factor.addr, align 8
  ret void
}

define i64 @aggregate_arguments_are_caller_copies(ptr %source) {
; CHECK-LABEL: aggregate_arguments_are_caller_copies:
; CHECK: sub.q 16, sp
; CHECK: mov.q sp, [[COPY:r[0-9]+]]
; CHECK: mov.q [r0 + 8], [{{r[0-9]+}}]
; CHECK: mov.q [r0], r0
; CHECK: mov.q r0, [sp]
; CHECK: sub.q 8, sp
; CHECK: mov.q [[COPY]], r0
; CHECK: call sink_byval
; CHECK-NEXT: add.q 8, sp
; CHECK: add.q 16, sp
  %value = call i64 @sink_byval(ptr byval(%Pair) align 16 %source)
  %result = add i64 %value, 1
  ret i64 %result
}

define void @sret_pointer_is_returned_after_call(ptr sret(%Pair) %out) {
; CHECK-LABEL: sret_pointer_is_returned_after_call:
; CHECK: mov.q r0, [[SAVED:r[8-9]|r1[0-4]]]
; CHECK: call clobber_sret_registers
; CHECK: mov.q [[SAVED]], r0
; CHECK: ret
  call void @clobber_sret_registers()
  store i64 7, ptr %out, align 8
  ret void
}

define double @floating_register_exhaustion(
    double %f0, double %f1, double %f2, double %f3, double %f4,
    double %f5, double %f6, double %f7, double %f8) {
; CHECK-LABEL: floating_register_exhaustion:
; CHECK: FMOV.D [sp + 16], f0
; CHECK-NEXT: ret
  ret double %f8
}

define float @single_register_exhaustion(
    float %f0, float %f1, float %f2, float %f3, float %f4,
    float %f5, float %f6, float %f7, float %f8) {
; CHECK-LABEL: single_register_exhaustion:
; CHECK: FMOV.S [sp + 16], f0
; CHECK-NEXT: ret
  ret float %f8
}

define i64 @call_single_register_exhaustion(
    float %f0, float %f1, float %f2, float %f3, float %f4,
    float %f5, float %f6, float %f7, float %f8) {
; CHECK-LABEL: call_single_register_exhaustion:
; CHECK: FMOV.S [sp + {{[0-9]+}}], [[STACKF:f[0-9]+]]
; CHECK: sub.q 24, sp
; CHECK: FMOV.S [[STACKF]], [{{r[0-9]+}} + 8]
; CHECK: call sink_nine_float
; CHECK-NEXT: add.q 24, sp
; CHECK-NEXT: inc.q r0
  %value = call i64 @sink_nine_float(
      float %f0, float %f1, float %f2, float %f3, float %f4,
      float %f5, float %f6, float %f7, float %f8)
  %result = add i64 %value, 1
  ret i64 %result
}

define double @general_and_float_cursors_are_independent(
    i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4, i64 %a5, i64 %a6,
    i64 %a7, double %factor) {
; CHECK-LABEL: general_and_float_cursors_are_independent:
; CHECK: ret
  ret double %factor
}

define i64 @float_and_general_cursors_are_independent(
    double %f0, double %f1, double %f2, double %f3, double %f4,
    double %f5, double %f6, double %f7, double %f8, i64 %tail) {
; CHECK-LABEL: float_and_general_cursors_are_independent:
; CHECK: ret
  ret i64 %tail
}

define i64 @call_narrow(i8 %a, i16 %b, i32 %c) {
; CHECK-LABEL: call_narrow:
; CHECK: extsq.b
; CHECK: extzq.w
; CHECK: shl.q 32
; CHECK-NEXT: sar.q 32
; CHECK: call sink_narrow
  %value = call i64 @sink_narrow(i8 signext %a, i16 zeroext %b,
                                 i32 signext %c)
  %result = add i64 %value, 1
  ret i64 %result
}

define [2 x i64] @small_aggregate_return(i64 %low, i64 %high) {
; CHECK-LABEL: small_aggregate_return:
; CHECK: ret
  %v0 = insertvalue [2 x i64] poison, i64 %low, 0
  %v1 = insertvalue [2 x i64] %v0, i64 %high, 1
  ret [2 x i64] %v1
}

define i128 @integer_pair_return(i64 %low, i64 %high) {
; CHECK-LABEL: integer_pair_return:
; CHECK: ret
  %lo = zext i64 %low to i128
  %hi0 = zext i64 %high to i128
  %hi = shl i128 %hi0, 64
  %value = or i128 %hi, %lo
  ret i128 %value
}

; OBJ-LABEL: <call_pair_exhaustion>:
; OBJ: sub.q 40, sp
; OBJ: call
; OBJ: add.q 40, sp
; OBJ-LABEL: <floating_register_exhaustion>:
; OBJ: FMOV.D [sp + 16], f0
; OBJ-LABEL: <single_register_exhaustion>:
; OBJ: FMOV.S [sp + 16], f0
