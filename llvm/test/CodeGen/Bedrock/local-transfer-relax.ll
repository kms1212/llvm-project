; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -dr --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

define dso_local i64 @local_callee(i64 %x) noinline {
; CHECK-LABEL: local_callee:
; CHECK: ret
  ret i64 %x
}

define dso_local i64 @local_call(i64 %x) noinline {
; CHECK-LABEL: local_call:
; CHECK: call local_callee
; OBJ-LABEL: <local_call>:
; OBJ: c8 a6 00 00 00 {{.*}}call
; OBJ-NEXT: {{.*}}R_BEDROCK_CALL16S{{.*}}local_callee
  %value = call i64 @local_callee(i64 %x)
  %result = add i64 %value, 1
  ret i64 %result
}

define dso_local i64 @local_tail(i64 %x) noinline {
; CHECK-LABEL: local_tail:
; CHECK: jmp local_callee
; OBJ-LABEL: <local_tail>:
; OBJ: c8 26 00 00 00 {{.*}}jmp
; OBJ-NEXT: {{.*}}R_BEDROCK_BRDISP16S{{.*}}local_callee
  %value = tail call i64 @local_callee(i64 %x)
  ret i64 %value
}

define dso_preemptable i64 @replaceable_callee(i64 %x) noinline {
  ret i64 %x
}

define dso_local i64 @replaceable_call(i64 %x) noinline {
; OBJ-LABEL: <replaceable_call>:
; OBJ: d0 e6 00 00 00 00 00 {{.*}}call
; OBJ-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}replaceable_callee
  %value = call i64 @replaceable_callee(i64 %x)
  %result = add i64 %value, 1
  ret i64 %result
}

define dso_local void @padding() noinline {
  call void asm sideeffect ".space 32768", ""()
  ret void
}

define dso_local i64 @far_local_call(i64 %x) noinline {
; OBJ-LABEL: <far_local_call>:
; OBJ: d0 e6 00 00 00 00 00 {{.*}}call
; OBJ-NEXT: {{.*}}R_BEDROCK_CALL32S{{.*}}local_callee
  %value = call i64 @local_callee(i64 %x)
  %result = add i64 %value, 1
  ret i64 %result
}
