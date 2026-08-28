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
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {

class MCInst;

namespace BedrockMC {

struct BedrockISAForm {
  const char *Id;
  const char *Mnemonic;
  const char *Syntax;
  const char *Pattern;
  const char *EncodingClass;
  const char *Owner;
  uint8_t PrimaryBytes;
  uint8_t FixedPayloadBytes;
  bool HasEffectiveAddress;
  bool HasVariableLength;
  bool TableGenCodecCandidate;
  const char *FieldMarkers;
  const char *FieldRoles;
  const char *FieldTypes;
  const char *PayloadRoles;
  const char *PayloadTypes;
};

enum class ScalarOperandKind : uint8_t {
  None,
  Condition,
  GPR,
  FPR,
  Vector,
  Predicate,
  EA,
  VEA,
  Immediate,
  TailSigned,
  TailUnsigned,
  Segment,
  FixedSP,
  FixedCS,
  FixedImmediate,
  FEA,
  MemoryOrder,
  RegisterSelector
};

struct ScalarOperandDesc {
  ScalarOperandKind Kind;
  char Field;
  uint8_t Width;
  bool Signed;
  bool AllowImmediateEA;
  uint64_t FixedValue;
  uint64_t AllowedMask[4];

  bool allows(unsigned Value) const {
    return Value < 256 && (AllowedMask[Value / 64] & (uint64_t(1) << (Value % 64)));
  }
};

struct ScalarEncodingForm {
  const char *Id;
  const char *Mnemonic;
  const char *Pattern;
  uint8_t FixedPayloadBytes;
  const char *Suffixes;
  uint8_t SuffixField;
  uint16_t AllowedSuffixMask;
  uint8_t ConditionField;
  uint16_t AllowedConditionMask;
  uint8_t OperandCount;
#define BEDROCK_SCALAR_OPERAND_FIELDS(N)                                    \
  uint8_t Operand##N##Kind;                                                 \
  uint8_t Operand##N##Field;                                                \
  uint8_t Operand##N##Width;                                                \
  bool Operand##N##Signed;                                                  \
  bool Operand##N##AllowImmediateEA;                                        \
  uint64_t Operand##N##FixedValue;                                          \
  uint64_t Operand##N##AllowedMask0;                                        \
  uint64_t Operand##N##AllowedMask1;                                        \
  uint64_t Operand##N##AllowedMask2;                                        \
  uint64_t Operand##N##AllowedMask3
  BEDROCK_SCALAR_OPERAND_FIELDS(0);
  BEDROCK_SCALAR_OPERAND_FIELDS(1);
  BEDROCK_SCALAR_OPERAND_FIELDS(2);
  BEDROCK_SCALAR_OPERAND_FIELDS(3);
#undef BEDROCK_SCALAR_OPERAND_FIELDS
  uint8_t DistinctOperandA;
  uint8_t DistinctOperandB;

  ScalarOperandDesc operand(unsigned Index) const;
  int distinctOperandA() const {
    return DistinctOperandA == UINT8_MAX ? -1 : DistinctOperandA;
  }
  int distinctOperandB() const {
    return DistinctOperandB == UINT8_MAX ? -1 : DistinctOperandB;
  }
};

enum class VectorEncodingClass : uint8_t { Long, ExtraLong, Xxlong };
enum class VectorOperandKind : uint8_t {
  None,
  Condition,
  GPR,
  FPR,
  Vector,
  Predicate,
  EA,
  VEA,
  Immediate,
  TailSigned,
  TailUnsigned
};

struct VectorOperandDesc {
  VectorOperandKind Kind;
  char Field;
  uint8_t Width;
  bool AllowImmediateEA;
};

struct VectorEncodingForm {
  const char *Id;
  const char *Mnemonic;
  const char *Pattern;
  const char *Suffixes;
  uint8_t EncodingClass;
  uint8_t SuffixField;
  uint8_t AllowedSuffixMask;
  uint16_t AllowedConditionMask;
  bool HasCondition;
  bool HasWidthOnlyAliases;
  uint8_t OperandCount;
#define BEDROCK_VECTOR_OPERAND_FIELDS(N)                                    \
  uint8_t Operand##N##Kind;                                                 \
  uint8_t Operand##N##Field;                                                \
  uint8_t Operand##N##Width;                                                \
  bool Operand##N##AllowImmediateEA
  BEDROCK_VECTOR_OPERAND_FIELDS(0);
  BEDROCK_VECTOR_OPERAND_FIELDS(1);
  BEDROCK_VECTOR_OPERAND_FIELDS(2);
  BEDROCK_VECTOR_OPERAND_FIELDS(3);
  BEDROCK_VECTOR_OPERAND_FIELDS(4);
  BEDROCK_VECTOR_OPERAND_FIELDS(5);
#undef BEDROCK_VECTOR_OPERAND_FIELDS
  uint8_t DistinctOperandA;
  uint8_t DistinctOperandB;

  VectorEncodingClass encodingClass() const {
    return static_cast<VectorEncodingClass>(EncodingClass);
  }
  VectorOperandDesc operand(unsigned Index) const;
  int distinctOperandA() const {
    return DistinctOperandA == UINT8_MAX ? -1 : DistinctOperandA;
  }
  int distinctOperandB() const {
    return DistinctOperandB == UINT8_MAX ? -1 : DistinctOperandB;
  }
};

struct RepeatEligibility {
  const char *Mnemonic;
  bool HasCondition;
  bool AllowsREP;
  bool AllowsREPcc;
};

struct RegisterSelector {
  uint8_t Group;
  const char *Name;
  uint64_t Encoding;
};

#define GET_BedrockISAForms_DECL
#define GET_BedrockRepeatEligibilityTable_DECL
#define GET_BedrockRegisterSelectors_DECL
#define GET_BedrockScalarEncodingForms_DECL
#define GET_BedrockVectorEncodingForms_DECL
#include "BedrockGenSearchableTables.inc"

ArrayRef<VectorEncodingForm> vectorEncodingForms();
ArrayRef<ScalarEncodingForm> scalarEncodingForms();
ArrayRef<RepeatEligibility> repeatEligibilityEntries();
bool isRegisterSelectorName(StringRef Name);
bool isReservedAssemblyName(StringRef Name);
bool lookupRegisterSelector(unsigned Group, StringRef Name,
                            uint64_t &Encoding);

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
bool encodeXxlong(uint64_t Payload, ArrayRef<uint8_t> Tail,
                  SmallVectorImpl<uint8_t> &Bytes);
bool decodeRawInst(ArrayRef<uint8_t> Bytes, uint64_t &Size,
                   SmallString<128> &Text);
bool getInstructionSize(ArrayRef<uint8_t> Bytes, uint64_t &Size);

} // namespace BedrockMC
} // namespace llvm

#endif
