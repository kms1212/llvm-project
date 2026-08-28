//===- Bedrock.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class Bedrock final : public TargetInfo {
public:
  Bedrock(Ctx &);
  uint32_t calcEFlags() const override;
  void checkProgramHeaders(
      ArrayRef<std::unique_ptr<PhdrEntry>> phdrs) const override;
  RelType getDynRel(RelType type) const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void scanSection(InputSectionBase &sec) override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  bool relaxOnce(int pass) const override;
  void finalizeRelax(int passes) const override;
  void writeGotPltHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &) const override;
  void writeIgotPlt(uint8_t *buf, const Symbol &) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
};
} // namespace

Bedrock::Bedrock(Ctx &ctx) : TargetInfo(ctx) {
  copyRel = R_BEDROCK_COPY;
  gotRel = R_BEDROCK_GLOB_DAT;
  pltRel = R_BEDROCK_JUMP_SLOT;
  relativeRel = R_BEDROCK_RELATIVE;
  iRelativeRel = R_BEDROCK_IRELATIVE;
  symbolicRel = R_BEDROCK_ABS64;
  tlsDescRel = R_BEDROCK_TLSDESC;
  tlsGotRel = R_BEDROCK_TLS_OFFSET64;
  gotBaseSymInGotPlt = true;
  gotSectionAlignment = 16;
  gotPltHeaderEntriesNum = 3;
  pltHeaderSize = 0;
  defaultImageBase = 0x10000;
  trapInstr = {0x00, 0x00, 0x00, 0x00};

#define BEDROCK_LLD_ORDINARY_PLT_ENTRY_ALIGNMENT_BYTES(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_ENTRY_SIZE_BYTES(VALUE)                       \
  pltEntrySize = VALUE;                                                        \
  ipltEntrySize = VALUE;
#define BEDROCK_LLD_ORDINARY_PLT_GOT_SLOT_ALIGNMENT_BYTES(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_GOT_SLOT_PUBLICATION(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_GOT_SLOT_SIZE_BYTES(VALUE)                    \
  gotEntrySize = VALUE;
#define BEDROCK_LLD_ORDINARY_PLT_LAYOUT_INSTRUCTION_OFFSET_BYTES(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_LAYOUT_PADDING_BYTE(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_LAYOUT_PADDING_OFFSET_BYTES(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_LAYOUT_PADDING_SIZE_BYTES(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_LAYOUT_RELOCATION_ADDEND(VALUE)
#define BEDROCK_LLD_ORDINARY_PLT_LAYOUT_RELOCATION_FIELD_OFFSET_BYTES(VALUE)
#define BEDROCK_LLD_LINKAGE_PROPERTY_ORDINARY_PLT(PROPERTY, VALUE)             \
  BEDROCK_LLD_ORDINARY_PLT_##PROPERTY(VALUE)
#define BEDROCK_LLD_LINKAGE_PROPERTY_TLSDESC_CALL(PROPERTY, VALUE)
#define BEDROCK_ELF_LINKAGE_PROPERTY(PROTOCOL, PROPERTY, VALUE)                \
  BEDROCK_LLD_LINKAGE_PROPERTY_##PROTOCOL(PROPERTY, VALUE)

#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_ALIGNMENT_BYTES(VALUE)                  \
  tlsDescEntryAlignment = VALUE;
#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_0_ID(VALUE)
#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_0_OFFSET_BYTES(VALUE)
#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_0_TYPE(VALUE)
#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_1_ID(VALUE)
#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_1_OFFSET_BYTES(VALUE)
#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_1_TYPE(VALUE)
#define BEDROCK_LLD_TLSDESC_DESCRIPTOR_SIZE_BYTES(VALUE)
#define BEDROCK_LLD_TLS_PROPERTY_TLSDESC(PROPERTY, VALUE)                      \
  BEDROCK_LLD_TLSDESC_##PROPERTY(VALUE)
#define BEDROCK_ELF_TLS_PROPERTY(MODEL, PROPERTY, VALUE)                       \
  BEDROCK_LLD_TLS_PROPERTY_##MODEL(PROPERTY, VALUE)
#include "llvm/BinaryFormat/BedrockGenELFABI.inc"
#undef BEDROCK_ELF_TLS_PROPERTY
#undef BEDROCK_LLD_TLS_PROPERTY_TLSDESC
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_ALIGNMENT_BYTES
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_0_ID
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_0_OFFSET_BYTES
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_0_TYPE
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_1_ID
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_1_OFFSET_BYTES
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_FIELDS_1_TYPE
#undef BEDROCK_LLD_TLSDESC_DESCRIPTOR_SIZE_BYTES
#undef BEDROCK_ELF_LINKAGE_PROPERTY
#undef BEDROCK_LLD_LINKAGE_PROPERTY_ORDINARY_PLT
#undef BEDROCK_LLD_LINKAGE_PROPERTY_TLSDESC_CALL
#undef BEDROCK_LLD_ORDINARY_PLT_ENTRY_ALIGNMENT_BYTES
#undef BEDROCK_LLD_ORDINARY_PLT_ENTRY_SIZE_BYTES
#undef BEDROCK_LLD_ORDINARY_PLT_GOT_SLOT_ALIGNMENT_BYTES
#undef BEDROCK_LLD_ORDINARY_PLT_GOT_SLOT_PUBLICATION
#undef BEDROCK_LLD_ORDINARY_PLT_GOT_SLOT_SIZE_BYTES
#undef BEDROCK_LLD_ORDINARY_PLT_LAYOUT_INSTRUCTION_OFFSET_BYTES
#undef BEDROCK_LLD_ORDINARY_PLT_LAYOUT_PADDING_BYTE
#undef BEDROCK_LLD_ORDINARY_PLT_LAYOUT_PADDING_OFFSET_BYTES
#undef BEDROCK_LLD_ORDINARY_PLT_LAYOUT_PADDING_SIZE_BYTES
#undef BEDROCK_LLD_ORDINARY_PLT_LAYOUT_RELOCATION_ADDEND
#undef BEDROCK_LLD_ORDINARY_PLT_LAYOUT_RELOCATION_FIELD_OFFSET_BYTES
}

static uint32_t getEFlags(ELFFileBase *file) {
  return file->getObj<object::ELF64LE>().getHeader().e_flags;
}

uint32_t Bedrock::calcEFlags() const {
  if (ctx.arg.androidPackDynRelocs || ctx.arg.relrPackDynRelocs)
    ErrAlways(ctx) << "Bedrock ELF permits only unpacked SHT_RELA dynamic "
                      "relocations";

  auto checkHeader = [&](ELFFileBase *file) {
    const object::ELF64LE::Ehdr &hdr =
        file->getObj<object::ELF64LE>().getHeader();
    if (hdr.e_ident[EI_VERSION] != EV_CURRENT)
      ErrAlways(ctx) << file << ": unrecognized ELF identification version: "
                     << static_cast<unsigned>(hdr.e_ident[EI_VERSION]);
    if (hdr.e_version != EV_CURRENT)
      ErrAlways(ctx) << file << ": unrecognized ELF header version: "
                     << hdr.e_version;
    if (file->osabi != ELFOSABI_NONE)
      ErrAlways(ctx) << file << ": unrecognized ELF OSABI: "
                     << static_cast<unsigned>(file->osabi);
    if (file->abiVersion != 0)
      ErrAlways(ctx) << file
                     << ": unrecognized ELF ABI version: "
                     << static_cast<unsigned>(file->abiVersion);
    if (uint32_t flags = getEFlags(file))
      ErrAlways(ctx) << file << ": unrecognized e_flags: " << flags;
    if (hdr.e_type == ET_REL && hdr.e_entry != 0)
      ErrAlways(ctx) << file << ": ET_REL e_entry must be zero";
    if (hdr.e_type == ET_DYN && hdr.e_entry != 0)
      ErrAlways(ctx) << file << ": shared-object ET_DYN e_entry must be zero";
    if (hdr.e_ehsize != sizeof(object::ELF64LE::Ehdr))
      ErrAlways(ctx) << file << ": invalid e_ehsize: " << hdr.e_ehsize;
    if (hdr.e_phentsize != sizeof(object::ELF64LE::Phdr))
      ErrAlways(ctx) << file << ": invalid e_phentsize: " << hdr.e_phentsize;
    if (hdr.e_shentsize != sizeof(object::ELF64LE::Shdr))
      ErrAlways(ctx) << file << ": invalid e_shentsize: " << hdr.e_shentsize;
    object::ELFFile<object::ELF64LE> obj =
        file->getObj<object::ELF64LE>();
    for (const object::ELF64LE::Shdr &sec :
         file->getELFShdrs<object::ELF64LE>()) {
      if (sec.sh_type == SHT_REL || sec.sh_type == SHT_RELR ||
          sec.sh_type == SHT_CREL || sec.sh_type == SHT_ANDROID_REL ||
          sec.sh_type == SHT_ANDROID_RELA ||
          sec.sh_type == SHT_ANDROID_RELR)
        ErrAlways(ctx) << file
                       << ": only SHT_RELA relocation sections are permitted";
      if (sec.sh_type == SHT_RELA) {
        StringRef name = CHECK2(obj.getSectionName(sec), file);
        if (!name.starts_with(".rela"))
          ErrAlways(ctx) << file << ": SHT_RELA section name must begin with "
                         << ".rela: " << name;
      }
    }
    for (const object::ELF64LE::Sym &sym :
         file->getELFSyms<object::ELF64LE>())
      if (sym.st_other & 0xfc)
        ErrAlways(ctx) << file << ": reserved symbol st_other bits are nonzero: "
                       << static_cast<unsigned>(sym.st_other);
  };
  for (ELFFileBase *file : ctx.objectFiles)
    checkHeader(file);
  for (ELFFileBase *file : ctx.sharedFiles)
    checkHeader(file);
  return 0;
}

void Bedrock::checkProgramHeaders(
    ArrayRef<std::unique_ptr<PhdrEntry>> phdrs) const {
  for (const std::unique_ptr<PhdrEntry> &phdr : phdrs) {
    if (phdr->p_type != PT_LOAD)
      continue;
    if (!(phdr->p_flags & PF_R))
      ErrAlways(ctx) << "Bedrock PT_LOAD segment must set PF_R";
    if (phdr->p_align < 4096)
      ErrAlways(ctx) << "Bedrock PT_LOAD segment alignment " << phdr->p_align
                     << " is below the 4096-byte minimum";
  }
}

RelType Bedrock::getDynRel(RelType type) const {
  return type == symbolicRel ? type : R_BEDROCK_NONE;
}

int64_t Bedrock::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_BEDROCK_NONE:
  case R_BEDROCK_COPY:
  case R_BEDROCK_RELATIVE:
  case R_BEDROCK_GLOB_DAT:
  case R_BEDROCK_JUMP_SLOT:
  case R_BEDROCK_IRELATIVE:
    return 0;
  case R_BEDROCK_TLSDESC:
    return read64le(buf + 8);
  default:
    return TargetInfo::getImplicitAddend(buf, type);
  }
}

RelExpr Bedrock::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
#define BEDROCK_LLD_RELOC_EXPR_NONE R_NONE
#define BEDROCK_LLD_RELOC_EXPR_ABS R_ABS
#define BEDROCK_LLD_RELOC_EXPR_PC R_PC
#define BEDROCK_LLD_RELOC_EXPR_SECTION_REL RE_BEDROCK_SECTION_REL
#define BEDROCK_LLD_RELOC_EXPR_PLT_PC R_PLT_PC
#define BEDROCK_LLD_RELOC_EXPR_PLT R_PLT
#define BEDROCK_LLD_RELOC_EXPR_GOT R_GOT
#define BEDROCK_LLD_RELOC_EXPR_GOT_PC R_GOT_PC
#define BEDROCK_LLD_RELOC_EXPR_GOTPLTREL R_GOTPLTREL
#define BEDROCK_LLD_RELOC_EXPR_GOTPLTONLY_PC R_GOTPLTONLY_PC
#define BEDROCK_LLD_RELOC_EXPR_TPREL R_TPREL
#define BEDROCK_LLD_RELOC_EXPR_TLSDESC_PC R_TLSDESC_PC
#define BEDROCK_LLD_RELOC_EXPR_TLSDESC_CALL R_TLSDESC_CALL
#define BEDROCK_LLD_RELOC_EXPR_TLSDESC R_TLSDESC
#define BEDROCK_ELF_RELOCATION(NAME, VALUE, FAMILY, RESULT_KIND, WIDTH,        \
                               IS_SIGNED, LLD_EXPRESSION, CALCULATION, FIELD)  \
  case NAME:                                                                   \
    return BEDROCK_LLD_RELOC_EXPR_##LLD_EXPRESSION;
  switch (type) {
#include "llvm/BinaryFormat/BedrockGenELFABI.inc"
  }
  llvm_unreachable("unknown Bedrock relocation");
#undef BEDROCK_ELF_RELOCATION
#undef BEDROCK_LLD_RELOC_EXPR_NONE
#undef BEDROCK_LLD_RELOC_EXPR_ABS
#undef BEDROCK_LLD_RELOC_EXPR_PC
#undef BEDROCK_LLD_RELOC_EXPR_SECTION_REL
#undef BEDROCK_LLD_RELOC_EXPR_PLT_PC
#undef BEDROCK_LLD_RELOC_EXPR_PLT
#undef BEDROCK_LLD_RELOC_EXPR_GOT
#undef BEDROCK_LLD_RELOC_EXPR_GOT_PC
#undef BEDROCK_LLD_RELOC_EXPR_GOTPLTREL
#undef BEDROCK_LLD_RELOC_EXPR_GOTPLTONLY_PC
#undef BEDROCK_LLD_RELOC_EXPR_TPREL
#undef BEDROCK_LLD_RELOC_EXPR_TLSDESC_PC
#undef BEDROCK_LLD_RELOC_EXPR_TLSDESC_CALL
#undef BEDROCK_LLD_RELOC_EXPR_TLSDESC
}

void Bedrock::scanSection(InputSectionBase &sec) {
  using ELFT = object::ELF64LE;
  using Elf_Rela = ELFT::Rela;
  Relocs<Elf_Rela> relas = sec.relsOrRelas<ELFT>().relas;
  ArrayRef<uint8_t> content = sec.content();

  auto report = [&](uint64_t offset, const Twine &message) {
    Err(ctx) << sec.getLocation(offset) << ": " << message;
  };
  auto getSymbol = [&](const Elf_Rela &rel) -> Symbol & {
    return sec.getFile<ELFT>()->getSymbol(rel.getSymbol(false));
  };
  auto checkTlsSymbol = [&](const Elf_Rela &rel) {
    Symbol &sym = getSymbol(rel);
    if (!sym.isTls())
      report(rel.r_offset, "TLSDESC relocation requires an STT_TLS symbol");
    if (sym.isUndefWeak())
      report(rel.r_offset,
             "TLSDESC relocation cannot leave a weak TLS symbol unresolved");
    if (!ctx.arg.shared && ctx.arg.dynamicLinker.empty() &&
        (sym.isUndefined() || sym.isPreemptible))
      report(rel.r_offset,
             "TLSDESC relocation cannot remain unresolved in an executable "
             "without a runtime loader");
  };

  for (const Elf_Rela &rel : relas) {
    RelType type = rel.getType(false);
    bool isAddr32 = type == R_BEDROCK_TLSDESC_GOTPCREL32S;
    bool isAddr64 = type == R_BEDROCK_TLSDESC_GOTPCREL64;
    if (isAddr32 || isAddr64) {
      checkTlsSymbol(rel);
      if (rel.r_addend != 4)
        report(rel.r_offset,
               "TLSDESC GOTPCREL relocation addend must be 4");

      uint64_t fieldSize = isAddr32 ? 4 : 8;
      uint64_t callOffset = rel.r_offset + fieldSize;
      const Elf_Rela *marker = nullptr;
      for (const Elf_Rela &candidate : relas)
        if (candidate.r_offset == callOffset &&
            candidate.getType(false) == R_BEDROCK_TLSDESC_CALL) {
          if (marker)
            report(callOffset, "duplicate TLSDESC call marker");
          marker = &candidate;
        }
      if (!marker) {
        report(rel.r_offset,
               "TLSDESC GOTPCREL relocation is not followed by a call marker");
      } else {
        if (marker->getSymbol(false) != rel.getSymbol(false))
          report(callOffset,
                 "TLSDESC relocation pair must name the same TLS symbol");
        if (marker->r_addend != 0)
          report(callOffset, "TLSDESC call marker addend must be zero");
      }

      static constexpr uint8_t Lea32[] = {0xd7, 0xcb, 0xd0, 0x56};
      static constexpr uint8_t Lea64[] = {0xe7, 0xcb, 0xd0, 0x57};
      static constexpr uint8_t CallR0[] = {0xc3, 0xb4, 0x20};
      ArrayRef<uint8_t> lea = isAddr32 ? ArrayRef(Lea32) : ArrayRef(Lea64);
      bool inBounds = rel.r_offset >= lea.size() &&
                      callOffset + std::size(CallR0) <= content.size();
      if (!inBounds ||
          content.slice(rel.r_offset - lea.size(), lea.size()) != lea ||
          content.slice(callOffset, std::size(CallR0)) != ArrayRef(CallR0))
        report(rel.r_offset,
               "TLSDESC relocations require the canonical LEA.Q/CALL R0 "
               "sequence");
      continue;
    }

    if (type == R_BEDROCK_TLSDESC_CALL) {
      checkTlsSymbol(rel);
      if (rel.r_addend != 0)
        report(rel.r_offset, "TLSDESC call marker addend must be zero");
      bool hasAddressRelocation = false;
      for (const Elf_Rela &candidate : relas) {
        RelType candidateType = candidate.getType(false);
        hasAddressRelocation |=
            (candidateType == R_BEDROCK_TLSDESC_GOTPCREL32S &&
             candidate.r_offset + 4 == rel.r_offset) ||
            (candidateType == R_BEDROCK_TLSDESC_GOTPCREL64 &&
             candidate.r_offset + 8 == rel.r_offset);
      }
      if (!hasAddressRelocation)
        report(rel.r_offset,
               "TLSDESC call marker has no immediately preceding address "
               "relocation");
      continue;
    }

    if (type == R_BEDROCK_TLSDESC) {
      checkTlsSymbol(rel);
      if (rel.r_addend != 0)
        report(rel.r_offset, "TLSDESC relocation addend must be zero");
      if (sec.addralign < 16 || rel.r_offset % 16 != 0)
        report(rel.r_offset,
               "TLSDESC relocation requires a 16-byte-aligned descriptor");
    }
  }

  TargetInfo::scanSection(sec);
}

static RelType getRelaxedTransferType(RelType type) {
  switch (type) {
  case R_BEDROCK_BRDISP32S:
    return R_BEDROCK_BRDISP16S;
  case R_BEDROCK_CALL32S:
    return R_BEDROCK_CALL16S;
  case R_BEDROCK_PLT32S:
    return R_BEDROCK_PLT16S;
  default:
    return R_BEDROCK_NONE;
  }
}

static bool hasCanonicalTransfer32(const InputSection &sec,
                                   const Relocation &rel) {
  ArrayRef<uint8_t> content = sec.content();
  if (rel.offset < 3 || rel.offset + 4 > content.size() ||
      content[rel.offset - 3] != 0xd3)
    return false;
  bool IsBranch = rel.type == R_BEDROCK_BRDISP32S;
  uint8_t Selector = content[rel.offset - 2];
  uint8_t Opcode = content[rel.offset - 1];
  if (Selector == 0xbc)
    return Opcode == (IsBranch ? 0x07 : 0x03);
  if (Selector == 0xb8)
    return (Opcode & 0x30) == (IsBranch ? 0x30 : 0x10);
  return false;
}

static bool relaxBedrockSection(Ctx &ctx, int pass, InputSection &sec) {
  MutableArrayRef<Relocation> relocs = sec.relocs();
  RelaxAux &aux = *sec.relaxAux;
  ArrayRef<SymbolAnchor> anchors = ArrayRef(aux.anchors);
  uint64_t delta = 0;
  bool changed = false;

  std::fill_n(aux.relocTypes.get(), relocs.size(), R_BEDROCK_NONE);
  for (auto [index, rel] : llvm::enumerate(relocs)) {
    uint32_t &currentDelta = aux.relocDeltas[index];
    uint32_t previousRemove = currentDelta - delta;
    uint32_t remove = 0;
    RelType relaxedType = getRelaxedTransferType(rel.type);
    if (sec.addralign <= 1 && relaxedType != R_BEDROCK_NONE &&
        hasCanonicalTransfer32(sec, rel)) {
      if (pass >= 4) {
        remove = previousRemove;
      } else {
        uint64_t fieldVA = sec.getVA() + rel.offset - delta;
        int64_t displacement =
            static_cast<int64_t>(sec.getRelocTargetVA(ctx, rel, fieldVA)) - 2;
        if (isInt<16>(displacement))
          remove = 2;
      }
      if (remove)
        aux.relocTypes[index] = relaxedType;
    }

    for (; !anchors.empty() && anchors.front().offset <= rel.offset;
         anchors = anchors.drop_front()) {
      const SymbolAnchor &anchor = anchors.front();
      if (anchor.end)
        anchor.d->size = anchor.offset - delta - anchor.d->value;
      else
        anchor.d->value = anchor.offset - delta;
    }

    delta += remove;
    if (currentDelta != delta) {
      currentDelta = delta;
      changed = true;
    }
  }

  for (const SymbolAnchor &anchor : anchors) {
    if (anchor.end)
      anchor.d->size = anchor.offset - delta - anchor.d->value;
    else
      anchor.d->value = anchor.offset - delta;
  }
  if (!isUInt<32>(delta))
    Err(ctx) << "section size decrease is too large: " << delta;
  sec.bytesDropped = delta;
  return changed;
}

bool Bedrock::relaxOnce(int pass) const {
  if (!ctx.arg.relax)
    return false;
  if (pass == 0)
    initSymbolAnchors(ctx);

  SmallVector<InputSection *, 0> storage;
  bool changed = false;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage))
      if (sec->relaxAux && sec->relaxAux->relocDeltas)
        changed |= relaxBedrockSection(ctx, pass, *sec);
  }
  return changed;
}

void Bedrock::finalizeRelax(int passes) const {
  if (!ctx.arg.relax)
    return;

  SmallVector<InputSection *, 0> storage;
  for (OutputSection *osec : ctx.outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      if (!sec->relaxAux || !sec->relaxAux->relocDeltas)
        continue;

      RelaxAux &aux = *sec->relaxAux;
      MutableArrayRef<Relocation> relocs = sec->relocs();
      ArrayRef<uint8_t> old = sec->content();
      size_t newSize = old.size() - aux.relocDeltas[relocs.size() - 1];
      uint8_t *newContent = ctx.bAlloc.Allocate<uint8_t>(newSize);
      uint8_t *out = newContent;
      uint64_t oldOffset = 0;
      uint32_t delta = 0;

      for (auto [index, rel] : llvm::enumerate(relocs)) {
        uint32_t remove = aux.relocDeltas[index] - delta;
        RelType newType = aux.relocTypes[index];
        if (remove == 2 &&
            (newType == R_BEDROCK_BRDISP16S ||
             newType == R_BEDROCK_CALL16S || newType == R_BEDROCK_PLT16S)) {
          uint64_t instructionOffset = rel.offset - 3;
          uint64_t copySize = instructionOffset - oldOffset;
          memcpy(out, old.data() + oldOffset, copySize);
          out += copySize;

          bool IsBranch = newType == R_BEDROCK_BRDISP16S;
          bool IsConditional = old[rel.offset - 2] == 0xb8;
          unsigned Cond = old[rel.offset - 1] & 0xf;
          out[0] = 0xcb;
          out[1] = IsConditional ? 0xb8 : 0xbc;
          out[2] = IsConditional ? ((IsBranch ? 0x20 : 0x00) | Cond)
                                 : (IsBranch ? 0x06 : 0x02);
          out[3] = 0;
          out[4] = 0;
          out += 5;
          oldOffset = rel.offset + 4;
        }
        delta = aux.relocDeltas[index];
      }
      memcpy(out, old.data() + oldOffset, old.size() - oldOffset);

      delta = 0;
      for (auto [index, rel] : llvm::enumerate(relocs)) {
        rel.offset -= delta;
        if (aux.relocTypes[index] != R_BEDROCK_NONE)
          rel.type = aux.relocTypes[index];
        delta = aux.relocDeltas[index];
      }

      sec->content_ = newContent;
      sec->size = newSize;
      sec->bytesDropped = 0;
    }
  }
}

static int64_t getNextIPRelativeBias(RelType type) {
  switch (type) {
  case R_BEDROCK_BRDISP8S:
    return 1;
  case R_BEDROCK_BRDISP16S:
  case R_BEDROCK_CALL16S:
  case R_BEDROCK_PLT16S:
    return 2;
  case R_BEDROCK_BRDISP32S:
  case R_BEDROCK_CALL32S:
  case R_BEDROCK_PLT32S:
    return 4;
  default:
    return 0;
  }
}

void Bedrock::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  int64_t adjusted = static_cast<int64_t>(val) - getNextIPRelativeBias(rel.type);

  switch (rel.type) {
  case R_BEDROCK_NONE:
    break;
  case R_BEDROCK_ABS8:
    checkUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_BEDROCK_ABS16:
    checkUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_BEDROCK_ABS32S:
    checkInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_BEDROCK_ABS64:
  case R_BEDROCK_IMM64:
  case R_BEDROCK_DISP64:
  case R_BEDROCK_RELATIVE:
  case R_BEDROCK_GLOB_DAT:
  case R_BEDROCK_JUMP_SLOT:
  case R_BEDROCK_IRELATIVE:
  case R_BEDROCK_GOT64:
  case R_BEDROCK_GOTPCREL64:
  case R_BEDROCK_GOTOFF64:
  case R_BEDROCK_GOT_BASE_PCREL64:
  case R_BEDROCK_PLT64:
  case R_BEDROCK_TLS_OFFSET64:
    write64le(loc, val);
    break;
  case R_BEDROCK_TLSDESC_GOTPCREL64:
    if (rel.expr == R_RELAX_TLS_GD_TO_LE) {
      uint8_t *start = loc - 4;
      // LEN 15, MOV.Q tpoff64, R0. The ELF addend includes the four-byte
      // field offset; it is not part of the semantic TLS subobject offset.
      start[0] = 0xf1;
      start[1] = 0x18;
      start[2] = 0x5e;
      write64le(start + 3, val - 4);
      memset(start + 11, 0, 4);
      break;
    }
    write64le(loc, val);
    break;
  case R_BEDROCK_IMM8S:
  case R_BEDROCK_DISP8S:
    checkInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_BEDROCK_IMM16S:
  case R_BEDROCK_DISP16S:
    checkInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_BEDROCK_IMM32S:
  case R_BEDROCK_DISP32S:
    checkInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_BEDROCK_PCREL8S:
    checkInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_BEDROCK_PCREL16S:
    checkInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_BEDROCK_PCREL32S:
  case R_BEDROCK_GOTPCREL32S:
  case R_BEDROCK_GOTOFF32S:
  case R_BEDROCK_GOT_BASE_PCREL32S:
  case R_BEDROCK_TLS_OFFSET32S:
  case R_BEDROCK_TLSDESC_GOTPCREL32S:
    if (rel.type == R_BEDROCK_TLSDESC_GOTPCREL32S &&
        rel.expr == R_RELAX_TLS_GD_TO_LE) {
      uint8_t *start = loc - 4;
      // LEN 11, MOV.Q tpoff32, R0. See the 64-bit form above.
      int64_t tlsOffset = static_cast<int64_t>(val) - 4;
      start[0] = 0xe1;
      start[1] = 0x18;
      start[2] = 0x5d;
      checkInt(ctx, start + 3, tlsOffset, 32, rel);
      write32le(start + 3, tlsOffset);
      memset(start + 7, 0, 4);
      break;
    }
    checkInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_BEDROCK_PCREL64:
    write64le(loc, val);
    break;
  case R_BEDROCK_BRDISP8S:
    checkInt(ctx, loc, adjusted, 8, rel);
    *loc = adjusted;
    break;
  case R_BEDROCK_BRDISP16S:
  case R_BEDROCK_CALL16S:
  case R_BEDROCK_PLT16S:
    checkInt(ctx, loc, adjusted, 16, rel);
    write16le(loc, adjusted);
    break;
  case R_BEDROCK_BRDISP32S:
  case R_BEDROCK_CALL32S:
  case R_BEDROCK_PLT32S:
    checkInt(ctx, loc, adjusted, 32, rel);
    write32le(loc, adjusted);
    break;
  case R_BEDROCK_SECTION_REL32:
    checkUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_BEDROCK_SECTION_REL64:
    write64le(loc, val);
    break;
  case R_BEDROCK_TLSDESC_CALL:
    break;
  case R_BEDROCK_TLSDESC:
    // Dynamic loaders consume a two-word descriptor. The RELA addend is
    // represented in its second word for static relocation processing.
    write64le(loc + 8, val);
    break;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void Bedrock::writeGotPltHeader(uint8_t *buf) const {
  // GOTPLT[0] is _DYNAMIC. GOTPLT[1] and GOTPLT[2] are initialized to zero
  // and reserved for the loader's object handle and resolver entry point.
  write64le(buf, ctx.mainPart->dynamic->getVA());
  write64le(buf + 8, 0);
  write64le(buf + 16, 0);
}

void Bedrock::writeGotPlt(uint8_t *buf, const Symbol &) const {
  // The loader eagerly publishes the resolved target before application code.
  write64le(buf, 0);
}

void Bedrock::writeIgotPlt(uint8_t *buf, const Symbol &) const {
  write64le(buf, 0);
}

void Bedrock::writePlt(uint8_t *buf, const Symbol &sym,
                       uint64_t pltEntryAddr) const {
  memset(buf, 0x01, pltEntrySize);
  // jmp.q [pc + disp64]. Architectural PC names the entry start, while the
  // ABI relocation field at +3 carries an addend of 3.
  buf[0] = 0xe7;
  buf[1] = 0x9d;
  buf[2] = 0x57;
  write64le(buf + 3, sym.getGotPltVA(ctx) - pltEntryAddr);
}

void elf::setBedrockTargetInfo(Ctx &ctx) {
  ctx.target.reset(new Bedrock(ctx));
}
