//===-- BedrockMCAsmInfo.h - Bedrock asm properties ------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKMCASMINFO_H
#define LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class Triple;

class BedrockMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit BedrockMCAsmInfo(const Triple &TT);
  bool isValidUnquotedName(StringRef Name) const override;
};

} // namespace llvm

#endif
