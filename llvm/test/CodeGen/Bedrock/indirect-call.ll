; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

define i64 @indirect_call(ptr %fn, i64 %x) {
; CHECK-LABEL: indirect_call:
; CHECK-NOT: rdseg cs
; CHECK-NOT: lcall
; CHECK: call r{{[0-9]+}}
; CHECK: inc.q r0
; CHECK: ret
; OBJ-LABEL: <indirect_call>:
; OBJ-NOT: rdseg cs
; OBJ-NOT: lcall
; OBJ: call r{{[0-9]+}}
  %value = call i64 %fn(i64 %x)
  %result = add i64 %value, 1
  ret i64 %result
}

define i64 @indirect_call_seven_args(ptr %fn, i64 %a0, i64 %a1, i64 %a2,
                                     i64 %a3, i64 %a4, i64 %a5, i64 %a6) {
; CHECK-LABEL: indirect_call_seven_args:
; CHECK-NOT: rdseg cs
; CHECK-NOT: lcall
; CHECK: call r{{[0-9]+}}
; CHECK: ret
  %value = call i64 %fn(i64 %a0, i64 %a1, i64 %a2, i64 %a3,
                        i64 %a4, i64 %a5, i64 %a6)
  %result = xor i64 %value, %a0
  ret i64 %result
}

define i64 @indirect_tail_call(ptr %fn, i64 %x) {
; CHECK-LABEL: indirect_tail_call:
; CHECK-NOT: rdseg cs
; CHECK-NOT: lcall
; CHECK: jmp.q r{{[0-9]+}}
; CHECK-NOT: ret
; OBJ-LABEL: <indirect_tail_call>:
; OBJ-NOT: rdseg cs
; OBJ-NOT: lcall
; OBJ: jmp.q r{{[0-9]+}}
; OBJ-NOT: ret
  %value = tail call i64 %fn(i64 %x)
  ret i64 %value
}
