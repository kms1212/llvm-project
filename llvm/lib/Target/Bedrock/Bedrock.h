//===-- Bedrock.h - Top-level interface for Bedrock ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCK_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCK_H

#include "llvm/Target/TargetMachine.h"

namespace llvm {

namespace BedrockII {
enum TargetOperandFlag : unsigned {
  MO_NONE,
  MO_ABS32,
  MO_ABS64,
  MO_PCREL32,
  MO_PCREL64,
  MO_GOTPCREL32,
  MO_GOTPCREL64,
  MO_PLT32,
  MO_PLT64,
  MO_TLS_LE32,
  MO_TLS_LE64,
  MO_TLSDESC32,
  MO_TLSDESC64,
  MO_TLSDESC_CALL,
};
} // namespace BedrockII

namespace BedrockVectorPseudo {
enum BinaryOperation : unsigned {
  Add,
  Sub,
  Mul,
  And,
  Or,
  Xor,
  MinSigned,
  MinUnsigned,
  MaxSigned,
  MaxUnsigned,
  MinFP,
  MaxFP,
  DivFP,
};
} // namespace BedrockVectorPseudo

class BedrockTargetMachine;
class FunctionPass;
class PassRegistry;

FunctionPass *createBedrockISelDag(BedrockTargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
FunctionPass *createBedrockPreEmitPeepholePass();

void initializeBedrockAsmPrinterPass(PassRegistry &);
void initializeBedrockDAGToDAGISelLegacyPass(PassRegistry &);
void initializeBedrockPreEmitPeepholePass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCK_H
