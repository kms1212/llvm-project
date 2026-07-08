; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llvm-objdump -d -r --triple=bedrock %t.o | FileCheck %s --check-prefix=DISASM

target triple = "bedrock"

@g = external global i64

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
  ret i64 %r
}

; RELOC: R_BEDROCK_ABS32S
; RELOC-SAME: g + 0
; RELOC: R_BEDROCK_CALL32S
; RELOC-SAME: callee + 0
