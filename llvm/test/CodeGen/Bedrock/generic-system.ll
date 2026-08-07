; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs -filetype=obj < %s \
; RUN:   -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

declare void @llvm.trap()
declare void @llvm.debugtrap()
declare i64 @llvm.readcyclecounter()

define void @trap() {
; CHECK-LABEL: trap:
; CHECK: illegal
; CHECK-NOT: call
; OBJ-LABEL: <trap>:
; OBJ: 00{{[ \t]+}}illegal
  call void @llvm.trap()
  unreachable
}

define void @debugtrap() {
; CHECK-LABEL: debugtrap:
; CHECK: bkpt
; CHECK-NOT: call
; CHECK: ret
; OBJ-LABEL: <debugtrap>:
; OBJ: 07{{[ \t]+}}bkpt
  call void @llvm.debugtrap()
  ret void
}

define i64 @cycle_counter() {
; CHECK-LABEL: cycle_counter:
; CHECK: rdpmc 1, r0
; CHECK-NOT: clr
; CHECK: ret
; OBJ-LABEL: <cycle_counter>:
; OBJ: cf ef 47 a0 01 00{{.*}}rdpmc{{[ \t]+}}1, r0
  %value = call i64 @llvm.readcyclecounter()
  ret i64 %value
}
