# REQUIRES: bedrock
# RUN: yaml2obj %s -o %t.o
# RUN: not ld.lld %t.o -e 0 -o /dev/null 2>&1 | FileCheck %s

# CHECK: error: {{.*}}.o: SHT_REL relocation sections are not permitted

--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_REL
  Machine: EM_BEDROCK
Sections:
  - Name:    .text
    Type:    SHT_PROGBITS
    Flags:   [ SHF_ALLOC, SHF_EXECINSTR ]
    Content: "0000000000000000"
  - Name: .rel.text
    Type: SHT_REL
    Info: .text
    Relocations:
      - Offset: 0
        Symbol: target
        Type:   R_BEDROCK_ABS64
Symbols:
  - Name:    target
    Section: .text
