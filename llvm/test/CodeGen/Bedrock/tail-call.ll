; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

declare void @sink(i32)

define void @tail_call_void(i32 %x) {
; CHECK-LABEL: tail_call_void:
; CHECK: jmp sink
; CHECK-NOT: ret
; OBJ-LABEL: <tail_call_void>:
; OBJ: jmp
; OBJ-NOT: ret
  tail call void @sink(i32 %x)
  ret void
}
