; RUN: llc -mtriple=bedrock -verify-machineinstrs -filetype=obj -o %t.o < %s
; RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=DIS
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llc -mtriple=bedrock -verify-machineinstrs -o %t.s < %s
; RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.parser.o %t.s
; RUN: llvm-objdump -d --no-show-raw-insn %t.parser.o | FileCheck %s --check-prefix=DIS

define i64 @call_indirect(ptr %f, i64 %x) {
; DIS-LABEL: <call_indirect>:
; DIS:       CALL{{[[:space:]]+}}A0
; DIS-NOT:   @PCREL32
; DIS:       RET
; RELOC:     Relocations [
; RELOC-NEXT: ]
entry:
  %r = call i64 %f(i64 %x)
  ret i64 %r
}
