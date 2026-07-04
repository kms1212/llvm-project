//===-- BedrockCondCode.h - Bedrock condition codes ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKCONDCODE_H
#define LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKCONDCODE_H

namespace llvm {
namespace BedrockCC {

enum CondCode : unsigned {
  T = 0x0,
  F = 0x1,
  EQ = 0x2,
  NE = 0x3,
  ULT = 0x4,
  UGE = 0x5,
  MI = 0x6,
  PL = 0x7,
  VS = 0x8,
  VC = 0x9,
  ULE = 0xa,
  UGT = 0xb,
  LT = 0xc,
  GE = 0xd,
  LE = 0xe,
  GT = 0xf,
};

} // end namespace BedrockCC
} // end namespace llvm

#endif
