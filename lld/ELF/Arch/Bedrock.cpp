//===- Bedrock.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
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
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  void scanSection(InputSectionBase &sec) override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  void writeGotPltHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writeIgotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  int64_t getImplicitAddend(const uint8_t *buf,
                            RelType type) const override;
};
} // namespace

bool elf::hasBedrockFarAttributeNote(const InputFile &file) {
  for (InputSectionBase *sec : file.getSections()) {
    if (!sec || sec == &InputSection::discarded || sec->name != ".note.bedrock")
      continue;
    ArrayRef<uint8_t> data = sec->content();
    if (data.size() == 28 && read32le(data.data()) == 8 &&
        read32le(data.data() + 4) == 8 &&
        read32le(data.data() + 8) == NT_BEDROCK_ABI_ATTRIBUTES &&
        StringRef(reinterpret_cast<const char *>(data.data() + 12), 8) ==
            StringRef("BEDROCK\0", 8) &&
        read32le(data.data() + 20) == TAG_BEDROCK_FAR_MODEL &&
        read32le(data.data() + 24) == 0)
      return true;
  }
  return false;
}

static uint64_t getFarDomainImage(Ctx &ctx, const Symbol &sym) {
  const auto *defined = dyn_cast<Defined>(&sym);
  if (!defined || !defined->file ||
      defined->file->kind() != InputFile::ObjKind) {
    Err(ctx) << "far relocation target '" << &sym
             << "' has no defining segment domain";
    return 0;
  }

  auto sections = defined->file->getSections();
  unsigned matches = 0;
  uint64_t image = 0;
  for (InputSectionBase *meta : sections) {
    if (!meta || meta == &InputSection::discarded ||
        meta->name != ".bedrock.segdomains")
      continue;
    ArrayRef<uint8_t> data = meta->content();
    if (meta->type != SHT_PROGBITS || meta->flags != 0 || meta->entsize != 16 ||
        data.size() % 16 != 0) {
      Err(ctx) << meta << ": malformed .bedrock.segdomains section";
      continue;
    }
    for (size_t off = 0; off != data.size(); off += 16) {
      uint32_t index = read32le(data.data() + off);
      uint32_t id = read32le(data.data() + off + 4);
      if (index >= sections.size() || sections[index] != defined->section)
        continue;
      ++matches;
      image = read64le(data.data() + off + 8);
      if (id == 0)
        Err(ctx) << meta << ": segment domain identifier must be nonzero";
      if (!(defined->section->flags & SHF_ALLOC) ||
          (defined->section->flags & SHF_TLS))
        Err(ctx) << meta
                 << ": segment domain must name an allocatable non-TLS section";
      if (image != 0 && ((image & 1) == 0 || ((image >> 1) & 0x3f) == 0))
        Err(ctx) << meta
                 << ": translated-window segment image is not permitted";
    }
  }
  if (matches != 1) {
    Err(ctx) << "far relocation target '" << &sym << "' is assigned to "
             << matches << " segment domains";
    return 0;
  }
  return image;
}

uint32_t elf::getBedrockOutputDomainID(Ctx &ctx, const ELFFileBase *wantedFile,
                                       uint32_t wantedInputID) {
  SmallVector<std::pair<const ELFFileBase *, uint32_t>, 0> seen;
  for (ELFFileBase *file : ctx.objectFiles) {
    for (InputSectionBase *meta : file->getSections()) {
      if (!meta || meta == &InputSection::discarded ||
          meta->name != ".bedrock.segdomains" || meta->entsize != 16)
        continue;
      ArrayRef<uint8_t> data = meta->content();
      for (size_t off = 0; off + 16 <= data.size(); off += 16) {
        uint32_t inputID = read32le(data.data() + off + 4);
        std::pair<const ELFFileBase *, uint32_t> key{file, inputID};
        if (!llvm::is_contained(seen, key))
          seen.push_back(key);
        if (file == wantedFile && inputID == wantedInputID)
          return llvm::find(seen, key) - seen.begin() + 1;
      }
    }
  }
  return 0;
}

uint32_t elf::getBedrockOutputDomainID(Ctx &ctx, const Symbol &sym) {
  const auto *defined = dyn_cast<Defined>(&sym);
  if (!defined || !defined->file || !defined->section)
    return 0;
  ArrayRef<InputSectionBase *> sections = defined->file->getSections();
  for (InputSectionBase *meta : sections) {
    if (!meta || meta == &InputSection::discarded ||
        meta->name != ".bedrock.segdomains" || meta->entsize != 16)
      continue;
    ArrayRef<uint8_t> data = meta->content();
    for (size_t off = 0; off + 16 <= data.size(); off += 16) {
      uint32_t sectionIndex = read32le(data.data() + off);
      if (sectionIndex < sections.size() &&
          sections[sectionIndex] == defined->section)
        return getBedrockOutputDomainID(ctx, cast<ELFFileBase>(defined->file),
                                        read32le(data.data() + off + 4));
    }
  }
  return 0;
}

static uint64_t encodeFarDomainImage(const PhdrEntry &phdr) {
  uint64_t pages = phdr.p_memsz / 4096;
  unsigned exponent = 0;
  while (pages > 63) {
    pages = alignTo(pages, 2) / 2;
    ++exponent;
  }
  return (phdr.p_vaddr & ~uint64_t(4095)) | (uint64_t(exponent) << 7) |
         (pages << 1) | 1;
}

static uint64_t getFinalFarDomainImage(Ctx &ctx, const Symbol &sym) {
  const auto *defined = dyn_cast<Defined>(&sym);
  if (!defined || !defined->section)
    return 0;
  for (const std::unique_ptr<PhdrEntry> &phdr : ctx.mainPart->phdrs) {
    if (phdr->p_type != PT_BEDROCK_SEGDOM)
      continue;
    if (llvm::is_contained(phdr->bedrockDomainSections, defined->section))
      return encodeFarDomainImage(*phdr);
  }
  Err(ctx) << "far relocation target '" << &sym
           << "' has no output segment domain";
  return 0;
}

static uint64_t getFinalFarDomainImage(Ctx &ctx, uint32_t domainID) {
  for (const std::unique_ptr<PhdrEntry> &phdr : ctx.mainPart->phdrs)
    if (phdr->p_type == PT_BEDROCK_SEGDOM && phdr->p_paddr == domainID)
      return encodeFarDomainImage(*phdr);
  Err(ctx) << "unknown Bedrock output segment domain " << domainID;
  return 0;
}

template <class ELFT>
static void validateFarRelocations(Ctx &ctx, InputSectionBase &sec) {
  auto rels = sec.template relsOrRelas<ELFT>();
  if (rels.areRelocsRel())
    return;
  ArrayRef<typename ELFT::Rela> entries = rels.relas;
  bool checkedNote = false;
  for (size_t i = 0; i != entries.size(); ++i) {
    const auto &rel = entries[i];
    RelType type = rel.getType(false);
    if (type.v < R_BEDROCK_FAR_ADDR64 || type.v > R_BEDROCK_FAR_IRELATIVE)
      continue;
    if (!checkedNote) {
      checkedNote = true;
      if (!hasBedrockFarAttributeNote(*sec.file))
        Err(ctx) << sec.file
                 << ": far ELF construct requires Tag_Bedrock_Far_Model=1";
    }

    Symbol &sym = sec.getFile<ELFT>()->getSymbol(rel.getSymbol(false));
    if (sym.isTls())
      Err(ctx) << getErrorLoc(ctx, sec.content().data() + rel.r_offset)
               << "far relocation cannot target TLS symbol '" << &sym << "'";

    if (type == R_BEDROCK_FAR_ADDR64) {
      const typename ELFT::Rela *mate =
          i + 1 != entries.size() ? &entries[i + 1] : nullptr;
      if ((rel.r_offset & 15) != 0 || !mate || mate->r_addend != 0)
        Err(ctx)
            << getErrorLoc(ctx, sec.content().data() + rel.r_offset)
            << "R_BEDROCK_FAR_ADDR64 requires an aligned adjacent "
               "R_BEDROCK_FAR_SEGMENT64 relocation against the same symbol";
      else if (mate->r_offset != rel.r_offset + 8 ||
               mate->getType(false) != R_BEDROCK_FAR_SEGMENT64 ||
               mate->getSymbol(false) != rel.getSymbol(false))
        Err(ctx) << getErrorLoc(ctx, sec.content().data() + rel.r_offset)
                 << "R_BEDROCK_FAR_ADDR64 relocation pair must be adjacent "
                    "and name the same symbol";
      if (rel.r_addend < 0 ||
          (sym.getSize() != 0 && uint64_t(rel.r_addend) > sym.getSize()) ||
          (uint64_t(rel.r_addend) == sym.getSize() && sym.getSize() != 0 &&
           sym.type != STT_OBJECT))
        Err(ctx) << "far relocation addend is outside symbol '" << &sym
                 << "' (only one-past STT_OBJECT is permitted)";
      if (isa<Defined>(sym))
        (void)getFarDomainImage(ctx, sym);
    } else if (type == R_BEDROCK_FAR_SEGMENT64) {
      bool hasAddress = llvm::any_of(entries, [&](const auto &candidate) {
        return candidate.r_offset + 8 == rel.r_offset &&
               candidate.getType(false) == R_BEDROCK_FAR_ADDR64 &&
               candidate.getSymbol(false) == rel.getSymbol(false);
      });
      if (!hasAddress)
        Err(ctx) << getErrorLoc(ctx, sec.content().data() + rel.r_offset)
                 << "orphan R_BEDROCK_FAR_SEGMENT64 relocation";
    }
  }
}

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
  gotEntrySize = 8;
  gotPltHeaderEntriesNum = 3;
  pltHeaderSize = 32;
  pltEntrySize = 32;
  ipltEntrySize = 32;
  defaultImageBase = 0x10000;
  trapInstr = {0x20, 0x42, 0x20, 0x42};
}

void Bedrock::scanSection(InputSectionBase &sec) {
  validateFarRelocations<object::ELF64LE>(ctx, sec);
  TargetInfo::scanSection(sec);
}

RelType Bedrock::getDynRel(RelType type) const {
  switch (type) {
  case R_BEDROCK_FAR_GLOB_DAT:
  case R_BEDROCK_FAR_JUMP_SLOT:
  case R_BEDROCK_FAR_IRELATIVE:
    return type;
  default:
    return type == symbolicRel ? type : R_BEDROCK_NONE;
  }
}

int64_t Bedrock::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_BEDROCK_NONE:
  case R_BEDROCK_COPY:
  case R_BEDROCK_RELATIVE:
  case R_BEDROCK_GLOB_DAT:
  case R_BEDROCK_JUMP_SLOT:
  case R_BEDROCK_IRELATIVE:
  case R_BEDROCK_FAR_GLOB_DAT:
  case R_BEDROCK_FAR_JUMP_SLOT:
  case R_BEDROCK_FAR_IRELATIVE:
    return 0;
  case R_BEDROCK_TLSDESC:
    return read64le(buf + 8);
  default:
    return TargetInfo::getImplicitAddend(buf, type);
  }
}

RelExpr Bedrock::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_BEDROCK_NONE:
    return R_NONE;
  case R_BEDROCK_PCREL8S:
  case R_BEDROCK_PCREL16S:
  case R_BEDROCK_PCREL32S:
  case R_BEDROCK_PCREL64:
  case R_BEDROCK_BRDISP8S:
  case R_BEDROCK_BRDISP16S:
  case R_BEDROCK_BRDISP32S:
  case R_BEDROCK_CALL16S:
  case R_BEDROCK_CALL32S:
    return R_PC;
  case R_BEDROCK_PLT16S:
  case R_BEDROCK_PLT32S:
    return R_PLT_PC;
  case R_BEDROCK_PLT64:
    return R_PLT;
  case R_BEDROCK_GOT64:
    return R_GOT;
  case R_BEDROCK_GOTPCREL32S:
  case R_BEDROCK_GOTPCREL64:
    return R_GOT_PC;
  case R_BEDROCK_GOTOFF32S:
  case R_BEDROCK_GOTOFF64:
    return R_GOTPLTREL;
  case R_BEDROCK_GOT_BASE_PCREL32S:
  case R_BEDROCK_GOT_BASE_PCREL64:
    return R_GOTPLTONLY_PC;
  case R_BEDROCK_TLS_OFFSET32S:
  case R_BEDROCK_TLS_OFFSET64:
    return R_TPREL;
  case R_BEDROCK_TLSDESC_GOTPCREL32S:
  case R_BEDROCK_TLSDESC_GOTPCREL64:
    return R_TLSDESC_PC;
  case R_BEDROCK_TLSDESC_CALL:
    return R_TLSDESC_CALL;
  case R_BEDROCK_TLSDESC:
    return R_TLSDESC;
  case R_BEDROCK_FAR_ADDR64:
  case R_BEDROCK_FAR_SEGMENT64:
  case R_BEDROCK_FAR_GLOB_DAT:
  case R_BEDROCK_FAR_JUMP_SLOT:
  case R_BEDROCK_FAR_IRELATIVE:
    return R_ABS;
  case R_BEDROCK_FAR_DOMAIN64:
    return R_ADDEND;
  default:
    return R_ABS;
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
      uint8_t *start = loc - 3;
      // lea.q [gs0:0 + tpoff64], r0
      start[0] = 0xe5;
      start[1] = 0xf8;
      start[2] = 0x03;
      start[3] = 0xb3;
      write64le(start + 4, val);
      memset(start + 12, 0x01, 13);
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
      uint8_t *start = loc - 3;
      // lea.q [gs0:0 + tpoff32], r0
      start[0] = 0xd5;
      start[1] = 0xf8;
      start[2] = 0x02;
      start[3] = 0xb3;
      checkInt(ctx, start + 4, val, 32, rel);
      write32le(start + 4, val);
      memset(start + 8, 0x01, 13);
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
  case R_BEDROCK_TLSDESC_CALL:
    break;
  case R_BEDROCK_TLSDESC:
    // Dynamic loaders consume a two-word descriptor. The RELA addend is
    // represented in its second word for static relocation processing.
    write64le(loc + 8, val);
    break;
  case R_BEDROCK_FAR_ADDR64:
    write64le(loc, val);
    break;
  case R_BEDROCK_FAR_SEGMENT64:
    write64le(loc, rel.sym ? getFinalFarDomainImage(ctx, *rel.sym) : 0);
    break;
  case R_BEDROCK_FAR_DOMAIN64:
    write64le(loc, getFinalFarDomainImage(ctx, rel.addend));
    break;
  case R_BEDROCK_FAR_GLOB_DAT:
  case R_BEDROCK_FAR_JUMP_SLOT:
    write64le(loc, val);
    write64le(loc + 8, rel.sym ? getFinalFarDomainImage(ctx, *rel.sym) : 0);
    break;
  case R_BEDROCK_FAR_IRELATIVE:
    write64le(loc, val);
    write64le(loc + 8, 0);
    break;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

static void writeMedium(uint8_t *buf, uint32_t payload, unsigned size) {
  buf[0] = 0xc0 | ((size - 3) << 2) | ((payload >> 16) & 3);
  buf[1] = payload >> 8;
  buf[2] = payload;
}

static void writeLong(uint8_t *buf, uint32_t payload, unsigned size) {
  buf[0] = 0xc0 | ((size - 3) << 2) | ((payload >> 24) & 3);
  buf[1] = payload >> 16;
  buf[2] = payload >> 8;
  buf[3] = payload;
}

void Bedrock::writeGotPltHeader(uint8_t *buf) const {
  // GOTPLT[0] is _DYNAMIC. GOTPLT[1] and GOTPLT[2] are initialized to zero
  // and reserved for the loader's object handle and resolver entry point.
  write64le(buf, ctx.mainPart->dynamic->getVA());
  write64le(buf + 8, 0);
  write64le(buf + 16, 0);
}

void Bedrock::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  // An unresolved slot enters the relocation-index setup step of PLTn.
  write64le(buf, s.getPltVA(ctx) + 8);
}

void Bedrock::writeIgotPlt(uint8_t *buf, const Symbol &s) const {
  if (ctx.arg.writeAddends)
    write64le(buf, s.getVA(ctx));
}

void Bedrock::writePltHeader(uint8_t *buf) const {
  memset(buf, 0x01, pltHeaderSize);
  // jmp.q [pc + GOTPLT[2]]
  writeLong(buf, 0x3c388e6, 8);
  uint64_t plt = ctx.in.plt->getVA();
  write32le(buf + 4, ctx.in.gotPlt->getVA() + 16 - (plt + 8));
}

void Bedrock::writePlt(uint8_t *buf, const Symbol &sym,
                       uint64_t pltEntryAddr) const {
  memset(buf, 0x01, pltEntrySize);

  // Step 0: jump through the symbol's GOTPLT slot. The unresolved value
  // points at step 1 below; a resolved slot transfers directly to the callee.
  writeLong(buf, 0x3c388e6, 8);
  write32le(buf + 4, sym.getGotPltVA(ctx) - (pltEntryAddr + 8));

  // Step 1: R0 = lazy relocation index.
  writeMedium(buf + 8, 0x1b80e, 7);
  write32le(buf + 11, sym.getPltIdx(ctx));

  // Step 2: R1 = GOT base, preserving the ordinary C argument registers on
  // the already-resolved path.
  writeMedium(buf + 15, 0x1b886, 7);
  write32le(buf + 18, ctx.in.gotPlt->getVA() - (pltEntryAddr + 22));

  // Step 3: enter PLT0. Three one-byte NOPs retain the fixed 32-byte stride.
  writeMedium(buf + 22, 0x6600, 7);
  write32le(buf + 25, ctx.in.plt->getVA() - (pltEntryAddr + 29));
}

void elf::setBedrockTargetInfo(Ctx &ctx) {
  ctx.target.reset(new Bedrock(ctx));
}
