# REQUIRES: bedrock
# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/shared.s -o %t/shared.o
# RUN: ld.lld -shared %t/shared.o -o %t/shared.so
# RUN: llvm-readobj -S -r -d %t/shared.so | FileCheck %s --check-prefix=SHARED
# RUN: llvm-readobj -x .got.plt %t/shared.so | FileCheck %s --check-prefix=GOT
# RUN: llvm-objdump -d -j .plt %t/shared.so | FileCheck %s --check-prefix=PLT
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/tls-ref.s -o %t/tls-ref.o
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/tls-def.s -o %t/tls-def.o
# RUN: ld.lld %t/tls-ref.o %t/tls-def.o -e tls_ref -o %t/tls.exe
# RUN: llvm-readobj -r %t/tls.exe | FileCheck %s --check-prefix=RELAX-RELOC
# RUN: llvm-objdump -d %t/tls.exe | FileCheck %s --check-prefix=RELAX
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/tls-ref-large.s -o %t/tls-ref-large.o
# RUN: ld.lld %t/tls-ref-large.o %t/tls-def.o -e tls_ref_large -o %t/tls-large.exe
# RUN: llvm-readobj -r %t/tls-large.exe | FileCheck %s --check-prefix=RELAX-RELOC
# RUN: llvm-objdump -d %t/tls-large.exe | FileCheck %s --check-prefix=RELAX64

# SHARED: Name: .rela.dyn
# SHARED: Type: SHT_RELA
# SHARED: Name: .rela.plt
# SHARED: Type: SHT_RELA
# SHARED: Name: .plt
# SHARED: Size: 64
# SHARED: AddressAlignment: 16
# SHARED: Name: .got.plt
# SHARED: Size: 32
# SHARED: PLTREL   RELA
# SHARED-DAG: R_BEDROCK_TLSDESC tls 0x0
# SHARED-DAG: R_BEDROCK_GLOB_DAT data 0x0
# SHARED-DAG: R_BEDROCK_JUMP_SLOT function 0x0

# PLT-LABEL: <.plt>:
# PLT: jmp	[pc +
# PLT: nop
# PLT: jmp	[pc +
# PLT: lea.q	0, r0
# PLT: lea.q	[pc + {{.*}}], r1
# PLT: jmp

# GOT: Hex dump of section '.got.plt':
# GOT: {{0x[0-9a-f]+}} {{[0-9a-f]+}} 00000000 00000000 00000000
# GOT-NEXT: {{0x[0-9a-f]+}} 00000000 00000000 {{[0-9a-f]+}} 00000000

# RELAX-RELOC: Relocations [
# RELAX-RELOC-NEXT: ]
# RELAX-LABEL: <tls_ref>:
# RELAX: lea.q	[gs0:0 + 0], r0
# RELAX-NEXT: nop
# RELAX: ret

# RELAX64-LABEL: <tls_ref_large>:
# RELAX64: lea.q	[gs0:0 + 0], r0
# RELAX64-NEXT: nop
# RELAX64: ret

#--- shared.s
.text
.globl entry
.type entry,@function
entry:
  .byte 0xd0, 0xe6, 0, 0, 0, 0, 0
  .reloc entry+3, R_BEDROCK_PLT32S, function
  .byte 0xd1, 0xb8, 0x06, 0, 0, 0, 0
  .reloc entry+10, R_BEDROCK_GOTPCREL32S, data
  .byte 0xd1, 0xb8, 0x06, 0, 0, 0, 0
  .reloc entry+17, R_BEDROCK_TLSDESC_GOTPCREL32S, tls
  .byte 0xc0, 0x38, 0x80
  .byte 0x0f
  .byte 0xc7, 0xc3, 0x88, 0x01
  .reloc entry+25, R_BEDROCK_TLSDESC_CALL, tls
  .byte 0x0e
  .byte 0xc9, 0xf8, 0x04, 0xb9, 0x20
  ret
.type tls,@tls_object

#--- tls-ref.s
.text
.globl tls_ref
.type tls_ref,@function
tls_ref:
  .byte 0xd1, 0xb8, 0x06, 0, 0, 0, 0
  .reloc tls_ref+3, R_BEDROCK_TLSDESC_GOTPCREL32S, tls
  .byte 0xc0, 0x38, 0x80
  .byte 0x0f
  .byte 0xc7, 0xc3, 0x88, 0x01
  .reloc tls_ref+11, R_BEDROCK_TLSDESC_CALL, tls
  .byte 0x0e
  .byte 0xc9, 0xf8, 0x04, 0xb9, 0x20
  ret
.type tls,@tls_object

#--- tls-def.s
.section .tbss,"awT",@nobits
.globl tls
.type tls,@tls_object
tls:
  .zero 8

#--- tls-ref-large.s
.text
.globl tls_ref_large
.type tls_ref_large,@function
tls_ref_large:
  .byte 0xe1, 0xb8, 0x07, 0, 0, 0, 0, 0, 0, 0, 0
  .reloc tls_ref_large+3, R_BEDROCK_TLSDESC_GOTPCREL64, tls
  .byte 0xc0, 0x38, 0x90
  .byte 0x0f
  .byte 0xc7, 0xc3, 0x88, 0x01
  .reloc tls_ref_large+15, R_BEDROCK_TLSDESC_CALL, tls
  .byte 0x0e
  .byte 0xc9, 0xf8, 0x04, 0xb9, 0x20
  ret
.type tls,@tls_object
