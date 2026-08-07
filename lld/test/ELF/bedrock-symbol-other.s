# REQUIRES: bedrock
# RUN: yaml2obj %s -o %t.o
# RUN: not ld.lld %t.o -o /dev/null 2>&1 | FileCheck %s

# CHECK: error: {{.*}}.o: reserved symbol st_other bits are nonzero: 4

--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_REL
  Machine: EM_BEDROCK
Symbols:
  - Name:  bad
    Other: [ 4 ]
