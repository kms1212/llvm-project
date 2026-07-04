; RUN: llc -mtriple=bedrock-unknown-unknown -O2 < %s | FileCheck %s

target triple = "bedrock-unknown-unknown"

define i32 @cmp_mem_rhs(ptr %p, i32 %x) minsize nounwind {
; CHECK-LABEL: cmp_mem_rhs:
; CHECK:       CMP.L [A0], D0
; CHECK-NEXT:  JGE.W
entry:
  %v = load i32, ptr %p, align 4
  %c = icmp slt i32 %x, %v
  br i1 %c, label %t, label %f

t:
  ret i32 1

f:
  ret i32 0
}

define i32 @cmp_mem_lhs(ptr %p, i32 %x) minsize nounwind {
; CHECK-LABEL: cmp_mem_lhs:
; CHECK:       CMP.L D0, [A0]
; CHECK-NEXT:  JGE.W
entry:
  %v = load i32, ptr %p, align 4
  %c = icmp slt i32 %v, %x
  br i1 %c, label %t, label %f

t:
  ret i32 1

f:
  ret i32 0
}

define i32 @cmp_mem_imm8_large(ptr %p) minsize nounwind {
; CHECK-LABEL: cmp_mem_imm8_large:
; CHECK-NOT:   MOV.B
; CHECK:       CMP.B 84, [A0]
entry:
  %v = load i8, ptr %p, align 1
  %c = icmp eq i8 %v, 84
  br i1 %c, label %t, label %f

t:
  ret i32 1

f:
  ret i32 0
}
