; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

define ptr @memcpy_loop(ptr returned %dst, ptr %src, i64 %size) minsize optsize {
; CHECK-LABEL: memcpy_loop:
; CHECK:       MOV.Q A0, [[DST:A[0-7]]]
; CHECK-NEXT:  MOV.Q A1, [[SRC:A[0-7]]]
; CHECK-NEXT:  REP D0, MOV.B {{\[}}[[SRC]]++{{\]}}, {{\[}}[[DST]]++{{\]}}
; CHECK-NEXT:  RET
entry:
  br label %cond

cond:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  %done = icmp eq i64 %i, %size
  br i1 %done, label %exit, label %body

exit:
  ret ptr %dst

body:
  %src.addr = getelementptr inbounds i8, ptr %src, i64 %i
  %v = load i8, ptr %src.addr, align 1
  %dst.addr = getelementptr inbounds i8, ptr %dst, i64 %i
  store i8 %v, ptr %dst.addr, align 1
  %next = add i64 %i, 1
  br label %cond
}

define ptr @memset_loop(ptr returned %dst, i32 %value, i64 %size) minsize optsize {
; CHECK-LABEL: memset_loop:
; CHECK:       MOV.Q A0, [[DST:A[0-7]]]
; CHECK-NEXT:  REP D1, MOV.B D0, {{\[}}[[DST]]++{{\]}}
; CHECK-NEXT:  RET
entry:
  %byte = trunc i32 %value to i8
  br label %cond

cond:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  %done = icmp eq i64 %i, %size
  br i1 %done, label %exit, label %body

exit:
  ret ptr %dst

body:
  %dst.addr = getelementptr inbounds i8, ptr %dst, i64 %i
  store i8 %byte, ptr %dst.addr, align 1
  %next = add i64 %i, 1
  br label %cond
}
