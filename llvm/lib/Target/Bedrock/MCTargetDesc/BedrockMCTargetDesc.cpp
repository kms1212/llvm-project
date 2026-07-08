//===-- BedrockMCTargetDesc.cpp - Bedrock target descriptions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockMCTargetDesc.h"
#include "BedrockInstPrinter.h"
#include "BedrockMCAsmInfo.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "BedrockGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "BedrockGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "BedrockGenRegisterInfo.inc"

static MCInstrInfo *createBedrockMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitBedrockMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createBedrockMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitBedrockMCRegisterInfo(X, Bedrock::R0);
  return X;
}

static MCSubtargetInfo *
createBedrockMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createBedrockMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createBedrockMCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TT,
                                         const MCTargetOptions &Options) {
  return new BedrockMCAsmInfo(TT);
}

static MCInstPrinter *createBedrockMCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new BedrockInstPrinter(MAI, MII, MRI);
  return nullptr;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockTargetMC() {
  Target &T = getTheBedrockTarget();

  TargetRegistry::RegisterMCAsmInfo(T, createBedrockMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createBedrockMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createBedrockMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createBedrockMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createBedrockMCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createBedrockMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createBedrockAsmBackend);
}
