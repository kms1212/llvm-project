//===-- BedrockCallingConv.h - Bedrock C ABI assignment --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKCALLINGCONV_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKCALLINGCONV_H

#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/DerivedTypes.h"

namespace llvm {

class BedrockCCState final : public CCState {
  unsigned GeneralCursor = 0;
  unsigned FloatCursor = 0;
  bool GeneralExhausted = false;
  bool FloatExhausted = false;

  bool HasPendingPair = false;
  bool PendingPairInReg = false;
  MCRegister PendingPairReg;
  int64_t PendingPairOffset = 0;

public:
  BedrockCCState(CallingConv::ID CC, bool IsVarArg, MachineFunction &MF,
                 SmallVectorImpl<CCValAssign> &Locs, LLVMContext &Context)
      : CCState(CC, IsVarArg, MF, Locs, Context) {}

  unsigned getGeneralCursor() const { return GeneralCursor; }
  void setGeneralCursor(unsigned Cursor) { GeneralCursor = Cursor; }
  unsigned getFloatCursor() const { return FloatCursor; }
  void setFloatCursor(unsigned Cursor) { FloatCursor = Cursor; }
  bool isGeneralExhausted() const { return GeneralExhausted; }
  void exhaustGeneral() {
    GeneralExhausted = true;
    GeneralCursor = 8;
  }
  bool isFloatExhausted() const { return FloatExhausted; }
  void exhaustFloat() {
    FloatExhausted = true;
    FloatCursor = 8;
  }

  bool hasPendingPair() const { return HasPendingPair; }
  void setPendingPairReg(MCRegister Reg) {
    HasPendingPair = true;
    PendingPairInReg = true;
    PendingPairReg = Reg;
  }
  void setPendingPairOffset(int64_t Offset) {
    HasPendingPair = true;
    PendingPairInReg = false;
    PendingPairOffset = Offset;
  }
  bool pendingPairIsReg() const { return PendingPairInReg; }
  MCRegister takePendingPairReg() {
    HasPendingPair = false;
    return PendingPairReg;
  }
  int64_t takePendingPairOffset() {
    HasPendingPair = false;
    return PendingPairOffset;
  }
};

inline CCValAssign::LocInfo
getBedrockIntegerLocInfo(const ISD::ArgFlagsTy &Flags) {
  if (Flags.isSExt())
    return CCValAssign::SExt;
  if (Flags.isZExt())
    return CCValAssign::ZExt;
  return CCValAssign::AExt;
}

inline bool isBedrockComplexPair(Type *Ty) {
  auto *StructTy = dyn_cast_or_null<StructType>(Ty);
  if (!StructTy || StructTy->getNumElements() != 2)
    return false;
  Type *First = StructTy->getElementType(0);
  return First == StructTy->getElementType(1) &&
         (First->isFloatTy() || First->isDoubleTy());
}

inline bool CC_Bedrock(unsigned ValNo, MVT ValVT, MVT LocVT,
                       CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                       Type *OrigTy, CCState &State) {
  auto &BState = static_cast<BedrockCCState &>(State);
  static const MCPhysReg GPRs[] = {
      Bedrock::R0, Bedrock::R1, Bedrock::R2, Bedrock::R3,
      Bedrock::R4, Bedrock::R5, Bedrock::R6, Bedrock::R7,
  };
  static const MCPhysReg FPRs[] = {
      Bedrock::F0, Bedrock::F1, Bedrock::F2, Bedrock::F3,
      Bedrock::F4, Bedrock::F5, Bedrock::F6, Bedrock::F7,
  };
  if (BState.hasPendingPair()) {
    if (BState.pendingPairIsReg())
      State.addLoc(CCValAssign::getReg(
          ValNo, ValVT, BState.takePendingPairReg(), ValVT, CCValAssign::Full));
    else
      State.addLoc(CCValAssign::getMem(ValNo, ValVT,
                                       BState.takePendingPairOffset(), ValVT,
                                       CCValAssign::Full));
    return false;
  }

  const bool IsGeneralPair = OrigTy && OrigTy->isIntegerTy(128);
  const bool IsFloatPair = isBedrockComplexPair(OrigTy) ||
                           ((ValVT == MVT::f32 || ValVT == MVT::f64) &&
                            (ArgFlags.isSplit() || ArgFlags.isInReg()));
  // The fixed and variable portions intentionally use different placement
  // rules. Every unnamed argument occupies one complete 16-byte stack slot;
  // it neither consumes nor exhausts either register cursor.
  if (ArgFlags.isVarArg()) {
    int64_t Offset = State.AllocateStack(16, Align(16));
    if (IsGeneralPair || IsFloatPair)
      BState.setPendingPairOffset(Offset +
                                  (IsFloatPair ? ValVT.getStoreSize() : 8));
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT,
                                     CCValAssign::Full));
    return false;
  }

  if (IsFloatPair) {
    unsigned Cursor = BState.getFloatCursor();
    if (!BState.isFloatExhausted() && Cursor + 1 < std::size(FPRs)) {
      MCRegister Real = State.AllocateReg(FPRs[Cursor]);
      MCRegister Imaginary = State.AllocateReg(FPRs[Cursor + 1]);
      assert(Real && Imaginary &&
             "Bedrock float-pair cursor disagrees with CC state");
      BState.setFloatCursor(Cursor + 2);
      BState.setPendingPairReg(Imaginary);
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, Real, ValVT, CCValAssign::Full));
      return false;
    }

    BState.exhaustFloat();
    int64_t Offset = State.AllocateStack(16, Align(16));
    BState.setPendingPairOffset(Offset + ValVT.getStoreSize());
    State.addLoc(
        CCValAssign::getMem(ValNo, ValVT, Offset, ValVT, CCValAssign::Full));
    return false;
  }

  if (IsGeneralPair) {
    unsigned PairStart = alignTo(BState.getGeneralCursor(), 2u);
    if (!BState.isGeneralExhausted() && PairStart + 1 < std::size(GPRs)) {
      MCRegister Low = State.AllocateReg(GPRs[PairStart]);
      MCRegister High = State.AllocateReg(GPRs[PairStart + 1]);
      assert(Low && High && "Bedrock general cursor disagrees with CC state");
      BState.setGeneralCursor(PairStart + 2);
      BState.setPendingPairReg(High);
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, Low, MVT::i64, CCValAssign::Full));
      return false;
    }

    BState.exhaustGeneral();
    int64_t Offset = State.AllocateStack(16, Align(16));
    BState.setPendingPairOffset(Offset + 8);
    State.addLoc(
        CCValAssign::getMem(ValNo, ValVT, Offset, MVT::i64, CCValAssign::Full));
    return false;
  }

  if (ValVT == MVT::f32 || ValVT == MVT::f64) {
    unsigned Cursor = BState.getFloatCursor();
    if (!BState.isFloatExhausted() && Cursor < std::size(FPRs)) {
      MCRegister Reg = State.AllocateReg(FPRs[Cursor]);
      assert(Reg && "Bedrock float cursor disagrees with CC state");
      BState.setFloatCursor(Cursor + 1);
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, Reg, ValVT, CCValAssign::Full));
      return false;
    }

    BState.exhaustFloat();
    int64_t Offset = State.AllocateStack(16, Align(16));
    State.addLoc(
        CCValAssign::getMem(ValNo, ValVT, Offset, ValVT, CCValAssign::Full));
    return false;
  }

  if (ValVT != MVT::i1 && ValVT != MVT::i8 && ValVT != MVT::i16 &&
      ValVT != MVT::i32 && ValVT != MVT::i64)
    return true;

  unsigned Cursor = BState.getGeneralCursor();
  if (!BState.isGeneralExhausted() && Cursor < std::size(GPRs)) {
    MCRegister Reg = State.AllocateReg(GPRs[Cursor]);
    assert(Reg && "Bedrock general cursor disagrees with CC state");
    BState.setGeneralCursor(Cursor + 1);

    MVT RegVT = MVT::i64;
    CCValAssign::LocInfo RegInfo = ValVT == MVT::i64
                                       ? CCValAssign::Full
                                       : getBedrockIntegerLocInfo(ArgFlags);
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, RegVT, RegInfo));
    return false;
  }

  BState.exhaustGeneral();
  int64_t Offset = State.AllocateStack(16, Align(16));
  State.addLoc(
      CCValAssign::getMem(ValNo, ValVT, Offset, ValVT, CCValAssign::Full));
  return false;
}

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKCALLINGCONV_H
