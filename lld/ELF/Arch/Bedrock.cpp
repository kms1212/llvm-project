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
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class Bedrock final : public TargetInfo {
public:
  Bedrock(Ctx &ctx);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

Bedrock::Bedrock(Ctx &ctx) : TargetInfo(ctx) {
  copyRel = R_BEDROCK_COPY;
  gotRel = R_BEDROCK_GLOB_DAT;
  iRelativeRel = R_BEDROCK_IRELATIVE;
  pltRel = R_BEDROCK_JUMP_SLOT;
  relativeRel = R_BEDROCK_RELATIVE;
  symbolicRel = R_BEDROCK_ABS64;

  trapInstr = {0x00, 0x00, 0x00, 0x00};
}

RelExpr Bedrock::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_BEDROCK_NONE:
    return R_NONE;
  case R_BEDROCK_ABS8:
  case R_BEDROCK_ABS16:
  case R_BEDROCK_ABS32:
  case R_BEDROCK_ABS64:
  case R_BEDROCK_IMM16:
  case R_BEDROCK_IMM32:
  case R_BEDROCK_IMM64:
  case R_BEDROCK_DISP16:
  case R_BEDROCK_DISP32:
  case R_BEDROCK_DISP64:
  case R_BEDROCK_SECTION_REL32:
  case R_BEDROCK_SECTION_REL64:
    return R_ABS;
  case R_BEDROCK_PCREL16:
  case R_BEDROCK_PCREL32:
  case R_BEDROCK_PCREL64:
  case R_BEDROCK_WORD_PCREL16:
  case R_BEDROCK_WORD_PCREL32:
  case R_BEDROCK_CALL_TARGET:
  case R_BEDROCK_JMP_TARGET:
  case R_BEDROCK_LONG_CONTROL_TARGET:
    return R_PC;
  case R_BEDROCK_PLT32:
  case R_BEDROCK_PLT64:
    return R_PLT_PC;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type.v
             << ") against symbol " << &s;
    return R_NONE;
  }
}

int64_t Bedrock::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_BEDROCK_NONE:
    return 0;
  case R_BEDROCK_ABS8:
    return *buf;
  case R_BEDROCK_ABS16:
  case R_BEDROCK_PCREL16:
  case R_BEDROCK_WORD_PCREL16:
  case R_BEDROCK_IMM16:
  case R_BEDROCK_DISP16:
    return read16le(buf);
  case R_BEDROCK_ABS32:
  case R_BEDROCK_PCREL32:
  case R_BEDROCK_WORD_PCREL32:
  case R_BEDROCK_IMM32:
  case R_BEDROCK_DISP32:
  case R_BEDROCK_SECTION_REL32:
  case R_BEDROCK_CALL_TARGET:
  case R_BEDROCK_JMP_TARGET:
  case R_BEDROCK_PLT32:
    return read32le(buf);
  case R_BEDROCK_ABS64:
  case R_BEDROCK_PCREL64:
  case R_BEDROCK_IMM64:
  case R_BEDROCK_DISP64:
  case R_BEDROCK_SECTION_REL64:
  case R_BEDROCK_LONG_CONTROL_TARGET:
  case R_BEDROCK_PLT64:
    return read64le(buf);
  default:
    InternalErr(ctx, buf) << "cannot read addend for relocation " << type;
    return 0;
  }
}

void Bedrock::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  switch (rel.type) {
  case R_BEDROCK_NONE:
    break;
  case R_BEDROCK_ABS8:
    checkIntUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_BEDROCK_ABS16:
  case R_BEDROCK_IMM16:
  case R_BEDROCK_DISP16:
    checkIntUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_BEDROCK_ABS32:
  case R_BEDROCK_IMM32:
  case R_BEDROCK_DISP32:
  case R_BEDROCK_SECTION_REL32:
    checkIntUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_BEDROCK_ABS64:
  case R_BEDROCK_IMM64:
  case R_BEDROCK_DISP64:
  case R_BEDROCK_SECTION_REL64:
    write64le(loc, val);
    break;
  case R_BEDROCK_PCREL16:
    checkInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_BEDROCK_PCREL32:
  case R_BEDROCK_CALL_TARGET:
  case R_BEDROCK_PLT32:
    checkInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_BEDROCK_PCREL64:
  case R_BEDROCK_LONG_CONTROL_TARGET:
  case R_BEDROCK_PLT64:
    write64le(loc, val);
    break;
  case R_BEDROCK_WORD_PCREL16:
    checkAlignment(ctx, loc, val, 2, rel);
    checkInt(ctx, loc, static_cast<int64_t>(val) / 2, 16, rel);
    write16le(loc, static_cast<int64_t>(val) / 2);
    break;
  case R_BEDROCK_WORD_PCREL32:
  case R_BEDROCK_JMP_TARGET:
    checkAlignment(ctx, loc, val, 2, rel);
    checkInt(ctx, loc, static_cast<int64_t>(val) / 2, 32, rel);
    write32le(loc, static_cast<int64_t>(val) / 2);
    break;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation "
             << rel.type;
  }
}

void elf::setBedrockTargetInfo(Ctx &ctx) { ctx.target.reset(new Bedrock(ctx)); }
