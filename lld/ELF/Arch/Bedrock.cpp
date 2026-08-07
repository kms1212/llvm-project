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
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  void writeGotPltHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &) const override;
  void writeIgotPlt(uint8_t *buf, const Symbol &) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  int64_t getImplicitAddend(const uint8_t *buf,
                            RelType type) const override;
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
  gotEntrySize = 8;
  gotPltHeaderEntriesNum = 3;
  pltHeaderSize = 0;
  pltEntrySize = 32;
  ipltEntrySize = 32;
  defaultImageBase = 0x10000;
  trapInstr = {0x00, 0x00, 0x00, 0x00};
}

static uint32_t getEFlags(ELFFileBase *file) {
  return file->getObj<object::ELF64LE>().getHeader().e_flags;
}

uint32_t Bedrock::calcEFlags() const {
  auto checkHeader = [&](ELFFileBase *file) {
    if (file->osabi != ELFOSABI_NONE)
      ErrAlways(ctx) << file << ": unrecognized ELF OSABI: "
                     << static_cast<unsigned>(file->osabi);
    if (file->abiVersion != 0)
      ErrAlways(ctx) << file
                     << ": unrecognized ELF ABI version: "
                     << static_cast<unsigned>(file->abiVersion);
    if (uint32_t flags = getEFlags(file))
      ErrAlways(ctx) << file << ": unrecognized e_flags: " << flags;
  };
  for (ELFFileBase *file : ctx.objectFiles)
    checkHeader(file);
  for (ELFFileBase *file : ctx.sharedFiles)
    checkHeader(file);
  return 0;
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
  case R_BEDROCK_SECTION_REL32:
  case R_BEDROCK_SECTION_REL64:
    return RE_BEDROCK_SECTION_REL;
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
      // LEN 15, MOV.Q tpoff64, R0. The ELF addend includes the three-byte
      // field offset; it is not part of the semantic TLS subobject offset.
      start[0] = 0xf0;
      start[1] = 0x38;
      start[2] = 0x6f;
      write64le(start + 3, val - 3);
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
      uint8_t *start = loc - 3;
      // LEN 11, MOV.Q tpoff32, R0. See the 64-bit form above.
      int64_t tlsOffset = static_cast<int64_t>(val) - 3;
      start[0] = 0xe0;
      start[1] = 0x38;
      start[2] = 0x6e;
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
  // ABI relocation field at +4 carries an addend of 4.
  buf[0] = 0xe7;
  buf[1] = 0xc9;
  buf[2] = 0x80;
  buf[3] = 0x67;
  write64le(buf + 4, sym.getGotPltVA(ctx) - pltEntryAddr);
}

void elf::setBedrockTargetInfo(Ctx &ctx) {
  ctx.target.reset(new Bedrock(ctx));
}
