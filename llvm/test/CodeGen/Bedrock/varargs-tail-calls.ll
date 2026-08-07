; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -stop-after=finalize-isel < %s | \
; RUN:   FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

%Pair = type { i64, i64 }

declare void @llvm.va_start.p0(ptr)
declare i64 @variadic_sink(i64, ...)
declare i64 @old_style(...)
declare i64 @nine(i64, i64, i64, i64, i64, i64, i64, i64, i64)
declare i64 @pointer_sink(ptr)
declare fastcc i64 @fast_sink(i64)
declare signext i32 @signext_sink(i32)

define i64 @first_unnamed(i64 %tag, ...) {
; CHECK-LABEL: first_unnamed:
; CHECK: mov.q [sp + 32], r0
; CHECK: ret
  %list = alloca ptr, align 8
  call void @llvm.va_start.p0(ptr %list)
  %address = load ptr, ptr %list, align 8
  %value = load i64, ptr %address, align 16
  ret i64 %value
}

define i64 @unnamed_after_named_stack(
    i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4,
    i64 %a5, i64 %a6, i64 %a7, i64 %a8, ...) {
; CHECK-LABEL: unnamed_after_named_stack:
; CHECK: mov.q [sp + 48], r0
; CHECK: ret
  %list = alloca ptr, align 8
  call void @llvm.va_start.p0(ptr %list)
  %address = load ptr, ptr %list, align 8
  %value = load i64, ptr %address, align 16
  ret i64 %value
}

define i64 @call_variadic(ptr %pair) {
; CHECK-LABEL: call_variadic:
; CHECK: sub.q 16, sp
; CHECK: sub.q 72, sp
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 56]
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 48]
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 40]
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 24]
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 8]
; CHECK: call variadic_sink
; CHECK-NEXT: add.q 72, sp
; CHECK: ret
  %result = call i64 (i64, ...) @variadic_sink(
      i64 7, i64 11, double 3.000000e+00,
      i128 22685491128062564232121423433698511445,
      ptr byval(%Pair) align 16 %pair)
  ret i64 %result
}

define i64 @call_unprototyped() {
; CHECK-LABEL: call_unprototyped:
; CHECK: sub.q 40, sp
; CHECK: mov.q {{.*}}, [{{r[0-9]+}} + 24]
; CHECK: mov.l 1, [{{r[0-9]+}} + 8]
; CHECK: call old_style
; CHECK-NEXT: add.q 40, sp
; CHECK-NEXT: ret
  %result = call i64 (...) @old_style(i32 signext 1, double 2.000000e+00)
  ret i64 %result
}

define i64 @legal_tail_call(i64 %value) {
; CHECK-LABEL: legal_tail_call:
; CHECK: jmp variadic_sink
; CHECK-NOT: ret
  %result = tail call i64 (i64, ...) @variadic_sink(i64 %value)
  ret i64 %result
}

define i64 @stack_argument_rejects_tail(
    i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4,
    i64 %a5, i64 %a6, i64 %a7, i64 %a8) {
; CHECK-LABEL: stack_argument_rejects_tail:
; CHECK: call nine
; CHECK: ret
  %result = tail call i64 @nine(
      i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4,
      i64 %a5, i64 %a6, i64 %a7, i64 %a8)
  ret i64 %result
}

define i64 @unnamed_argument_rejects_tail(i64 %value) {
; CHECK-LABEL: unnamed_argument_rejects_tail:
; CHECK: call variadic_sink
; CHECK: ret
  %result = tail call i64 (i64, ...) @variadic_sink(i64 1, i64 %value)
  ret i64 %result
}

define i64 @calling_convention_rejects_tail(i64 %value) {
; CHECK-LABEL: calling_convention_rejects_tail:
; CHECK: call fast_sink
; CHECK: ret
  %result = tail call fastcc i64 @fast_sink(i64 %value)
  ret i64 %result
}

define i32 @return_extension_rejects_tail(i32 %value) {
; CHECK-LABEL: return_extension_rejects_tail:
; CHECK: call signext_sink
; CHECK: ret
  %result = tail call signext i32 @signext_sink(i32 %value)
  ret i32 %result
}

define i64 @caller_frame_address_rejects_tail() {
; CHECK-LABEL: caller_frame_address_rejects_tail:
; CHECK: call pointer_sink
; CHECK: ret
  %local = alloca i64, align 16
  %result = tail call i64 @pointer_sink(ptr %local)
  ret i64 %result
}

define i128 @compiler_rt_i128_cc(i64 %lhs64, i64 %rhs64) {
; CHECK-LABEL: compiler_rt_i128_cc:
; CHECK: call __divti3
; CHECK: ret
  %lhs = sext i64 %lhs64 to i128
  %rhs = sext i64 %rhs64 to i128
  %result = sdiv i128 %lhs, %rhs
  ret i128 %result
}

; OBJ-LABEL: <call_unprototyped>:
; OBJ: sub.q 40, sp
; OBJ: call
; OBJ: add.q 40, sp
; OBJ-LABEL: <legal_tail_call>:
; OBJ: jmp

; MIR-LABEL: name: legal_tail_call
; MIR: CALL_TAIL @variadic_sink
; MIR-LABEL: name: stack_argument_rejects_tail
; MIR: CALL @nine
; MIR-LABEL: name: compiler_rt_i128_cc
; MIR: $r0 = COPY
; MIR-NEXT: $r1 = COPY
; MIR-NEXT: $r2 = COPY
; MIR-NEXT: $r3 = COPY
; MIR: CALL &__divti3
; MIR-SAME: implicit $r0, implicit $r1, implicit $r2, implicit $r3
