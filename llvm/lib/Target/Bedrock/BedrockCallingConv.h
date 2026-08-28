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

namespace BedrockCABI {

#define BEDROCK_C_CALLING_CONVENTION(STACK_POINTER, GROWTH, ENTRY_ALIGNMENT,   \
                                     FIRST_ARGUMENT_OFFSET, ARGUMENT_SLOT,     \
                                     SRET_REGISTER, RED_ZONE)                  \
  inline constexpr unsigned EntryAlignment = ENTRY_ALIGNMENT;                  \
  inline constexpr unsigned FirstArgumentOffset = FIRST_ARGUMENT_OFFSET;       \
  inline constexpr unsigned ArgumentSlot = ARGUMENT_SLOT;                      \
  inline constexpr unsigned RedZone = RED_ZONE;
#include "llvm/TargetParser/BedrockGenCABI.inc"
#undef BEDROCK_C_CALLING_CONVENTION

#define BEDROCK_C_EMIT_GENERAL(REG) Bedrock::REG,
#define BEDROCK_C_EMIT_FLOATING(REG)
#define BEDROCK_C_EMIT_VECTOR(REG)
#define BEDROCK_C_EMIT_PREDICATE(REG)
#define BEDROCK_C_ARGUMENT_REGISTER(CLASS, INDEX, REG)                         \
  BEDROCK_C_EMIT_##CLASS(REG)
inline constexpr MCPhysReg GeneralArgumentRegisters[] = {
#include "llvm/TargetParser/BedrockGenCABI.inc"
};
#undef BEDROCK_C_ARGUMENT_REGISTER
#undef BEDROCK_C_EMIT_GENERAL
#undef BEDROCK_C_EMIT_FLOATING
#undef BEDROCK_C_EMIT_VECTOR
#undef BEDROCK_C_EMIT_PREDICATE

#define BEDROCK_C_EMIT_GENERAL(REG)
#define BEDROCK_C_EMIT_FLOATING(REG) Bedrock::REG,
#define BEDROCK_C_EMIT_VECTOR(REG)
#define BEDROCK_C_EMIT_PREDICATE(REG)
#define BEDROCK_C_ARGUMENT_REGISTER(CLASS, INDEX, REG)                         \
  BEDROCK_C_EMIT_##CLASS(REG)
inline constexpr MCPhysReg FloatingArgumentRegisters[] = {
#include "llvm/TargetParser/BedrockGenCABI.inc"
};
#undef BEDROCK_C_ARGUMENT_REGISTER
#undef BEDROCK_C_EMIT_GENERAL
#undef BEDROCK_C_EMIT_FLOATING
#undef BEDROCK_C_EMIT_VECTOR
#undef BEDROCK_C_EMIT_PREDICATE

#define BEDROCK_C_EMIT_GENERAL(REG)
#define BEDROCK_C_EMIT_FLOATING(REG)
#define BEDROCK_C_EMIT_VECTOR(REG) Bedrock::REG,
#define BEDROCK_C_EMIT_PREDICATE(REG)
#define BEDROCK_C_ARGUMENT_REGISTER(CLASS, INDEX, REG)                         \
  BEDROCK_C_EMIT_##CLASS(REG)
inline constexpr MCPhysReg VectorArgumentRegisters[] = {
#include "llvm/TargetParser/BedrockGenCABI.inc"
};
#undef BEDROCK_C_ARGUMENT_REGISTER
#undef BEDROCK_C_EMIT_GENERAL
#undef BEDROCK_C_EMIT_FLOATING
#undef BEDROCK_C_EMIT_VECTOR
#undef BEDROCK_C_EMIT_PREDICATE

#define BEDROCK_C_EMIT_GENERAL(REG)
#define BEDROCK_C_EMIT_FLOATING(REG)
#define BEDROCK_C_EMIT_VECTOR(REG)
#define BEDROCK_C_EMIT_PREDICATE(REG) Bedrock::REG,
#define BEDROCK_C_ARGUMENT_REGISTER(CLASS, INDEX, REG)                         \
  BEDROCK_C_EMIT_##CLASS(REG)
inline constexpr MCPhysReg PredicateArgumentRegisters[] = {
#include "llvm/TargetParser/BedrockGenCABI.inc"
};
#undef BEDROCK_C_ARGUMENT_REGISTER
#undef BEDROCK_C_EMIT_GENERAL
#undef BEDROCK_C_EMIT_FLOATING
#undef BEDROCK_C_EMIT_VECTOR
#undef BEDROCK_C_EMIT_PREDICATE

} // namespace BedrockCABI

class BedrockCCState final : public CCState {
  unsigned GeneralCursor = 0;
  unsigned FloatCursor = 0;
  unsigned VectorCursor = 0;
  unsigned PredicateCursor = 0;
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
  unsigned getVectorCursor() const { return VectorCursor; }
  void setVectorCursor(unsigned Cursor) { VectorCursor = Cursor; }
  unsigned getPredicateCursor() const { return PredicateCursor; }
  void setPredicateCursor(unsigned Cursor) { PredicateCursor = Cursor; }
  bool isGeneralExhausted() const { return GeneralExhausted; }
  void exhaustGeneral() {
    GeneralExhausted = true;
    GeneralCursor = std::size(BedrockCABI::GeneralArgumentRegisters);
  }
  bool isFloatExhausted() const { return FloatExhausted; }
  void exhaustFloat() {
    FloatExhausted = true;
    FloatCursor = std::size(BedrockCABI::FloatingArgumentRegisters);
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
  ArrayRef<MCPhysReg> GPRs = BedrockCABI::GeneralArgumentRegisters;
  ArrayRef<MCPhysReg> FPRs = BedrockCABI::FloatingArgumentRegisters;
  ArrayRef<MCPhysReg> VRs = BedrockCABI::VectorArgumentRegisters;
  ArrayRef<MCPhysReg> PRs = BedrockCABI::PredicateArgumentRegisters;
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
  const bool IsPredicate =
      ValVT.isScalableVector() && ValVT.getVectorElementType() == MVT::i1;
  const bool IsVector = ValVT.isScalableVector() && !IsPredicate;
  if (IsVector || IsPredicate) {
    unsigned Cursor = IsPredicate ? BState.getPredicateCursor()
                                  : BState.getVectorCursor();
    ArrayRef<MCPhysReg> Regs = IsPredicate ? PRs : VRs;
    if (!ArgFlags.isVarArg() && Cursor < Regs.size()) {
      MCRegister Reg = State.AllocateReg(Regs[Cursor]);
      assert(Reg && "Bedrock scalable cursor disagrees with CC state");
      if (IsPredicate)
        BState.setPredicateCursor(Cursor + 1);
      else
        BState.setVectorCursor(Cursor + 1);
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, Reg, ValVT, CCValAssign::Full));
      return false;
    }

    // The caller owns the complete scalable object. The ABI passes its
    // address as a GENERAL value, except that an unnamed variadic argument
    // forces that pointer itself into a complete 16-byte stack slot.
    if (!ArgFlags.isVarArg()) {
      unsigned GeneralCursor = BState.getGeneralCursor();
      if (!BState.isGeneralExhausted() &&
          GeneralCursor < std::size(GPRs)) {
        MCRegister Reg = State.AllocateReg(GPRs[GeneralCursor]);
        assert(Reg && "Bedrock indirect cursor disagrees with CC state");
        BState.setGeneralCursor(GeneralCursor + 1);
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, MVT::i64,
                                         CCValAssign::Indirect));
        return false;
      }
      BState.exhaustGeneral();
    }
    int64_t Offset = State.AllocateStack(
        BedrockCABI::ArgumentSlot, Align(BedrockCABI::ArgumentSlot));
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, MVT::i64,
                                     CCValAssign::Indirect));
    return false;
  }

  // The fixed and variable portions intentionally use different placement
  // rules. Every unnamed argument occupies one complete 16-byte stack slot;
  // it neither consumes nor exhausts either register cursor.
  if (ArgFlags.isVarArg()) {
    int64_t Offset = State.AllocateStack(
        BedrockCABI::ArgumentSlot, Align(BedrockCABI::ArgumentSlot));
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
    int64_t Offset = State.AllocateStack(
        BedrockCABI::ArgumentSlot, Align(BedrockCABI::ArgumentSlot));
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
    int64_t Offset = State.AllocateStack(
        BedrockCABI::ArgumentSlot, Align(BedrockCABI::ArgumentSlot));
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
    int64_t Offset = State.AllocateStack(
        BedrockCABI::ArgumentSlot, Align(BedrockCABI::ArgumentSlot));
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
  int64_t Offset = State.AllocateStack(
      BedrockCABI::ArgumentSlot, Align(BedrockCABI::ArgumentSlot));
  State.addLoc(
      CCValAssign::getMem(ValNo, ValVT, Offset, ValVT, CCValAssign::Full));
  return false;
}

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKCALLINGCONV_H
