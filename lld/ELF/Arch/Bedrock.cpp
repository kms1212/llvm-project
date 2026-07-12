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
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
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
