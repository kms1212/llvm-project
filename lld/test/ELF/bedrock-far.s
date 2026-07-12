# REQUIRES: bedrock
# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -mattr=+far-elf -filetype=obj %t/static.s -o %t/static.o
# RUN: ld.lld %t/static.o -e 0 -o %t/static
# RUN: llvm-readobj --program-headers %t/static | FileCheck %s --check-prefix=PHDR
# RUN: llvm-readobj -x .far -x .bedrock.segdomains %t/static | FileCheck %s --check-prefix=STATIC
# RUN: ld.lld -shared %t/static.o -o %t/local.so
# RUN: llvm-readobj -r %t/local.so | FileCheck %s --check-prefix=LOCAL
# RUN: llvm-mc -triple=bedrock -mattr=+far-elf -filetype=obj %t/dynamic.s -o %t/dynamic.o
# RUN: ld.lld -shared %t/dynamic.o -o %t/dynamic.so
# RUN: llvm-readobj -S -r -l %t/dynamic.so | FileCheck %s --check-prefix=DYNAMIC
# RUN: llvm-readobj -S %t/dynamic.so | FileCheck %s --check-prefix=NO-PLT
# RUN: llvm-mc -triple=bedrock -mattr=+far-elf -filetype=obj %t/bad-pair.s -o %t/bad-pair.o
# RUN: not ld.lld %t/bad-pair.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-PAIR
# RUN: llvm-mc -triple=bedrock -mattr=+far-elf -filetype=obj %t/tls.s -o %t/tls.o
# RUN: not ld.lld %t/tls.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=TLS
# RUN: llvm-mc -triple=bedrock -mattr=+far-elf -filetype=obj %t/translated.s -o %t/translated.o
# RUN: not ld.lld %t/translated.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=TRANSLATED
# RUN: llvm-mc -triple=bedrock -mattr=+far-elf -filetype=obj %t/range.s -o %t/range.o
# RUN: not ld.lld %t/range.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANGE
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/missing-note.s -o %t/missing-note.o
# RUN: not ld.lld %t/missing-note.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOTE

# PHDR: Type: PT_BEDROCK_SEGDOM (0x70000000)
# PHDR: Offset: 0x0
# PHDR: PhysicalAddress: 0x1
# PHDR: FileSize: 0
# PHDR: MemSize: 4096
# PHDR: PF_BEDROCK_BOUNDS_ONLY (0x10000000)
# PHDR: Alignment: 4096

# STATIC: Hex dump of section '.far':
# STATIC: 03200100 00000000
# STATIC: Hex dump of section '.bedrock.segdomains':
# STATIC: 01000000 03200100 00000000

# LOCAL: R_BEDROCK_RELATIVE - 0x{{[0-9A-Fa-f]+}}
# LOCAL-NEXT: {{.*}} R_BEDROCK_FAR_DOMAIN64 - 0x1

# DYNAMIC-DAG: Name: .got.far
# DYNAMIC-DAG: AddressAlignment: 16
# DYNAMIC-DAG: Name: .rela.dyn
# DYNAMIC-DAG: R_BEDROCK_FAR_GLOB_DAT extobj 0x0
# DYNAMIC-DAG: R_BEDROCK_FAR_JUMP_SLOT extfunc 0x0
# DYNAMIC-DAG: Type: PT_GNU_RELRO
# DYNAMIC-DAG: Type: PT_BEDROCK_SEGDOM
# NO-PLT: Sections [
# NO-PLT-NOT: Name: .plt
# NO-PLT: ]

# BAD-PAIR: R_BEDROCK_FAR_ADDR64 requires an aligned adjacent R_BEDROCK_FAR_SEGMENT64
# TLS: far relocation cannot target TLS symbol 'tls'
# TRANSLATED: translated-window segment image is not permitted
# RANGE: far relocation addend is outside symbol 'object'
# NOTE: far ELF construct requires Tag_Bedrock_Far_Model=1

#--- static.s
.data
.balign 16
.globl object
.hidden object
.type object,@object
object:
  .quad 0
.size object,8
.section .far,"aw",@progbits
.farptr object+8
.bedrock_segdomain 3, 9, 0

#--- dynamic.s
.section .got.far,"aw",@progbits
.balign 16
data_slot:
  .zero 16
.reloc data_slot, R_BEDROCK_FAR_GLOB_DAT, extobj
.balign 16
func_slot:
  .zero 16
.reloc func_slot, R_BEDROCK_FAR_JUMP_SLOT, extfunc
.bedrock_far_func extfunc
.data
dummy:
  .quad 0
.bedrock_segdomain 6, 1, 0

#--- bad-pair.s
.data
.globl object
.type object,@object
object:
  .quad 0
.size object,8
.section .far,"aw",@progbits
.balign 16
slot:
  .zero 16
.reloc slot, R_BEDROCK_FAR_ADDR64, object
.bedrock_segdomain 3, 1, 0

#--- tls.s
.section .tbss,"awT",@nobits
.globl tls
.type tls,@tls_object
tls:
  .zero 8
.section .far,"aw",@progbits
.farptr tls
.bedrock_segdomain 3, 1, 0

#--- translated.s
.data
.globl object
.type object,@object
object:
  .quad 0
.size object,8
.section .far,"aw",@progbits
.farptr object
.bedrock_segdomain 3, 1, 2

#--- range.s
.data
.globl object
.type object,@object
object:
  .quad 0
.size object,8
.section .far,"aw",@progbits
.farptr object+9
.bedrock_segdomain 3, 1, 0

#--- missing-note.s
.data
.globl object
.type object,@object
object:
  .quad 0
.size object,8
.section .far,"aw",@progbits
.balign 16
slot:
  .zero 16
.reloc slot, R_BEDROCK_FAR_ADDR64, object
.reloc slot+8, R_BEDROCK_FAR_SEGMENT64, object
