; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock-unknown-unknown"

define i32 @add_reg_small(i32 %x) {
; CHECK-LABEL: add_reg_small:
; CHECK: ADD.L{{[[:space:]]+}}12, D0
; CHECK: RET
; OBJ-LABEL: <add_reg_small>:
; OBJ: 3e 1f 80 4c{{[[:space:]]+}}ADD.L{{[[:space:]]+}}12, D0
entry:
  %r = add i32 %x, 12
  ret i32 %r
}

define void @add_mem(ptr %p) {
; CHECK-LABEL: add_mem:
; CHECK: ADD.L{{[[:space:]]+}}12, [A0]
; CHECK: RET
; OBJ-LABEL: <add_mem>:
; OBJ: 3e 1f 90 4c{{[[:space:]]+}}ADD.L{{[[:space:]]+}}12, [A0]
entry:
  %v = load i32, ptr %p, align 4
  %r = add i32 %v, 12
  store i32 %r, ptr %p, align 4
  ret void
}

define void @sub_mem(ptr %p) {
; CHECK-LABEL: sub_mem:
; CHECK: SUB.L{{[[:space:]]+}}13, [A0]
; CHECK: RET
entry:
  %v = load i32, ptr %p, align 4
  %r = sub i32 %v, 13
  store i32 %r, ptr %p, align 4
  ret void
}

define void @and_mem(ptr %p) {
; CHECK-LABEL: and_mem:
; CHECK: AND.L{{[[:space:]]+}}14, [A0]
; CHECK: RET
entry:
  %v = load i32, ptr %p, align 4
  %r = and i32 %v, 14
  store i32 %r, ptr %p, align 4
  ret void
}

define void @or_mem(ptr %p) {
; CHECK-LABEL: or_mem:
; CHECK: OR.L{{[[:space:]]+}}15, [A0]
; CHECK: RET
entry:
  %v = load i32, ptr %p, align 4
  %r = or i32 %v, 15
  store i32 %r, ptr %p, align 4
  ret void
}

define void @xor_mem(ptr %p) {
; CHECK-LABEL: xor_mem:
; CHECK: XOR.L{{[[:space:]]+}}18, [A0]
; CHECK: RET
entry:
  %v = load i32, ptr %p, align 4
  %r = xor i32 %v, 18
  store i32 %r, ptr %p, align 4
  ret void
}

define void @add_mem_wide(ptr %p) {
; CHECK-LABEL: add_mem_wide:
; CHECK-NOT: ADD.L{{[[:space:]]+}}100, [A0]
; CHECK: MOV.L{{[[:space:]]+}}[A0], D0
; CHECK: ADD.L{{[[:space:]]+}}100, D0
; CHECK: MOV.L{{[[:space:]]+}}D0, [A0]
; CHECK: RET
entry:
  %v = load i32, ptr %p, align 4
  %r = add i32 %v, 100
  store i32 %r, ptr %p, align 4
  ret void
}

define void @sub_mem_dec(ptr %p) {
; CHECK-LABEL: sub_mem_dec:
; CHECK: DEC.L{{[[:space:]]+}}[A0]
; CHECK: RET
entry:
  %v = load i32, ptr %p, align 4
  %r = sub i32 %v, 1
  store i32 %r, ptr %p, align 4
  ret void
}

define i32 @cmp_imm(i32 %x) {
; CHECK-LABEL: cmp_imm:
; CHECK: CMP.L{{[[:space:]]+}}9, D0
entry:
  %c = icmp eq i32 %x, 9
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

define i32 @cmp_mem(ptr %p) {
; CHECK-LABEL: cmp_mem:
; CHECK: CMP.L{{[[:space:]]+}}9, [A0]
entry:
  %v = load i32, ptr %p, align 4
  %c = icmp eq i32 %v, 9
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

define i32 @test_mem_mask(ptr %p) {
; CHECK-LABEL: test_mem_mask:
; CHECK: TEST.B{{[[:space:]]+}}10, [A0]
entry:
  %v = load i32, ptr %p, align 4
  %m = and i32 %v, 10
  %c = icmp eq i32 %m, 0
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}
