; RUN: opt -mtriple=bedrock -aa-pipeline=default -passes=aa-eval \
; RUN:   -disable-output -print-all-alias-modref-info < %s 2>&1 | FileCheck %s --check-prefix=AA
; RUN: opt -mtriple=bedrock -passes=gvn -S < %s | FileCheck %s --check-prefix=GVN
; RUN: opt -mtriple=bedrock -passes='loop-mssa(licm)' -S < %s | FileCheck %s --check-prefix=LICM

; AA: MayAlias: i32 addrspace(1)* %far, i32* %near
define i32 @overlap(ptr %near, ptr addrspace(1) %far) {
  %a = load i32, ptr %near
  %b = load i32, ptr addrspace(1) %far
  %r = add i32 %a, %b
  ret i32 %r
}

; GVN-LABEL: define i32 @keep_reload(
; GVN: %a = load i32, ptr %near
; GVN: store i32 7, ptr addrspace(1) %far
; GVN: %b = load i32, ptr %near
define i32 @keep_reload(ptr %near, ptr addrspace(1) %far) {
  %a = load i32, ptr %near
  store i32 7, ptr addrspace(1) %far
  %b = load i32, ptr %near
  %r = add i32 %a, %b
  ret i32 %r
}

; GVN-LABEL: define i32 @restrict_reload(
; GVN: %a = load i32, ptr %near
; GVN: %r = add i32 %a, %a
define i32 @restrict_reload(ptr noalias %near,
                            ptr addrspace(1) noalias %far) {
  %a = load i32, ptr %near
  store i32 7, ptr addrspace(1) %far
  %b = load i32, ptr %near
  %r = add i32 %a, %b
  ret i32 %r
}

; LICM-LABEL: define i32 @keep_in_loop(
; LICM: loop:
; LICM: %value = load i32, ptr %near
define i32 @keep_in_loop(ptr %near, ptr addrspace(1) %far, i64 %count) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %value = load i32, ptr %near
  store i32 %value, ptr addrspace(1) %far
  %next = add nuw i64 %i, 1
  %done = icmp eq i64 %next, %count
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %value
}
