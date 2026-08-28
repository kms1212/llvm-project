//===-- BedrockSubtarget.h - Bedrock subtarget information ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKSUBTARGET_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKSUBTARGET_H

#include "BedrockFrameLowering.h"
#include "BedrockISelLowering.h"
#include "BedrockInstrInfo.h"
#include "BedrockSelectionDAGInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Target/TargetMachine.h"

#define GET_SUBTARGETINFO_HEADER
#include "BedrockGenSubtargetInfo.inc"

namespace llvm {

class BedrockSubtarget : public BedrockGenSubtargetInfo {
  bool HasFPU = false;
  bool HasFPTRANSA = false;
  bool HasVector = false;
  BedrockInstrInfo InstrInfo;
  BedrockFrameLowering FrameLowering;
  BedrockTargetLowering TLInfo;
  BedrockSelectionDAGInfo TSInfo;

public:
  BedrockSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                   const TargetMachine &TM, const TargetOptions &Options,
                   CodeModel::Model CM, CodeGenOptLevel OptLevel);

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
  BedrockSubtarget &initializeSubtargetDependencies(StringRef CPU,
                                                    StringRef FS);

  bool hasFPU() const { return HasFPU; }
  bool hasFPTRANSA() const { return HasFPTRANSA; }
  bool hasVector() const { return HasVector; }
  const BedrockInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const TargetFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const BedrockRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const BedrockTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const BedrockSelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKSUBTARGET_H
