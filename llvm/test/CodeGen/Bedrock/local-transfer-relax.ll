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

define dso_local i64 @local_fused_branch(i64 %x) noinline {
; OBJ-LABEL: <local_fused_branch>:
; OBJ: testjeq.q{{.*}}0
; OBJ-NEXT: {{.*}}R_BEDROCK_BRDISP8S{{.*}}.L
  %is.zero = icmp eq i64 %x, 0
  br i1 %is.zero, label %zero, label %nonzero

zero:
  ret i64 0

nonzero:
  ret i64 1
}

define dso_local void @local_index_loop(ptr %dst, i64 %count) noinline {
; OBJ-LABEL: <local_index_loop>:
; OBJ: ijult{{.*}}[pc + 0]
; OBJ-NEXT: {{.*}}R_BEDROCK_PCREL32S{{.*}}.L{{.*}}+0x5
entry:
  br label %loop

loop:
  %index = phi i64 [ 0, %entry ], [ %next, %loop ]
  %address = getelementptr i8, ptr %dst, i64 %index
  store volatile i8 1, ptr %address
  %next = add nuw i64 %index, 1
  %more = icmp ult i64 %next, %count
  br i1 %more, label %loop, label %exit

exit:
  ret void
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
