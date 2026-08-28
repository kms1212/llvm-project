# REQUIRES: bedrock
# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/bad-addend.s -o %t/bad-addend.o
# RUN: not ld.lld -shared %t/bad-addend.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ADDEND
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/missing-marker.s -o %t/missing-marker.o
# RUN: not ld.lld -shared %t/missing-marker.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/bad-marker.s -o %t/bad-marker.o
# RUN: not ld.lld -shared %t/bad-marker.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=MARKER
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/mismatch.s -o %t/mismatch.o
# RUN: not ld.lld -shared %t/mismatch.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISMATCH
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/bad-code.s -o %t/bad-code.o
# RUN: not ld.lld -shared %t/bad-code.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=CODE
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/non-tls.s -o %t/non-tls.o
# RUN: not ld.lld -shared %t/non-tls.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/weak.s -o %t/weak.o
# RUN: not ld.lld -shared %t/weak.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=WEAK
# RUN: llvm-mc -triple=bedrock -filetype=obj %t/static-undefined.s -o %t/static-undefined.o
# RUN: not ld.lld --unresolved-symbols=ignore-all %t/static-undefined.o -e entry -o /dev/null 2>&1 | FileCheck %s --check-prefix=STATIC

# ADDEND: TLSDESC GOTPCREL relocation addend must be 4
# MISSING: TLSDESC GOTPCREL relocation is not followed by a call marker
# MARKER: TLSDESC call marker addend must be zero
# MISMATCH: TLSDESC relocation pair must name the same TLS symbol
# CODE: TLSDESC relocations require the canonical LEA.Q/CALL R0 sequence
# TYPE: TLSDESC relocation requires an STT_TLS symbol
# WEAK: TLSDESC relocation cannot leave a weak TLS symbol unresolved
# STATIC: TLSDESC relocation cannot remain unresolved in an executable without a runtime loader

#--- bad-addend.s
.text
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, tls
.reloc ., R_BEDROCK_TLSDESC_CALL, tls
.byte 0xc3, 0xb4, 0x20
.type tls,@tls_object

#--- missing-marker.s
.text
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+4
.byte 0xc3, 0xb4, 0x20
.type tls,@tls_object

#--- bad-marker.s
.text
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+4
.reloc ., R_BEDROCK_TLSDESC_CALL, tls+1
.byte 0xc3, 0xb4, 0x20
.type tls,@tls_object

#--- mismatch.s
.text
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+4
.reloc ., R_BEDROCK_TLSDESC_CALL, other
.byte 0xc3, 0xb4, 0x20
.type tls,@tls_object
.type other,@tls_object

#--- bad-code.s
.text
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+4
.reloc ., R_BEDROCK_TLSDESC_CALL, tls
.byte 0x01, 0x01, 0x01
.type tls,@tls_object

#--- non-tls.s
.text
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, object+4
.reloc ., R_BEDROCK_TLSDESC_CALL, object
.byte 0xc3, 0xb4, 0x20
.type object,@object

#--- weak.s
.text
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+4
.reloc ., R_BEDROCK_TLSDESC_CALL, tls
.byte 0xc3, 0xb4, 0x20
.weak tls
.type tls,@tls_object

#--- static-undefined.s
.text
.globl entry
.type entry,@function
entry:
.byte 0xd7, 0xcb, 0xd0, 0x56, 0, 0, 0, 0
.reloc .-4, R_BEDROCK_TLSDESC_GOTPCREL32S, tls+4
.reloc ., R_BEDROCK_TLSDESC_CALL, tls
.byte 0xc3, 0xb4, 0x20
.type tls,@tls_object
