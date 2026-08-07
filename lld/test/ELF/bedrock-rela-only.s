# REQUIRES: bedrock
# RUN: yaml2obj --docnum=1 %s -o %t.o
# RUN: not ld.lld %t.o -e 0 -o /dev/null 2>&1 | FileCheck %s
# RUN: yaml2obj --docnum=2 %s -o %t.name.o
# RUN: not ld.lld %t.name.o -e 0 -o /dev/null 2>&1 | FileCheck %s --check-prefix=NAME
# RUN: yaml2obj --docnum=3 %s -o %t.pack.o
# RUN: not ld.lld -shared --pack-dyn-relocs=android %t.pack.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=PACK
# RUN: not ld.lld -shared --pack-dyn-relocs=relr %t.pack.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=PACK
# RUN: not ld.lld -shared -z pack-relative-relocs %t.pack.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=PACK

# CHECK: error: {{.*}}.o: only SHT_RELA relocation sections are permitted
# NAME: error: {{.*}}.name.o: SHT_RELA section name must begin with .rela: .bad
# PACK: error: Bedrock ELF permits only unpacked SHT_RELA dynamic relocations

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
  - Name: .bad
    Type: SHT_RELA
    Info: .text
    Relocations:
      - Offset: 0
        Addend: 0
        Symbol: target
        Type:   R_BEDROCK_ABS64
Symbols:
  - Name:    target
    Section: .text

--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_REL
  Machine: EM_BEDROCK
Sections:
  - Name:         .data
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 0x8
    Content:      "00000000000000000000000000000000"
  - Name: .rela.data
    Type: SHT_RELA
    Info: .data
    Relocations:
      - Offset: 8
        Addend: 0
        Symbol: local
        Type:   R_BEDROCK_ABS64
Symbols:
  - Name:       local
    Type:       STT_OBJECT
    Section:    .data
    Binding:    STB_GLOBAL
    Other:      [ STV_HIDDEN ]
