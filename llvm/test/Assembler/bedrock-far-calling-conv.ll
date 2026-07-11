; RUN: llvm-as < %s | llvm-dis | FileCheck %s

target datalayout = "e-m:e-p:64:64-p1:128:128:128:64-i64:64-i128:128-n64-S128"
target triple = "bedrock"

define bedrock_farcc i32 @far_definition(i32 %x) addrspace(1) {
  ret i32 %x
}

define i32 @far_caller(ptr addrspace(1) %fn, i32 %x) {
  %result = call bedrock_farcc addrspace(1) i32 %fn(i32 %x)
  ret i32 %result
}

; CHECK: define bedrock_farcc i32 @far_definition(i32 %x) addrspace(1)
; CHECK: call bedrock_farcc addrspace(1) i32 %fn(i32 %x)
