# REQUIRES: bedrock
# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/shared.s -o %t/shared.o
# RUN: ld.lld -shared %t/shared.o -o %t/shared.so
# RUN: ld.lld -shared -z now %t/shared.o -o %t/shared-now.so
# RUN: cmp %t/shared.so %t/shared-now.so
# RUN: llvm-readobj -S -r -d %t/shared.so | FileCheck %s --check-prefix=SHARED
# RUN: llvm-readobj -x .plt -x .got.plt %t/shared.so | FileCheck %s --check-prefixes=PLT-GOLDEN,GOT
# RUN: llvm-objdump -d -j .plt %t/shared.so | FileCheck %s --check-prefix=PLT
# RUN: llvm-readelf -l %t/shared.so | FileCheck %s --check-prefix=RELRO
# RUN: not ld.lld -shared -z lazy %t/shared.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=LAZY
# RUN: ld.lld -shared -z separate-code %t/shared.o -o %t/trap.so
# RUN: od -Ax -t x1 -N16 -j0x1ff0 %t/trap.so | FileCheck %s --check-prefix=TRAP
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/tls-ref.s -o %t/tls-ref.o
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/tls-def.s -o %t/tls-def.o
# RUN: ld.lld %t/tls-ref.o %t/tls-def.o -e tls_ref -o %t/tls.exe
# RUN: llvm-readobj -r %t/tls.exe | FileCheck %s --check-prefix=RELAX-RELOC
# RUN: llvm-objdump -d %t/tls.exe | FileCheck %s --check-prefix=RELAX
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/tls-ref-large.s -o %t/tls-ref-large.o
# RUN: ld.lld %t/tls-ref-large.o %t/tls-def.o -e tls_ref_large -o %t/tls-large.exe
# RUN: llvm-readobj -r %t/tls-large.exe | FileCheck %s --check-prefix=RELAX-RELOC
# RUN: llvm-objdump -d %t/tls-large.exe | FileCheck %s --check-prefix=RELAX64
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/ifunc.s -o %t/ifunc.o
# RUN: llvm-readelf -h -s %t/ifunc.o | FileCheck %s --check-prefix=IFUNC-OBJ
# RUN: ld.lld -shared %t/ifunc.o -o %t/ifunc.so
# RUN: llvm-readobj -r %t/ifunc.so | FileCheck %s --check-prefix=IFUNC-LINK

# SHARED: Name: .rela.dyn
# SHARED: Type: SHT_RELA
# SHARED: Name: .rela.plt
# SHARED: Type: SHT_RELA
# SHARED: Name: .plt
# SHARED: Size: 32
# SHARED: AddressAlignment: 16
# SHARED: Name: .got
# SHARED: Size: 32
# SHARED: AddressAlignment: 16
# SHARED: Name: .got.plt
# SHARED: Size: 32
# SHARED: FLAGS    BIND_NOW
# SHARED: FLAGS_1  NOW
# SHARED: PLTREL   RELA
# SHARED-DAG: R_BEDROCK_TLSDESC tls 0x0
# SHARED-DAG: R_BEDROCK_ABS64 data 0x0
# SHARED-DAG: R_BEDROCK_GLOB_DAT data 0x0
# SHARED-DAG: R_BEDROCK_JUMP_SLOT function 0x0

# PLT-LABEL: <.plt>:
# PLT: jmp.q	[pc +
# PLT-NEXT: nop
# PLT-NOT: jmp

# PLT-GOLDEN: Hex dump of section '.plt':
# PLT-GOLDEN-NEXT: {{0x[0-9a-f]+}} e7c98067 {{[0-9a-f]+}} 00000000 01010101
# PLT-GOLDEN-NEXT: {{0x[0-9a-f]+}} 01010101 01010101 01010101 01010101

# GOT: Hex dump of section '.got.plt':
# GOT: {{0x[0-9a-f]+}} {{[1-9a-f][0-9a-f]*}} 00000000 00000000 00000000
# GOT-NEXT: {{0x[0-9a-f]+}} 00000000 00000000 00000000 00000000

# RELRO: Section to Segment mapping:
# RELRO: {{[0-9]+}}     .dynamic .got .got.plt .relro_padding
# LAZY: error: -z lazy is not supported for Bedrock
# TRAP: 001ff0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

# RELAX-RELOC: Relocations [
# RELAX-RELOC-NEXT: ]
# RELAX-LABEL: <tls_ref>:
# RELAX: e0 38 6e 00 00 00 00 00 00 00 00 {{.*}}mov.q	0, r0
# RELAX-NEXT: seglea.q	[gs0:0 + r0], r0
# RELAX: ret

# RELAX64-LABEL: <tls_ref_large>:
# RELAX64: f0 38 6f 00 00 00 00 00 00 00 00 00 00 00 00 {{.*}}mov.q	0, r0
# RELAX64-NEXT: seglea.q	[gs0:0 + r0], r0
# RELAX64: ret

# IFUNC-OBJ: OS/ABI:                            UNIX - System V
# IFUNC-OBJ: IFUNC   GLOBAL HIDDEN
# IFUNC-LINK: R_BEDROCK_IRELATIVE

#--- shared.s
.text
.globl entry
.type entry,@function
entry:
  .byte 0xd0, 0xe6, 0, 0, 0, 0, 0
  .reloc entry+3, R_BEDROCK_PLT32S, function
  .byte 0xd1, 0xb8, 0x06, 0, 0, 0, 0
  .reloc entry+10, R_BEDROCK_GOTPCREL32S, data+3
  .byte 0xd1, 0xb8, 0x06, 0, 0, 0, 0
  .reloc entry+17, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+3
  .byte 0xc7, 0xc3, 0x70, 0x10
  .reloc entry+21, R_BEDROCK_TLSDESC_CALL, tls
  .byte 0xcf, 0xc7, 0x80, 0x74, 0xa9, 0x20
  ret
.type tls,@tls_object

.data
.globl data_pointer
data_pointer:
  .quad data

#--- ifunc.s
.text
.globl resolver
.hidden resolver
.type resolver,@function
resolver:
  ret

.globl indirect
.hidden indirect
.type indirect,@gnu_indirect_function
.set indirect,resolver

.data
.globl indirect_pointer
indirect_pointer:
  .quad indirect

#--- tls-ref.s
.text
.globl tls_ref
.type tls_ref,@function
tls_ref:
  .byte 0xd1, 0xb8, 0x06, 0, 0, 0, 0
  .reloc tls_ref+3, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+3
  .byte 0xc7, 0xc3, 0x70, 0x10
  .reloc tls_ref+7, R_BEDROCK_TLSDESC_CALL, tls
  .byte 0xcf, 0xc7, 0x80, 0x74, 0xa9, 0x20
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
  .reloc tls_ref_large+3, R_BEDROCK_TLSDESC_GOTPCREL64, tls+3
  .byte 0xc7, 0xc3, 0x70, 0x10
  .reloc tls_ref_large+11, R_BEDROCK_TLSDESC_CALL, tls
  .byte 0xcf, 0xc7, 0x80, 0x74, 0xa9, 0x20
  ret
.type tls,@tls_object
