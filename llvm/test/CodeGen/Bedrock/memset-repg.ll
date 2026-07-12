; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)

define void @constant_memset(i8 %value) {
; CHECK-LABEL: constant_memset:
; CHECK: repg
; CHECK-NEXT: mov.b{{.*}}++
; CHECK-NEXT: }
; CHECK: ret
; OBJ-LABEL: <constant_memset>:
; OBJ: repg
; OBJ-SAME: mov.b
; OBJ-SAME: ++
  call void @llvm.memset.p0.i64(ptr inttoptr (i64 15728640 to ptr),
                               i8 %value, i64 64000, i1 false)
  ret void
}

define void @volatile_memset(i8 %value) {
; CHECK-LABEL: volatile_memset:
; CHECK-NOT: repg
; CHECK: call memset
; OBJ-LABEL: <volatile_memset>:
; OBJ-NOT: repg
  call void @llvm.memset.p0.i64(ptr inttoptr (i64 15728640 to ptr),
                               i8 %value, i64 64000, i1 true)
  ret void
}
