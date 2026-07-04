//===-- BedrockSubtarget.h - Bedrock Subtarget ----------------*- C++ -*-===//
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
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_SUBTARGETINFO_HEADER
#include "BedrockGenSubtargetInfo.inc"

namespace llvm {

class BedrockSubtarget : public BedrockGenSubtargetInfo {
  Triple TargetTriple;
  BedrockInstrInfo InstrInfo;
  BedrockFrameLowering FrameLowering;
  BedrockTargetLowering TLInfo;
  BedrockSelectionDAGInfo TSInfo;

public:
  BedrockSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                   const TargetMachine &TM);

  const BedrockInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const BedrockRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const BedrockFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const BedrockTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const BedrockSelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
  void initLibcallLoweringInfo(LibcallLoweringInfo &Info) const override;

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
};

} // end namespace llvm

#endif
