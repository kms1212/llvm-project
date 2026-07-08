//===-- BedrockSubtarget.cpp - Bedrock subtarget information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockSubtarget.h"

using namespace llvm;

#define DEBUG_TYPE "bedrock-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "BedrockGenSubtargetInfo.inc"

BedrockSubtarget &
BedrockSubtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  std::string CPUName = CPU.empty() ? "generic" : CPU.str();
  ParseSubtargetFeatures(CPUName, /*TuneCPU=*/CPUName, FS);
  return *this;
}

BedrockSubtarget::BedrockSubtarget(const Triple &TT, StringRef CPU,
                                   StringRef FS, const TargetMachine &TM,
                                   const TargetOptions &Options,
                                   CodeModel::Model CM,
                                   CodeGenOptLevel OptLevel)
    : BedrockGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), FrameLowering(*this),
      TLInfo(TM, *this) {}
