//===- Bedrock.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
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
};
} // namespace

Bedrock::Bedrock(Ctx &ctx) : TargetInfo(ctx) {
  copyRel = R_BEDROCK_COPY;
  gotRel = R_BEDROCK_GLOB_DAT;
  pltRel = R_BEDROCK_JUMP_SLOT;
  relativeRel = R_BEDROCK_RELATIVE;
  iRelativeRel = R_BEDROCK_IRELATIVE;
  symbolicRel = R_BEDROCK_ABS64;
  defaultImageBase = 0x10000;
  trapInstr = {0x20, 0x42, 0x20, 0x42};
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
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setBedrockTargetInfo(Ctx &ctx) {
  ctx.target.reset(new Bedrock(ctx));
}
