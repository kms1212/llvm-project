; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -code-model=tiny -filetype=obj < %s -o %t.o
; RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llvm-objdump -d -r --triple=bedrock %t.o | FileCheck %s --check-prefix=DISASM

target triple = "bedrock"

@g = external global i64
@ptrdst = global ptr null
@ptrsrc = external global i8

declare i64 @callee(i64)

define i64 @obj_smoke(i64 %x) {
; DISASM-LABEL: <obj_smoke>:
; DISASM: mov.q [0], r1
; DISASM: R_BEDROCK_ABS32S g
; DISASM: call 0
; DISASM: R_BEDROCK_CALL32S callee
; DISASM: ret
  %v = load i64, ptr @g, align 8
  %s = add i64 %v, %x
  %r = call i64 @callee(i64 %s)
  %out = add i64 %r, 1
  ret i64 %out
}

define void @store_symbol_ptr() {
; DISASM-LABEL: <store_symbol_ptr>:
; DISASM: mov.q 0, [0]
; DISASM-NEXT: R_BEDROCK_IMM32S ptrsrc
; DISASM-NEXT: R_BEDROCK_ABS32S ptrdst
; DISASM: ret
  store ptr @ptrsrc, ptr @ptrdst, align 8
  ret void
}

define i64 @cmp_symbol_ptr(ptr %p) {
; DISASM-LABEL: <cmp_symbol_ptr>:
; DISASM: cmp.q 0, r0
; DISASM-NEXT: R_BEDROCK_IMM32S ptrsrc
; DISASM: setne r0
; DISASM: ret
  %cmp = icmp ne ptr %p, @ptrsrc
  %z = zext i1 %cmp to i64
  ret i64 %z
}

; RELOC: R_BEDROCK_ABS32S
; RELOC-SAME: g + 0
; RELOC: R_BEDROCK_CALL32S
; RELOC-SAME: callee + 0
; RELOC: R_BEDROCK_IMM32S
; RELOC-SAME: ptrsrc + 0
; RELOC: R_BEDROCK_ABS32S
; RELOC-SAME: ptrdst + 0
; RELOC: R_BEDROCK_IMM32S
; RELOC-SAME: ptrsrc + 0
