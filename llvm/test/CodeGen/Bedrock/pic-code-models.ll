; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -relocation-model=static -code-model=tiny -filetype=obj %s -o %t.low.o
; RUN: llvm-readobj -r %t.low.o | FileCheck %s --check-prefix=LOW
; RUN: llc -mtriple=bedrock -relocation-model=static -code-model=small -filetype=obj %s -o %t.small-static.o
; RUN: llvm-readobj -r %t.small-static.o | FileCheck %s --check-prefix=SMALL-STATIC
; RUN: llc -mtriple=bedrock -relocation-model=static -code-model=medium -filetype=obj %s -o %t.medium.o
; RUN: llvm-readobj -r %t.medium.o | FileCheck %s --check-prefix=MEDIUM
; RUN: llc -mtriple=bedrock -relocation-model=static -code-model=kernel -filetype=obj %s -o %t.high.o
; RUN: llvm-readobj -r %t.high.o | FileCheck %s --check-prefix=HIGH
; RUN: llc -mtriple=bedrock -relocation-model=static -code-model=large -filetype=obj %s -o %t.large.o
; RUN: llvm-readobj -r %t.large.o | FileCheck %s --check-prefix=LARGE
; RUN: llc -mtriple=bedrock -relocation-model=pic -code-model=small -filetype=obj %s -o %t.small-pic.o
; RUN: llvm-readobj -r %t.small-pic.o | FileCheck %s --check-prefix=SMALL-PIC
; RUN: llvm-objdump -d %t.small-pic.o | FileCheck %s --check-prefix=PIC-DISASM
; RUN: llc -mtriple=bedrock -relocation-model=pic -code-model=small -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=PIC-MIR
; RUN: llc -mtriple=bedrock -relocation-model=pic -code-model=medium -filetype=obj %s -o %t.medium-pic.o
; RUN: llvm-readobj -r %t.medium-pic.o | FileCheck %s --check-prefix=MEDIUM-PIC
; RUN: llc -mtriple=bedrock -relocation-model=pic -code-model=large -filetype=obj %s -o %t.large-pic.o
; RUN: llvm-readobj -r %t.large-pic.o | FileCheck %s --check-prefix=LARGE-PIC

target triple = "bedrock"

@external_data = external global i64
@local_data = internal global i64 1
@local_tls = internal thread_local global i64 0
@external_tls = external thread_local global i64

declare void @external_function()

define ptr @external_address() {
  ret ptr @external_data
}

define ptr @local_address() {
  ret ptr @local_data
}

define ptr @local_tls_address() {
  ret ptr @local_tls
}

define ptr @external_tls_address() {
; PIC-MIR-LABEL: name: external_tls_address
; PIC-MIR: TLSDESC_CALL {{.*}}@external_tls, {{.*}}implicit-def dead $status, implicit-def dead $fflags
  ret ptr @external_tls
}

define void @external_call() {
  call void @external_function()
  ret void
}

; LOW: R_BEDROCK_IMM32S external_data
; LOW: R_BEDROCK_IMM32S .data
; LOW: R_BEDROCK_TLS_OFFSET32S local_tls
; LOW: R_BEDROCK_CALL32S external_function

; SMALL-STATIC: R_BEDROCK_PCREL32S external_data
; SMALL-STATIC: R_BEDROCK_PCREL32S .data
; SMALL-STATIC: R_BEDROCK_TLS_OFFSET32S local_tls
; SMALL-STATIC: R_BEDROCK_TLSDESC_GOTPCREL32S external_tls
; SMALL-STATIC: R_BEDROCK_TLSDESC_CALL external_tls
; SMALL-STATIC: R_BEDROCK_CALL32S external_function

; MEDIUM: R_BEDROCK_ABS64 external_data
; MEDIUM: R_BEDROCK_ABS64 .data
; MEDIUM: R_BEDROCK_TLS_OFFSET32S local_tls

; HIGH: R_BEDROCK_ABS64 external_data
; HIGH: R_BEDROCK_PCREL32S .data

; LARGE: R_BEDROCK_ABS64 external_data
; LARGE: R_BEDROCK_ABS64 .data
; LARGE: R_BEDROCK_TLS_OFFSET64 local_tls
; LARGE: R_BEDROCK_ABS64 external_function

; SMALL-PIC: R_BEDROCK_GOTPCREL32S external_data
; SMALL-PIC: R_BEDROCK_PCREL32S .data
; SMALL-PIC: R_BEDROCK_TLSDESC_GOTPCREL32S local_tls
; SMALL-PIC: R_BEDROCK_TLSDESC_CALL local_tls
; SMALL-PIC: R_BEDROCK_TLSDESC_GOTPCREL32S external_tls
; SMALL-PIC: R_BEDROCK_TLSDESC_CALL external_tls
; SMALL-PIC: R_BEDROCK_PLT32S external_function

; MEDIUM-PIC: R_BEDROCK_GOTPCREL32S external_data
; MEDIUM-PIC: R_BEDROCK_GOTPCREL32S .data
; MEDIUM-PIC: R_BEDROCK_TLSDESC_GOTPCREL32S local_tls
; MEDIUM-PIC: R_BEDROCK_TLSDESC_GOTPCREL32S external_tls
; MEDIUM-PIC: R_BEDROCK_PLT32S external_function

; LARGE-PIC: R_BEDROCK_GOTPCREL64 external_data
; LARGE-PIC: R_BEDROCK_PCREL64 .data
; LARGE-PIC: R_BEDROCK_TLSDESC_GOTPCREL64 local_tls
; LARGE-PIC: R_BEDROCK_TLSDESC_CALL local_tls
; LARGE-PIC: R_BEDROCK_TLSDESC_GOTPCREL64 external_tls
; LARGE-PIC: R_BEDROCK_TLSDESC_CALL external_tls
; LARGE-PIC: R_BEDROCK_GOTPCREL64 external_function

; PIC-DISASM-LABEL: <external_address>:
; PIC-DISASM: lea.q	[pc + 0], r0
; PIC-DISASM-NEXT: mov.q	[r0], r0
; PIC-DISASM-LABEL: <external_tls_address>:
; PIC-DISASM: lea.q	[pc + 0], r0
; PIC-DISASM-NEXT: mov.q	[r0], r1
; PIC-DISASM-NEXT: sub.q	8, sp
; PIC-DISASM-NEXT: call	r1
; PIC-DISASM-NEXT: add.q	8, sp
; PIC-DISASM-NEXT: lea.q	[gs0:0 + r0], r0
