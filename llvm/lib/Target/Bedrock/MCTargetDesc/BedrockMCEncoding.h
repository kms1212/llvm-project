//===-- BedrockMCEncoding.h - Bedrock MC encoding helpers -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKMCENCODING_H
#define LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKMCENCODING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace llvm {

class MCInst;

namespace BedrockMC {

void createRawInst(ArrayRef<uint8_t> Bytes, MCInst &Inst);
bool getRawInstBytes(const MCInst &Inst, SmallVectorImpl<uint8_t> &Bytes);

bool encodeExtraShort(uint8_t Payload, SmallVectorImpl<uint8_t> &Bytes);
bool encodeShort(uint16_t Payload, SmallVectorImpl<uint8_t> &Bytes);
bool encodeMedium(uint32_t Payload, ArrayRef<uint8_t> Tail,
                  SmallVectorImpl<uint8_t> &Bytes);
bool encodeLong(uint32_t Payload, ArrayRef<uint8_t> Tail,
                SmallVectorImpl<uint8_t> &Bytes);
bool encodeExtraLong(uint64_t Payload, ArrayRef<uint8_t> Tail,
                     SmallVectorImpl<uint8_t> &Bytes);
bool decodeRawInst(ArrayRef<uint8_t> Bytes, uint64_t &Size,
                   SmallString<128> &Text);
bool getInstructionSize(ArrayRef<uint8_t> Bytes, uint64_t &Size);

} // namespace BedrockMC
} // namespace llvm

#endif
