//===-- BedrockMCTargetDesc.h - Bedrock Target Descriptions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKMCTARGETDESC_H
#define LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKMCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class StringRef;
class Target;
class Triple;
class raw_pwrite_stream;
class raw_ostream;

Target &getTheBedrockTarget();

MCCodeEmitter *createBedrockMCCodeEmitter(const MCInstrInfo &MCII,
                                          MCContext &Ctx);
MCAsmBackend *createBedrockAsmBackend(const Target &T,
                                      const MCSubtargetInfo &STI,
                                      const MCRegisterInfo &MRI,
                                      const MCTargetOptions &Options);
std::unique_ptr<MCObjectTargetWriter>
createBedrockELFObjectWriter(uint8_t OSABI);

namespace Bedrock {
enum MCInstFlagBits : unsigned {
  RepgStart = 1u << 0,
  RepgEnd = 1u << 1,
  DecodedInst = 1u << 2,
  RepgCounterShift = 8,
  RepgCounterMask = 0x7u << RepgCounterShift,
  DeclaredLenShift = 16,
  DeclaredLenMask = 0xfu << DeclaredLenShift,
};

enum OperandUpdateMode : unsigned {
  UpdateNone = 0x00,
  UpdatePostInc = 0x04,
  UpdatePreInc = 0x05,
  UpdatePostDec = 0x06,
  UpdatePreDec = 0x07,
};
} // namespace Bedrock
} // end namespace llvm

#define GET_REGINFO_ENUM
#include "BedrockGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "BedrockGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "BedrockGenSubtargetInfo.inc"

#endif
