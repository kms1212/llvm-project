//===-- BedrockISelLowering.cpp - Bedrock DAG lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockISelLowering.h"
#include "Bedrock.h"
#include "BedrockCallingConv.h"
#include "BedrockMachineFunctionInfo.h"
#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsBedrock.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <cmath>

using namespace llvm;

#define DEBUG_TYPE "bedrock-isel-lowering"

#include "BedrockGenCallingConv.inc"

BedrockTargetLowering::BedrockTargetLowering(const TargetMachine &TM,
                                             const BedrockSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  addRegisterClass(MVT::i32, &Bedrock::GPR64RegClass);
  addRegisterClass(MVT::i64, &Bedrock::GPR64RegClass);
  addRegisterClass(MVT::f32, &Bedrock::FPR64RegClass);
  addRegisterClass(MVT::f64, &Bedrock::FPR64RegClass);

  setBooleanContents(ZeroOrOneBooleanContent);
  for (MVT VT : {MVT::i32, MVT::i64}) {
    setOperationAction(ISD::ABS, VT, Legal);
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::ROTL, VT, Legal);
    setOperationAction(ISD::ROTR, VT, Legal);
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::SELECT, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::SMAX, VT, Custom);
    setOperationAction(ISD::SMIN, VT, Custom);
    setOperationAction(ISD::UMAX, VT, Custom);
    setOperationAction(ISD::UMIN, VT, Custom);
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Custom);
    setOperationAction(ISD::CTLZ, VT, Legal);
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Legal);
    setOperationAction(ISD::CTTZ, VT, Legal);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Legal);
    setOperationAction(ISD::CTPOP, VT, Legal);
    setOperationAction(ISD::PARITY, VT, Legal);
    setOperationAction(ISD::BSWAP, VT, Legal);
    setOperationAction(ISD::FSHL, VT, Custom);
    setOperationAction(ISD::FSHR, VT, Custom);
    setOperationAction(ISD::ADDC, VT, Legal);
    setOperationAction(ISD::ADDE, VT, Legal);
    setOperationAction(ISD::SUBC, VT, Legal);
    setOperationAction(ISD::SUBE, VT, Legal);
    setOperationAction(ISD::CLMUL, VT, Legal);
    setOperationAction(ISD::SADDO, VT, Custom);
    setOperationAction(ISD::SSUBO, VT, Custom);
    setOperationAction(ISD::UADDO, VT, Custom);
    setOperationAction(ISD::USUBO, VT, Custom);

    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i8, Legal);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i8, Legal);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i16, Legal);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i16, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i16, Legal);
  }
  setOperationAction(ISD::MULHU, MVT::i32, Expand);
  setOperationAction(ISD::MULHS, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::MULHU, MVT::i64, Legal);
  setOperationAction(ISD::MULHS, MVT::i64, Legal);
  setOperationAction(ISD::CLMULH, MVT::i64, Legal);
  setOperationAction(ISD::UDIVREM, MVT::i32, Legal);
  setOperationAction(ISD::UDIVREM, MVT::i64, Legal);
  setOperationAction(ISD::SDIVREM, MVT::i32, Legal);
  setOperationAction(ISD::SDIVREM, MVT::i64, Legal);
  setTargetDAGCombine({ISD::ADD, ISD::UDIV, ISD::UREM, ISD::SDIV, ISD::SREM});
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SHL_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i64, Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::i64, MVT::i32, Legal);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MVT::i32, Legal);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, MVT::i32, Legal);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);

  setTruncStoreAction(MVT::i32, MVT::i8, Legal);
  setTruncStoreAction(MVT::i32, MVT::i16, Legal);
  setTruncStoreAction(MVT::i64, MVT::i8, Legal);
  setTruncStoreAction(MVT::i64, MVT::i16, Legal);
  setTruncStoreAction(MVT::i64, MVT::i32, Legal);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Custom);
  setOperationAction(ISD::TRAP, MVT::Other, Legal);
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Legal);
  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Legal);
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::ConstantFP, VT, Expand);
    setOperationAction(ISD::FADD, VT, Legal);
    setOperationAction(ISD::FSUB, VT, Legal);
    setOperationAction(ISD::FMUL, VT, Legal);
    setOperationAction(ISD::FDIV, VT, Legal);
    setOperationAction(ISD::FREM, VT, Legal);
    setOperationAction(ISD::FMA, VT, Legal);
    setOperationAction(ISD::FABS, VT, Legal);
    setOperationAction(ISD::FNEG, VT, Legal);
    setOperationAction(ISD::FCOPYSIGN, VT, Legal);
    setOperationAction(ISD::FMINIMUMNUM, VT, Legal);
    setOperationAction(ISD::FMAXIMUMNUM, VT, Legal);
    setOperationAction(ISD::FSQRT, VT, Legal);
    setOperationAction(ISD::FROUNDEVEN, VT, Legal);
    setOperationAction(ISD::FTRUNC, VT, Legal);
    setOperationAction(ISD::FCEIL, VT, Legal);
    setOperationAction(ISD::FFLOOR, VT, Legal);
    setOperationAction(ISD::FRINT, VT, Legal);
    for (unsigned Opcode :
         {ISD::STRICT_FADD, ISD::STRICT_FSUB, ISD::STRICT_FMUL,
          ISD::STRICT_FDIV, ISD::STRICT_FREM, ISD::STRICT_FMA,
          ISD::STRICT_FSQRT, ISD::STRICT_FROUNDEVEN, ISD::STRICT_FTRUNC,
          ISD::STRICT_FCEIL, ISD::STRICT_FFLOOR, ISD::STRICT_FRINT})
      setOperationAction(Opcode, VT, Legal);
    setOperationAction(ISD::FLDEXP, VT, Custom);
    setOperationAction(ISD::STRICT_FLDEXP, VT, Custom);
    setOperationAction(ISD::FFREXP, VT, Custom);
    setOperationAction(ISD::IS_FPCLASS, VT, Custom);
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::STRICT_FSETCC, VT, Custom);
    setOperationAction(ISD::STRICT_FSETCCS, VT, Custom);
    setOperationAction(ISD::SELECT, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    for (unsigned Opcode :
         {ISD::FACOS, ISD::FASIN, ISD::FATAN, ISD::FCOS, ISD::FCOSH,
          ISD::FEXP, ISD::FEXP2, ISD::FEXP10, ISD::FLOG, ISD::FLOG2,
          ISD::FLOG10, ISD::FSIN, ISD::FSINCOS, ISD::FSINH, ISD::FTAN,
          ISD::FTANH})
      setOperationAction(Opcode, VT, Expand);
    setOperationAction(ISD::SINT_TO_FP, VT, Legal);
    setOperationAction(ISD::UINT_TO_FP, VT, Legal);
  }
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Legal);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Legal);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Legal);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Legal);
  setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i32, Legal);
  setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i64, Legal);
  setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i32, Legal);
  setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i64, Legal);
  for (MVT VT : {MVT::i32, MVT::i64}) {
    setOperationAction(ISD::SINT_TO_FP, VT, Legal);
    setOperationAction(ISD::UINT_TO_FP, VT, Legal);
    setOperationAction(ISD::STRICT_SINT_TO_FP, VT, Legal);
    setOperationAction(ISD::STRICT_UINT_TO_FP, VT, Legal);
  }
  setOperationAction(ISD::FP_ROUND, MVT::f32, Legal);
  setOperationAction(ISD::FP_EXTEND, MVT::f64, Legal);
  setOperationAction(ISD::STRICT_FP_ROUND, MVT::f32, Legal);
  setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f64, Legal);
  setTargetDAGCombine(ISD::ConstantFP);
  if (Subtarget.hasFPTRANSA())
    setTargetDAGCombine(
        {ISD::FACOS, ISD::FASIN, ISD::FATAN, ISD::FCOS, ISD::FCOSH,
         ISD::FEXP, ISD::FEXP2, ISD::FEXP10, ISD::FLOG, ISD::FLOG2,
         ISD::FLOG10, ISD::FSIN, ISD::FSINCOS, ISD::FSINH, ISD::FTAN,
         ISD::FTANH});
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);
  setMaxAtomicSizeInBitsSupported(64);
  setMinimumJumpTableEntries(16);
  setMinFunctionAlignment(Align(16));

  computeRegisterProperties(Subtarget.getRegisterInfo());
}

bool BedrockTargetLowering::enableAggressiveFMAFusion(EVT VT) const {
  return VT == MVT::f32 || VT == MVT::f64;
}

bool BedrockTargetLowering::isCheapToSpeculateCtlz(Type *Ty) const {
  return Ty->isIntegerTy(32) || Ty->isIntegerTy(64);
}

bool BedrockTargetLowering::isCheapToSpeculateCttz(Type *Ty) const {
  return Ty->isIntegerTy(32) || Ty->isIntegerTy(64);
}

bool BedrockTargetLowering::isFMAFasterThanFMulAndFAdd(
    const MachineFunction &MF, EVT VT) const {
  return isFMAFasterThanFMulAndFAdd(
      MF.getFunction(), VT.getTypeForEVT(MF.getFunction().getContext()));
}

bool BedrockTargetLowering::isFMAFasterThanFMulAndFAdd(
    const Function &F, Type *Ty) const {
  return Ty->isFloatTy() || Ty->isDoubleTy();
}

bool BedrockTargetLowering::shouldSignExtendTypeInLibCall(Type *Ty,
                                                          bool IsSigned) const {
  // Every baseline libcall parameter with C type int follows the Bedrock C
  // ABI's signed GENERAL rule, even when the operation producing the libcall
  // otherwise has unsigned semantics. In particular, the shift-count argument
  // of __ashlti3 and __lshrti3 is signed int.
  if (Ty->isIntegerTy(32))
    return true;
  return IsSigned;
}

static bool isFPTRANSAConstantInPrimaryRange(SDValue Op) {
  auto *Constant = dyn_cast<ConstantFPSDNode>(Op);
  if (!Constant || !Constant->getValueAPF().isFinite())
    return false;
  constexpr double PiOverFour = 0.78539816339744830962;
  return std::abs(Constant->getValueAPF().convertToDouble()) <= PiOverFour;
}

static int getFMOVCRConstantID(const APFloat &Value) {
  uint64_t Bits = Value.bitcastToAPInt().getZExtValue();
  switch (Bits) {
  case 0x0000000000000000ULL:
    return 0x0000;
  case 0x8000000000000000ULL:
    return 0x0001;
  case 0x3ff0000000000000ULL:
    return 0x0002;
  case 0xbff0000000000000ULL:
    return 0x0003;
  case 0x3fe0000000000000ULL:
    return 0x0004;
  case 0xbfe0000000000000ULL:
    return 0x0005;
  case 0x4000000000000000ULL:
    return 0x0006;
  case 0xc000000000000000ULL:
    return 0x0007;
  case 0x4024000000000000ULL:
    return 0x0008;
  case 0xc024000000000000ULL:
    return 0x0009;
  case 0x400921fb54442d18ULL:
    return 0x0010;
  case 0x3ff921fb54442d18ULL:
    return 0x0011;
  case 0x3fe921fb54442d18ULL:
    return 0x0012;
  case 0x401921fb54442d18ULL:
    return 0x0013;
  case 0x3fd45f306dc9c883ULL:
    return 0x0014;
  case 0x3fe45f306dc9c883ULL:
    return 0x0015;
  case 0x3ff6a09e667f3bcdULL:
    return 0x0016;
  case 0x3fe6a09e667f3bccULL:
    return 0x0017;
  case 0x4005bf0a8b145769ULL:
    return 0x0020;
  case 0x3ff71547652b82feULL:
    return 0x0021;
  case 0x3fdbcb7b1526e50eULL:
    return 0x0022;
  case 0x3fe62e42fefa39efULL:
    return 0x0023;
  case 0x40026bb1bbb55516ULL:
    return 0x0024;
  case 0x400a934f0979a371ULL:
    return 0x0025;
  case 0x3fd34413509f79ffULL:
    return 0x0026;
  case 0x7ff0000000000000ULL:
    return 0x0100;
  case 0xfff0000000000000ULL:
    return 0x0101;
  case 0x7ff8000000000000ULL:
    return 0x0102;
  case 0xfff8000000000000ULL:
    return 0x0103;
  case 0x7ff0000000000001ULL:
    return 0x0104;
  case 0xfff0000000000001ULL:
    return 0x0105;
  case 0x7fefffffffffffffULL:
    return 0x0110;
  case 0xffefffffffffffffULL:
    return 0x0111;
  case 0x0010000000000000ULL:
    return 0x0112;
  case 0x8010000000000000ULL:
    return 0x0113;
  case 0x0000000000000001ULL:
    return 0x0114;
  case 0x8000000000000001ULL:
    return 0x0115;
  case 0x3cb0000000000000ULL:
    return 0x0116;
  case 0x000fffffffffffffULL:
    return 0x0117;
  case 0x800fffffffffffffULL:
    return 0x0118;
  default:
    return -1;
  }
}

SDValue BedrockTargetLowering::PerformDAGCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  switch (N->getOpcode()) {
  case ISD::ADD: {
    if (N->getValueType(0) != MVT::i64)
      return {};

    auto matchMixedHighMultiply = [&](SDValue High, SDValue Correction,
                                      SDValue &Signed,
                                      SDValue &Unsigned) -> bool {
      if (High.getOpcode() != ISD::MULHU || Correction.getOpcode() != ISD::MUL)
        return false;

      auto matchSignMask = [](SDValue Mask, SDValue Value) {
        if (Mask.getOpcode() != ISD::SRA || Mask.getOperand(0) != Value)
          return false;
        auto *Shift = dyn_cast<ConstantSDNode>(Mask.getOperand(1));
        return Shift && Shift->getZExtValue() == 63;
      };

      SDValue HighLHS = High.getOperand(0);
      SDValue HighRHS = High.getOperand(1);
      SDValue CorrLHS = Correction.getOperand(0);
      SDValue CorrRHS = Correction.getOperand(1);
      if (matchSignMask(CorrLHS, HighLHS) && CorrRHS == HighRHS) {
        Signed = HighLHS;
        Unsigned = HighRHS;
        return true;
      }
      if (matchSignMask(CorrRHS, HighLHS) && CorrLHS == HighRHS) {
        Signed = HighLHS;
        Unsigned = HighRHS;
        return true;
      }
      if (matchSignMask(CorrLHS, HighRHS) && CorrRHS == HighLHS) {
        Signed = HighRHS;
        Unsigned = HighLHS;
        return true;
      }
      if (matchSignMask(CorrRHS, HighRHS) && CorrLHS == HighLHS) {
        Signed = HighRHS;
        Unsigned = HighLHS;
        return true;
      }
      return false;
    };

    SDValue Signed;
    SDValue Unsigned;
    if (matchMixedHighMultiply(N->getOperand(0), N->getOperand(1), Signed,
                               Unsigned) ||
        matchMixedHighMultiply(N->getOperand(1), N->getOperand(0), Signed,
                               Unsigned))
      return DAG.getNode(BedrockISD::MULHSU, DL, MVT::i64, Signed, Unsigned);
    return {};
  }
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::SDIV:
  case ISD::SREM: {
    unsigned Opcode = N->getOpcode();
    bool IsSigned = Opcode == ISD::SDIV || Opcode == ISD::SREM;
    bool IsDivision = Opcode == ISD::UDIV || Opcode == ISD::SDIV;
    unsigned OtherOpcode = IsSigned ? (IsDivision ? ISD::SREM : ISD::SDIV)
                                    : (IsDivision ? ISD::UREM : ISD::UDIV);
    unsigned DivRemOpcode = IsSigned ? ISD::SDIVREM : ISD::UDIVREM;
    SDValue Dividend = N->getOperand(0);
    SDValue Divisor = N->getOperand(1);
    for (SDNode *User : Dividend->users()) {
      if (User == N || User->getOpcode() != OtherOpcode || User->use_empty() ||
          User->getOperand(0) != Dividend || User->getOperand(1) != Divisor)
        continue;
      EVT VT = N->getValueType(0);
      SDValue Pair = DAG.getNode(DivRemOpcode, DL, DAG.getVTList(VT, VT),
                                 Dividend, Divisor);
      DCI.CombineTo(User, IsDivision ? Pair.getValue(1) : Pair);
      return IsDivision ? Pair : Pair.getValue(1);
    }
    return {};
  }
  default:
    break;
  }

  if (N->getOpcode() == ISD::ConstantFP) {
    auto *Constant = cast<ConstantFPSDNode>(N);
    EVT VT = N->getValueType(0);
    if (Constant->getValueAPF().isZero()) {
      for (SDUse &Use : N->uses())
        if (Use.getUser()->getOpcode() == ISD::SETCC ||
            Use.getUser()->getOpcode() == ISD::BR_CC ||
            Use.getUser()->getOpcode() == ISD::SELECT_CC)
          return {};
      if (Constant->getValueAPF().isPosZero()) {
        if (VT == MVT::f32)
          return DAG.getNode(BedrockISD::FCLR_S, DL, VT);
        if (VT == MVT::f64)
          return DAG.getNode(BedrockISD::FCLR_D, DL, VT);
      }
    }
    if (VT == MVT::f64) {
      int ConstantID = getFMOVCRConstantID(Constant->getValueAPF());
      if (ConstantID >= 0)
        return DAG.getNode(BedrockISD::FMOVCR_D, DL, VT,
                           DAG.getTargetConstant(ConstantID, DL, MVT::i32));
    }
    return {};
  }

  if (!Subtarget.hasFPTRANSA() || !N->getFlags().hasApproximateFuncs())
    return {};

  SDValue Arg = N->getOperand(0);
  EVT VT = N->getValueType(0);
  if (VT != MVT::f32 && VT != MVT::f64)
    return {};

  unsigned Opcode;
  switch (N->getOpcode()) {
  case ISD::FACOS:
    Opcode = BedrockISD::FACOSA;
    break;
  case ISD::FASIN:
    Opcode = BedrockISD::FASINA;
    break;
  case ISD::FATAN:
    Opcode = BedrockISD::FATANA;
    break;
  case ISD::FCOS:
    if (!isFPTRANSAConstantInPrimaryRange(Arg))
      return {};
    Opcode = BedrockISD::FCOSA;
    break;
  case ISD::FCOSH:
    Opcode = BedrockISD::FCOSHA;
    break;
  case ISD::FEXP:
    Opcode = BedrockISD::FETOXA;
    break;
  case ISD::FEXP2:
    Opcode = BedrockISD::FTWOTOXA;
    break;
  case ISD::FEXP10:
    Opcode = BedrockISD::FTENTOXA;
    break;
  case ISD::FLOG:
    Opcode = BedrockISD::FLOGNA;
    break;
  case ISD::FLOG2:
    Opcode = BedrockISD::FLOG2A;
    break;
  case ISD::FLOG10:
    Opcode = BedrockISD::FLOG10A;
    break;
  case ISD::FSIN:
    if (!isFPTRANSAConstantInPrimaryRange(Arg))
      return {};
    Opcode = BedrockISD::FSINA;
    break;
  case ISD::FSINH:
    Opcode = BedrockISD::FSINHA;
    break;
  case ISD::FTAN:
    if (!isFPTRANSAConstantInPrimaryRange(Arg))
      return {};
    Opcode = BedrockISD::FTANA;
    break;
  case ISD::FTANH:
    Opcode = BedrockISD::FTANHA;
    break;
  case ISD::FSINCOS: {
    if (!isFPTRANSAConstantInPrimaryRange(Arg))
      return {};
    SDValue Result = DAG.getNode(BedrockISD::FSINCOSA, DL,
                                 DAG.getVTList(VT, VT), Arg);
    return DCI.CombineTo(N, Result, Result.getValue(1));
  }
  default:
    return {};
  }

  return DAG.getNode(Opcode, DL, VT, Arg);
}

bool BedrockTargetLowering::shouldInsertFencesForAtomic(
    const Instruction *I) const {
  // FETCH* and CMPXCHG carry the requested memory order in the instruction.
  // Plain loads and stores use AFENCE sequences around an otherwise relaxed
  // memory operation.
  return isa<LoadInst, StoreInst>(I);
}

Instruction *BedrockTargetLowering::emitLeadingFence(
    IRBuilderBase &Builder, Instruction *Inst, AtomicOrdering Ord) const {
  if ((isa<LoadInst>(Inst) && Ord == AtomicOrdering::SequentiallyConsistent) ||
      (isa<StoreInst>(Inst) && isReleaseOrStronger(Ord)))
    return Builder.CreateFence(AtomicOrdering::SequentiallyConsistent);
  return nullptr;
}

Instruction *BedrockTargetLowering::emitTrailingFence(
    IRBuilderBase &Builder, Instruction *Inst, AtomicOrdering Ord) const {
  if ((isa<LoadInst>(Inst) && isAcquireOrStronger(Ord)) ||
      (isa<StoreInst>(Inst) && Ord == AtomicOrdering::SequentiallyConsistent))
    return Builder.CreateFence(AtomicOrdering::SequentiallyConsistent);
  return nullptr;
}

TargetLowering::AtomicExpansionKind
BedrockTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  switch (RMW->getOperation()) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::And:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
    return AtomicExpansionKind::None;
  default:
    // Bedrock has no native exchange/min/max/nand operation. AtomicExpand
    // builds the required retry loop from the native CMPXCHG instruction.
    return AtomicExpansionKind::CmpXChg;
  }
}

const char *BedrockTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case BedrockISD::RET_FLAG:
    return "BedrockISD::RET_FLAG";
  case BedrockISD::CALL:
    return "BedrockISD::CALL";
  case BedrockISD::CALL_ADDRESS:
    return "BedrockISD::CALL_ADDRESS";
  case BedrockISD::TLS_ADDRESS:
    return "BedrockISD::TLS_ADDRESS";
  case BedrockISD::TLSDESC_CALL:
    return "BedrockISD::TLSDESC_CALL";
  case BedrockISD::TAIL_CALL_CANDIDATE:
    return "BedrockISD::TAIL_CALL_CANDIDATE";
  case BedrockISD::CMP:
    return "BedrockISD::CMP";
  case BedrockISD::FCMP:
    return "BedrockISD::FCMP";
  case BedrockISD::STRICT_FCMP:
    return "BedrockISD::STRICT_FCMP";
  case BedrockISD::FTEST:
    return "BedrockISD::FTEST";
  case BedrockISD::STRICT_FTEST:
    return "BedrockISD::STRICT_FTEST";
  case BedrockISD::TEST:
    return "BedrockISD::TEST";
  case BedrockISD::BTEST:
    return "BedrockISD::BTEST";
  case BedrockISD::BR_CC:
    return "BedrockISD::BR_CC";
  case BedrockISD::SET_CC:
    return "BedrockISD::SET_CC";
  case BedrockISD::SELECT_CC:
    return "BedrockISD::SELECT_CC";
  case BedrockISD::FP_SELECT_CC:
    return "BedrockISD::FP_SELECT_CC";
  case BedrockISD::FP_SELECT_TEST:
    return "BedrockISD::FP_SELECT_TEST";
  case BedrockISD::FP_SET_CC:
    return "BedrockISD::FP_SET_CC";
  case BedrockISD::STRICT_FP_SET_CC:
    return "BedrockISD::STRICT_FP_SET_CC";
  case BedrockISD::SMAX:
    return "BedrockISD::SMAX";
  case BedrockISD::SMIN:
    return "BedrockISD::SMIN";
  case BedrockISD::UMAX:
    return "BedrockISD::UMAX";
  case BedrockISD::UMIN:
    return "BedrockISD::UMIN";
  case BedrockISD::SMAX_ZERO:
    return "BedrockISD::SMAX_ZERO";
  case BedrockISD::SMIN_ZERO:
    return "BedrockISD::SMIN_ZERO";
  case BedrockISD::FCLR_S:
    return "BedrockISD::FCLR_S";
  case BedrockISD::FCLR_D:
    return "BedrockISD::FCLR_D";
  case BedrockISD::FMOVCR_D:
    return "BedrockISD::FMOVCR_D";
  case BedrockISD::FGETEXP:
    return "BedrockISD::FGETEXP";
  case BedrockISD::FGETMAN:
    return "BedrockISD::FGETMAN";
  case BedrockISD::FSCALE:
    return "BedrockISD::FSCALE";
  case BedrockISD::STRICT_FSCALE:
    return "BedrockISD::STRICT_FSCALE";
  case BedrockISD::FACOSA:
    return "BedrockISD::FACOSA";
  case BedrockISD::FASINA:
    return "BedrockISD::FASINA";
  case BedrockISD::FATANA:
    return "BedrockISD::FATANA";
  case BedrockISD::FATANHA:
    return "BedrockISD::FATANHA";
  case BedrockISD::FCOSA:
    return "BedrockISD::FCOSA";
  case BedrockISD::FCOSHA:
    return "BedrockISD::FCOSHA";
  case BedrockISD::FETOXA:
    return "BedrockISD::FETOXA";
  case BedrockISD::FETOXM1A:
    return "BedrockISD::FETOXM1A";
  case BedrockISD::FLOG10A:
    return "BedrockISD::FLOG10A";
  case BedrockISD::FLOG2A:
    return "BedrockISD::FLOG2A";
  case BedrockISD::FLOGNA:
    return "BedrockISD::FLOGNA";
  case BedrockISD::FLOGNP1A:
    return "BedrockISD::FLOGNP1A";
  case BedrockISD::FSINA:
    return "BedrockISD::FSINA";
  case BedrockISD::FSINHA:
    return "BedrockISD::FSINHA";
  case BedrockISD::FTANA:
    return "BedrockISD::FTANA";
  case BedrockISD::FTANHA:
    return "BedrockISD::FTANHA";
  case BedrockISD::FTENTOXA:
    return "BedrockISD::FTENTOXA";
  case BedrockISD::FTWOTOXA:
    return "BedrockISD::FTWOTOXA";
  case BedrockISD::FSINCOSA:
    return "BedrockISD::FSINCOSA";
  case BedrockISD::INCF:
    return "BedrockISD::INCF";
  case BedrockISD::DECF:
    return "BedrockISD::DECF";
  case BedrockISD::EXTRACT:
    return "BedrockISD::EXTRACT";
  case BedrockISD::MULHSU:
    return "BedrockISD::MULHSU";
  default:
    return nullptr;
  }
}

EVT BedrockTargetLowering::getSetCCResultType(const DataLayout &DL,
                                              LLVMContext &Context,
                                              EVT VT) const {
  return MVT::i64;
}

MVT BedrockTargetLowering::getScalarShiftAmountTy(const DataLayout &DL,
                                                  EVT VT) const {
  return MVT::i64;
}

SDValue
BedrockTargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                     SelectionDAG &DAG,
                                     SmallVectorImpl<SDNode *> &) const {
  EVT VT = N->getValueType(0);
  const Function &F = DAG.getMachineFunction().getFunction();
  if ((VT != MVT::i32 && VT != MVT::i64) ||
      (!F.hasMinSize() && !F.hasOptSize()))
    return SDValue();

  unsigned ImmBytes = Divisor.isSignedIntN(8)    ? 1
                      : Divisor.isSignedIntN(16) ? 2
                      : Divisor.isSignedIntN(32) ? 4
                                                 : 8;
  constexpr unsigned LongDivBytes = 4;
  constexpr unsigned WorstCaseCopyBytes = 2;
  constexpr unsigned ExpandedSDIVPow2Bytes = 16;
  if (LongDivBytes + ImmBytes + WorstCaseCopyBytes >= ExpandedSDIVPow2Bytes)
    return SDValue();

  // At -Oz/-Os the immediate DIVS is smaller even when two-address lowering
  // needs a copy. Non-size builds keep the shift expansion for lower latency.
  return SDValue(N, 0);
}

unsigned BedrockTargetLowering::getJumpTableEncoding() const {
  return MachineJumpTableInfo::EK_LabelDifference32;
}

static unsigned getBedrockCondCode(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETEQ:
    return 0x2;
  case ISD::SETNE:
    return 0x3;
  case ISD::SETULT:
    return 0x4;
  case ISD::SETUGE:
    return 0x5;
  case ISD::SETULE:
    return 0xa;
  case ISD::SETUGT:
    return 0xb;
  case ISD::SETLT:
    return 0xc;
  case ISD::SETGE:
    return 0xd;
  case ISD::SETLE:
    return 0xe;
  case ISD::SETGT:
    return 0xf;
  default:
    report_fatal_error("unsupported Bedrock integer condition code");
  }
}

static constexpr unsigned BedrockFPOneCond = 0x10;
static constexpr unsigned BedrockFPEqualOrUnorderedCond = 0x11;

static unsigned getBedrockFPCondCode(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETFALSE:
  case ISD::SETFALSE2:
    return 0x1;
  case ISD::SETOEQ:
    return 0x2;
  case ISD::SETOGT:
    return 0xf;
  case ISD::SETOGE:
    return 0xd;
  case ISD::SETOLT:
    return 0x4;
  case ISD::SETOLE:
    return 0xa;
  case ISD::SETONE:
    return BedrockFPOneCond;
  case ISD::SETO:
    return 0x9;
  case ISD::SETUO:
    return 0x8;
  case ISD::SETUEQ:
    return BedrockFPEqualOrUnorderedCond;
  case ISD::SETUGT:
    return 0xb;
  case ISD::SETUGE:
    return 0x5;
  case ISD::SETULT:
    return 0xc;
  case ISD::SETULE:
    return 0xe;
  case ISD::SETUNE:
    return 0x3;
  case ISD::SETTRUE:
  case ISD::SETTRUE2:
    return 0x0;
  default:
    report_fatal_error("unsupported Bedrock floating-point condition code");
  }
}

static ISD::CondCode getCondCodeOperand(SDValue Op, StringRef Context) {
  if (auto *CC = dyn_cast<CondCodeSDNode>(Op))
    return CC->get();
  report_fatal_error(Twine("Bedrock expected condition code in ") + Context);
}

static EVT getVTSDNodeOperand(SDValue Op, StringRef Context) {
  if (auto *VT = dyn_cast<VTSDNode>(Op))
    return VT->getVT();
  report_fatal_error(Twine("Bedrock expected value type in ") + Context);
}

SDValue BedrockTargetLowering::LowerOperation(SDValue Op,
                                              SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::BRCOND:
    return LowerBRCOND(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
    return LowerSETCC(Op, DAG);
  case ISD::SELECT:
    return LowerSELECT(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::SMAX:
  case ISD::SMIN:
  case ISD::UMAX:
  case ISD::UMIN:
    return LowerMinMax(Op, DAG);
  case ISD::SIGN_EXTEND_INREG:
    return LowerSIGN_EXTEND_INREG(Op, DAG);
  case ISD::IS_FPCLASS:
    return LowerIS_FPCLASS(Op, DAG);
  case ISD::FSHR:
    return LowerFSHR(Op, DAG);
  case ISD::FSHL:
    return LowerFSHL(Op, DAG);
  case ISD::FLDEXP:
  case ISD::STRICT_FLDEXP:
    return LowerFLDEXP(Op, DAG);
  case ISD::FFREXP:
    return LowerFFREXP(Op, DAG);
  case ISD::SADDO:
  case ISD::SSUBO:
  case ISD::UADDO:
  case ISD::USUBO:
    return LowerOverflow(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::ATOMIC_FENCE:
    // A C atomic_signal_fence synchronizes only with signal handlers in the
    // current thread. Preserve compiler ordering without issuing a hardware
    // AFENCE, which is reserved for cross-thread fences.
    if (static_cast<SyncScope::ID>(Op.getConstantOperandVal(2)) ==
        SyncScope::SingleThread)
      return DAG.getNode(ISD::MEMBARRIER, SDLoc(Op), MVT::Other,
                         Op.getOperand(0));
    return Op;
  default:
    llvm_unreachable("unhandled Bedrock lowering operation");
  }
}

SDValue BedrockTargetLowering::LowerOverflow(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op->getValueType(0);
  bool IsAdd = Op.getOpcode() == ISD::SADDO || Op.getOpcode() == ISD::UADDO;
  bool IsSigned =
      Op.getOpcode() == ISD::SADDO || Op.getOpcode() == ISD::SSUBO;
  unsigned Opcode = IsAdd ? ISD::ADDC : ISD::SUBC;
  SDValue Arithmetic;
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  auto *Constant = dyn_cast<ConstantSDNode>(RHS);
  if (IsAdd && !Constant) {
    Constant = dyn_cast<ConstantSDNode>(LHS);
    if (Constant)
      std::swap(LHS, RHS);
  }

  unsigned UnaryOpcode = 0;
  if (Constant && Constant->isOne() &&
      (IsSigned || Op.getOpcode() == ISD::UADDO))
    UnaryOpcode = BedrockISD::INCF;
  else if (Constant && Constant->isOne() && Op.getOpcode() == ISD::USUBO)
    UnaryOpcode = BedrockISD::DECF;
  else if (Constant && Constant->isAllOnes() && IsSigned && IsAdd)
    UnaryOpcode = BedrockISD::DECF;

  if (UnaryOpcode)
    Arithmetic =
        DAG.getNode(UnaryOpcode, DL, DAG.getVTList(VT, MVT::Glue), LHS);
  else
    Arithmetic = DAG.getNode(Opcode, DL, DAG.getVTList(VT, MVT::Glue), LHS,
                             RHS);
  SDValue Overflow = DAG.getNode(BedrockISD::SET_CC, DL, MVT::i64,
                                 DAG.getConstant(IsSigned ? /*VS=*/0x8
                                                          : /*ULT/CS=*/0x4,
                                                 DL, MVT::i32),
                                 Arithmetic.getValue(1));
  assert(Op->getValueType(1) == MVT::i64 &&
         "expected promoted Bedrock overflow result");
  return DAG.getMergeValues({Arithmetic, Overflow}, DL);
}

SDValue BedrockTargetLowering::LowerFSHR(SDValue Op, SelectionDAG &DAG) const {
  auto *Amount = dyn_cast<ConstantSDNode>(Op.getOperand(2));
  if (!Amount)
    return expandFunnelShift(Op.getNode(), DAG);

  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  uint64_t Offset = Amount->getZExtValue() % VT.getSizeInBits();
  return DAG.getNode(BedrockISD::EXTRACT, DL, VT, Op.getOperand(0),
                     Op.getOperand(1),
                     DAG.getTargetConstant(Offset, DL, MVT::i64));
}

SDValue BedrockTargetLowering::LowerFSHL(SDValue Op, SelectionDAG &DAG) const {
  auto *Amount = dyn_cast<ConstantSDNode>(Op.getOperand(2));
  if (!Amount)
    return expandFunnelShift(Op.getNode(), DAG);

  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  uint64_t Width = VT.getSizeInBits();
  uint64_t Shift = Amount->getZExtValue() % Width;
  uint64_t Offset = Shift == 0 ? Width : Width - Shift;
  return DAG.getNode(BedrockISD::EXTRACT, DL, VT, Op.getOperand(0),
                     Op.getOperand(1),
                     DAG.getTargetConstant(Offset, DL, MVT::i64));
}

SDValue BedrockTargetLowering::LowerFLDEXP(SDValue Op,
                                           SelectionDAG &DAG) const {
  bool IsStrict = Op.getOpcode() == ISD::STRICT_FLDEXP;
  SDLoc DL(Op);
  SDValue Value = Op.getOperand(IsStrict ? 1 : 0);
  SDValue Exponent = Op.getOperand(IsStrict ? 2 : 1);
  EVT VT = Value.getValueType();
  EVT ExponentVT = Exponent.getValueType();

  // FSCALE consumes a floating-point exponent and rounds it according to the
  // current FSTATUS mode. Clamp exponents that already guarantee overflow or
  // underflow so the integer-to-FP conversion is exact and cannot introduce
  // an intermediate NX exception.
  int64_t Bound = VT == MVT::f32 ? 512 : 4096;
  SDValue Min = DAG.getSignedConstant(-Bound, DL, ExponentVT);
  SDValue Max = DAG.getSignedConstant(Bound, DL, ExponentVT);
  SDValue Clamped = DAG.getNode(ISD::SMAX, DL, ExponentVT, Exponent, Min);
  Clamped = DAG.getNode(ISD::SMIN, DL, ExponentVT, Clamped, Max);

  if (!IsStrict) {
    SDValue Scale = DAG.getNode(ISD::SINT_TO_FP, DL, VT, Clamped);
    return DAG.getNode(BedrockISD::FSCALE, DL, VT, Value, Scale);
  }

  SDVTList VTs = DAG.getVTList(VT, MVT::Other);
  SDValue Scale = DAG.getNode(ISD::STRICT_SINT_TO_FP, DL, VTs,
                              Op.getOperand(0), Clamped);
  return DAG.getNode(BedrockISD::STRICT_FSCALE, DL, VTs, Scale.getValue(1),
                     Value, Scale);
}

SDValue BedrockTargetLowering::LowerFFREXP(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Value = Op.getOperand(0);
  EVT VT = Value.getValueType();
  EVT ResultExpVT = Op->getValueType(1);

  Intrinsic::ID ClassIID = VT == MVT::f32 ? Intrinsic::bedrock_fclass_f32
                                          : Intrinsic::bedrock_fclass_f64;
  SDValue Class = DAG.getNode(
      ISD::INTRINSIC_WO_CHAIN, DL, MVT::i64,
      DAG.getTargetConstant(ClassIID, DL, MVT::i32), Value);
  SDValue Zero = DAG.getConstant(0, DL, MVT::i64);
  auto TestClassMask = [&](uint64_t Mask) {
    SDValue Masked = DAG.getNode(ISD::AND, DL, MVT::i64, Class,
                                 DAG.getConstant(Mask, DL, MVT::i64));
    return DAG.getSetCC(DL, MVT::i64, Masked, Zero, ISD::SETNE);
  };

  // FGETMAN preserves subnormal encodings instead of normalizing them. Scale
  // subnormals into the normal range first, then compensate the exponent.
  // Non-finite values are replaced before the FPU operations so that frexp
  // does not acquire exceptions from speculative FGETEXP/FGETMAN execution.
  SDValue IsSubnormal = TestClassMask((1u << 2) | (1u << 5));
  SDValue IsFiniteNonzero =
      TestClassMask((1u << 1) | (1u << 2) | (1u << 5) | (1u << 6));
  unsigned FClrOpcode =
      VT == MVT::f32 ? BedrockISD::FCLR_S : BedrockISD::FCLR_D;
  SDValue SafeZero = DAG.getNode(FClrOpcode, DL, VT);
  SDValue SafeValue = DAG.getNode(ISD::SELECT, DL, VT, IsFiniteNonzero, Value,
                                  SafeZero);

  int64_t SubnormalScale = VT == MVT::f32 ? 24 : 54;
  SDValue Scale = DAG.getNode(
      ISD::SELECT, DL, MVT::i64, IsSubnormal,
      DAG.getConstant(SubnormalScale, DL, MVT::i64), Zero);
  SDValue Normalized =
      DAG.getNode(ISD::FLDEXP, DL, VT, SafeValue, Scale);

  SDValue Mantissa =
      DAG.getNode(BedrockISD::FGETMAN, DL, VT, Normalized);
  Mantissa = DAG.getNode(ISD::FMUL, DL, VT, Mantissa,
                         DAG.getConstantFP(0.5, DL, VT));

  SDValue ExponentFP =
      DAG.getNode(BedrockISD::FGETEXP, DL, VT, Normalized);
  SDValue Exponent = DAG.getNode(ISD::FP_TO_SINT, DL, MVT::i64, ExponentFP);
  Exponent = DAG.getNode(ISD::ADD, DL, MVT::i64, Exponent,
                         DAG.getConstant(1, DL, MVT::i64));
  Exponent = DAG.getNode(ISD::SUB, DL, MVT::i64, Exponent, Scale);

  Mantissa = DAG.getNode(ISD::SELECT, DL, VT, IsFiniteNonzero, Mantissa,
                         Value);
  Exponent = DAG.getNode(ISD::SELECT, DL, MVT::i64, IsFiniteNonzero,
                         Exponent, Zero);
  Exponent = DAG.getSExtOrTrunc(Exponent, DL, ResultExpVT);
  return DAG.getMergeValues({Mantissa, Exponent}, DL);
}

static unsigned getBedrockFClassMask(unsigned Test) {
  unsigned Mask = 0;
  if (Test & fcNegInf)
    Mask |= 1u << 0;
  if (Test & fcNegNormal)
    Mask |= 1u << 1;
  if (Test & fcNegSubnormal)
    Mask |= 1u << 2;
  if (Test & fcNegZero)
    Mask |= 1u << 3;
  if (Test & fcPosZero)
    Mask |= 1u << 4;
  if (Test & fcPosSubnormal)
    Mask |= 1u << 5;
  if (Test & fcPosNormal)
    Mask |= 1u << 6;
  if (Test & fcPosInf)
    Mask |= 1u << 7;
  if (Test & fcSNan)
    Mask |= 1u << 8;
  if (Test & fcQNan)
    Mask |= 1u << 9;
  return Mask;
}

SDValue BedrockTargetLowering::LowerIS_FPCLASS(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Value = Op.getOperand(0);
  auto *Test = cast<ConstantSDNode>(Op.getOperand(1));
  Intrinsic::ID IID = Value.getValueType() == MVT::f32
                          ? Intrinsic::bedrock_fclass_f32
                          : Intrinsic::bedrock_fclass_f64;
  SDValue ID = DAG.getConstant(IID, DL, MVT::i32);
  SDValue Class =
      DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, MVT::i64, {ID, Value});
  SDValue Masked =
      DAG.getNode(ISD::AND, DL, MVT::i64, Class,
                  DAG.getConstant(getBedrockFClassMask(Test->getZExtValue()),
                                  DL, MVT::i64));
  SDValue Result = DAG.getSetCC(DL, MVT::i64, Masked,
                                DAG.getConstant(0, DL, MVT::i64), ISD::SETNE);
  if (Op.getValueType() == MVT::i64)
    return Result;
  return DAG.getNode(ISD::TRUNCATE, DL, Op.getValueType(), Result);
}

SDValue
BedrockTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  uint64_t Alignment = Op.getConstantOperandVal(2);
  if (Alignment < 16)
    Alignment = 16;

  SDValue SP = DAG.getCopyFromReg(Chain, DL, Bedrock::SP, MVT::i64);
  Chain = SP.getValue(1);
  SDValue Result = DAG.getNode(ISD::SUB, DL, MVT::i64, SP, Size);
  Result =
      DAG.getNode(ISD::AND, DL, MVT::i64, Result,
                  DAG.getSignedConstant(-int64_t(Alignment), DL, MVT::i64));
  Chain = DAG.getCopyToReg(Chain, DL, Bedrock::SP, Result);
  return DAG.getMergeValues({Result, Chain}, DL);
}

static SDValue emitCompare(SDValue LHS, SDValue RHS, const SDLoc &DL,
                           SelectionDAG &DAG) {
  if (LHS.getValueType().isFloatingPoint()) {
    auto *Constant = dyn_cast<ConstantFPSDNode>(RHS);
    if (Constant && Constant->getValueAPF().isZero())
      return DAG.getNode(BedrockISD::FTEST, DL, MVT::Glue, LHS);
    return DAG.getNode(BedrockISD::FCMP, DL, MVT::Glue, LHS, RHS);
  }
  if (isNullConstant(RHS))
    return DAG.getNode(BedrockISD::TEST, DL, MVT::Glue, LHS);
  return DAG.getNode(BedrockISD::CMP, DL, MVT::Glue, LHS, RHS);
}

static SDValue emitSingleBitCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                                    const SDLoc &DL, SelectionDAG &DAG) {
  if (CC != ISD::SETEQ && CC != ISD::SETNE)
    return {};
  if (isNullConstant(LHS))
    std::swap(LHS, RHS);
  if (!isNullConstant(RHS) || LHS.getOpcode() != ISD::AND)
    return {};

  SDValue Value = LHS.getOperand(0);
  auto *Mask = dyn_cast<ConstantSDNode>(LHS.getOperand(1));
  if (!Mask) {
    Value = LHS.getOperand(1);
    Mask = dyn_cast<ConstantSDNode>(LHS.getOperand(0));
  }
  if (!Mask || !Mask->getAPIntValue().isPowerOf2())
    return {};

  unsigned Bit = Mask->getAPIntValue().countr_zero();
  return DAG.getNode(BedrockISD::BTEST, DL, MVT::Glue, Value,
                     DAG.getTargetConstant(Bit, DL, MVT::i64));
}

static SDValue lowerFPSetCC(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                            const SDLoc &DL, SelectionDAG &DAG) {
  unsigned TargetCC = getBedrockFPCondCode(CC);
  if (TargetCC == 0x0 || TargetCC == 0x1)
    return DAG.getConstant(TargetCC == 0x0, DL, MVT::i64);
  if (TargetCC == BedrockFPOneCond ||
      TargetCC == BedrockFPEqualOrUnorderedCond)
    return DAG.getNode(BedrockISD::FP_SET_CC, DL, MVT::i64, LHS, RHS,
                       DAG.getConstant(TargetCC, DL, MVT::i32));

  SDValue Glue = emitCompare(LHS, RHS, DL, DAG);
  SDValue Target = DAG.getConstant(TargetCC, DL, MVT::i32);
  return DAG.getNode(BedrockISD::SET_CC, DL, MVT::i64, Target, Glue);
}

static SDValue lowerStrictFPSetCC(SDValue Chain, SDValue LHS, SDValue RHS,
                                  ISD::CondCode CC, const SDLoc &DL,
                                  SelectionDAG &DAG) {
  unsigned TargetCC = getBedrockFPCondCode(CC);
  if (TargetCC == BedrockFPOneCond ||
      TargetCC == BedrockFPEqualOrUnorderedCond)
    return DAG.getNode(
        BedrockISD::STRICT_FP_SET_CC, DL,
        DAG.getVTList(MVT::i64, MVT::Other), Chain, LHS, RHS,
        DAG.getConstant(TargetCC, DL, MVT::i32));

  bool TestZero = false;
  if (auto *Constant = dyn_cast<ConstantFPSDNode>(RHS))
    TestZero = Constant->getValueAPF().isZero();
  unsigned CompareOpcode =
      TestZero ? BedrockISD::STRICT_FTEST : BedrockISD::STRICT_FCMP;
  SDValue Compare =
      TestZero
          ? DAG.getNode(CompareOpcode, DL,
                        DAG.getVTList(MVT::Other, MVT::Glue), Chain, LHS)
          : DAG.getNode(CompareOpcode, DL,
                        DAG.getVTList(MVT::Other, MVT::Glue), Chain, LHS, RHS);

  SDValue Result;
  if (TargetCC == 0x0 || TargetCC == 0x1)
    Result = DAG.getConstant(TargetCC == 0x0, DL, MVT::i64);
  else
    Result = DAG.getNode(BedrockISD::SET_CC, DL, MVT::i64,
                         DAG.getConstant(TargetCC, DL, MVT::i32),
                         Compare.getValue(1));
  return DAG.getMergeValues({Result, Compare}, DL);
}

static SDValue lowerStrictFPSignalingSetCC(
    SDValue Chain, SDValue LHS, SDValue RHS, ISD::CondCode CC,
    const SDLoc &DL, SelectionDAG &DAG) {
  EVT VT = LHS.getValueType();
  Intrinsic::ID ClassIID = VT == MVT::f32 ? Intrinsic::bedrock_fclass_f32
                                          : Intrinsic::bedrock_fclass_f64;
  auto Classify = [&](SDValue Value) {
    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, MVT::i64,
                       DAG.getTargetConstant(ClassIID, DL, MVT::i32), Value);
  };
  auto IsNaN = [&](SDValue Value) {
    SDValue Class = Classify(Value);
    SDValue NaNClass = DAG.getNode(
        ISD::AND, DL, MVT::i64, Class,
        DAG.getConstant((1u << 8) | (1u << 9), DL, MVT::i64));
    return DAG.getSetCC(DL, MVT::i64, NaNClass,
                        DAG.getConstant(0, DL, MVT::i64), ISD::SETNE);
  };

  unsigned FClrOpcode =
      VT == MVT::f32 ? BedrockISD::FCLR_S : BedrockISD::FCLR_D;
  SDValue SafeZero = DAG.getNode(FClrOpcode, DL, VT);
  SDValue NaNOrZero =
      DAG.getNode(ISD::SELECT, DL, VT, IsNaN(RHS), RHS, SafeZero);
  NaNOrZero = DAG.getNode(ISD::SELECT, DL, VT, IsNaN(LHS), LHS, NaNOrZero);

  // FCVT raises NV for either quiet or signaling NaNs. Selecting +0 for the
  // all-numeric case avoids conversion-specific NX/NV side effects.
  SDValue Probe = DAG.getNode(ISD::STRICT_FP_TO_SINT, DL,
                              DAG.getVTList(MVT::i64, MVT::Other), Chain,
                              NaNOrZero);
  return lowerStrictFPSetCC(Probe.getValue(1), LHS, RHS, CC, DL, DAG);
}

SDValue BedrockTargetLowering::LowerBRCOND(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue Cond = Op.getOperand(1);
  SDValue Chain = Op.getOperand(0);
  SDValue Dest = Op.getOperand(2);
  SDLoc DL(Op);
  SDValue TargetCC;
  SDValue Glue;
  if (Cond.getOpcode() == BedrockISD::SET_CC && Cond.hasOneUse()) {
    TargetCC = Cond.getOperand(0);
    Glue = Cond.getOperand(1);
  } else {
    TargetCC = DAG.getConstant(getBedrockCondCode(ISD::SETNE), DL, MVT::i32);
    Glue = emitCompare(Cond, DAG.getConstant(0, DL, Cond.getValueType()), DL,
                       DAG);
  }
  return DAG.getNode(BedrockISD::BR_CC, DL, MVT::Other, Chain, Dest, TargetCC,
                     Glue);
}

SDValue BedrockTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = getCondCodeOperand(Op.getOperand(1), "BR_CC");
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  if (LHS.getValueType().isFloatingPoint()) {
    unsigned FPCC = getBedrockFPCondCode(CC);
    if (FPCC == 0x1)
      return Chain;
    if (FPCC == 0x0)
      return DAG.getNode(ISD::BR, DL, Op.getValueType(), Chain, Dest);
    if (FPCC == BedrockFPOneCond ||
        FPCC == BedrockFPEqualOrUnorderedCond) {
      SDValue Cond = lowerFPSetCC(LHS, RHS, CC, DL, DAG);
      SDValue TargetCC =
          DAG.getConstant(getBedrockCondCode(ISD::SETNE), DL, MVT::i32);
      SDValue Glue = emitCompare(Cond, DAG.getConstant(0, DL, MVT::i64), DL,
                                 DAG);
      return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain,
                         Dest, TargetCC, Glue);
    }
    SDValue TargetCC = DAG.getConstant(FPCC, DL, MVT::i32);
    SDValue Glue = emitCompare(LHS, RHS, DL, DAG);
    return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain, Dest,
                       TargetCC, Glue);
  }

  SDValue TargetCC = DAG.getConstant(getBedrockCondCode(CC), DL, MVT::i32);
  SDValue Glue = emitSingleBitCompare(LHS, RHS, CC, DL, DAG);
  if (!Glue)
    Glue = emitCompare(LHS, RHS, DL, DAG);
  return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain, Dest,
                     TargetCC, Glue);
}

SDValue BedrockTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getOpcode() == ISD::STRICT_FSETCC ||
      Op.getOpcode() == ISD::STRICT_FSETCCS) {
    SDValue LHS = Op.getOperand(1);
    SDValue RHS = Op.getOperand(2);
    bool IsSignaling = Op.getOpcode() == ISD::STRICT_FSETCCS;
    ISD::CondCode CC = getCondCodeOperand(
        Op.getOperand(3), IsSignaling ? "STRICT_FSETCCS" : "STRICT_FSETCC");
    if (IsSignaling)
      return lowerStrictFPSignalingSetCC(Op.getOperand(0), LHS, RHS, CC,
                                         SDLoc(Op), DAG);
    return lowerStrictFPSetCC(Op.getOperand(0), LHS, RHS, CC, SDLoc(Op), DAG);
  }

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = getCondCodeOperand(Op.getOperand(2), "SETCC");
  SDLoc DL(Op);

  if (LHS.getValueType().isFloatingPoint())
    return lowerFPSetCC(LHS, RHS, CC, DL, DAG);

  SDValue TargetCC = DAG.getConstant(getBedrockCondCode(CC), DL, MVT::i32);
  SDValue Glue = emitSingleBitCompare(LHS, RHS, CC, DL, DAG);
  if (!Glue)
    Glue = emitCompare(LHS, RHS, DL, DAG);
  return DAG.getNode(BedrockISD::SET_CC, DL, Op.getValueType(), TargetCC, Glue);
}

static SDValue fitIntegerToVT(SDValue Value, EVT VT, const SDLoc &DL,
                              SelectionDAG &DAG) {
  EVT ValueVT = Value.getValueType();
  if (ValueVT == VT)
    return Value;
  if (ValueVT.bitsLT(VT))
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Value);
  return DAG.getNode(ISD::TRUNCATE, DL, VT, Value);
}

static SDValue lowerSelectFromZeroOrOne(SDValue Cond, SDValue TrueValue,
                                        SDValue FalseValue, const SDLoc &DL,
                                        SelectionDAG &DAG) {
  EVT VT = TrueValue.getValueType();
  if (!VT.isInteger())
    report_fatal_error("Bedrock only supports integer select lowering");

  Cond = fitIntegerToVT(Cond, VT, DL, DAG);
  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue Mask = DAG.getNode(ISD::SUB, DL, VT, Zero, Cond);
  SDValue InvertedMask = DAG.getNode(ISD::XOR, DL, VT, Mask,
                                     DAG.getAllOnesConstant(DL, VT));
  SDValue TruePart = DAG.getNode(ISD::AND, DL, VT, TrueValue, Mask);
  SDValue FalsePart = DAG.getNode(ISD::AND, DL, VT, FalseValue, InvertedMask);
  return DAG.getNode(ISD::OR, DL, VT, TruePart, FalsePart);
}

static bool isNonConstantRegisterValue(SDValue Value) {
  return !isa<ConstantSDNode>(Value) && !isa<ConstantFPSDNode>(Value);
}

static unsigned getBedrockMinMaxOpcode(unsigned Opcode) {
  switch (Opcode) {
  case ISD::SMAX:
    return BedrockISD::SMAX;
  case ISD::SMIN:
    return BedrockISD::SMIN;
  case ISD::UMAX:
    return BedrockISD::UMAX;
  case ISD::UMIN:
    return BedrockISD::UMIN;
  default:
    llvm_unreachable("unexpected Bedrock min/max opcode");
  }
}

static SDValue lowerSelectCCToMinMax(SDValue LHS, SDValue RHS,
                                     SDValue TrueValue, SDValue FalseValue,
                                     ISD::CondCode CC, const SDLoc &DL,
                                     SelectionDAG &DAG) {
  EVT VT = TrueValue.getValueType();
  if (!VT.isInteger() || FalseValue.getValueType() != VT ||
      LHS.getValueType() != VT ||
      RHS.getValueType() != VT)
    return SDValue();

  auto GetDirectOpcode = [&]() -> unsigned {
    switch (CC) {
    case ISD::SETGT:
    case ISD::SETGE:
      return BedrockISD::SMAX;
    case ISD::SETLT:
    case ISD::SETLE:
      return BedrockISD::SMIN;
    case ISD::SETUGT:
    case ISD::SETUGE:
      return BedrockISD::UMAX;
    case ISD::SETULT:
    case ISD::SETULE:
      return BedrockISD::UMIN;
    default:
      return 0;
    }
  };

  auto GetSwappedOpcode = [&]() -> unsigned {
    switch (CC) {
    case ISD::SETGT:
    case ISD::SETGE:
      return BedrockISD::SMIN;
    case ISD::SETLT:
    case ISD::SETLE:
      return BedrockISD::SMAX;
    case ISD::SETUGT:
    case ISD::SETUGE:
      return BedrockISD::UMIN;
    case ISD::SETULT:
    case ISD::SETULE:
      return BedrockISD::UMAX;
    default:
      return 0;
    }
  };

  if (TrueValue == LHS && FalseValue == RHS) {
    if (unsigned Opc = GetDirectOpcode())
      return DAG.getNode(Opc, DL, VT, LHS, RHS);
  }
  if (TrueValue == RHS && FalseValue == LHS) {
    if (unsigned Opc = GetSwappedOpcode())
      return DAG.getNode(Opc, DL, VT, LHS, RHS);
  }

  return SDValue();
}

SDValue BedrockTargetLowering::LowerMinMax(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDLoc DL(Op);

  if (Op.getOpcode() == ISD::SMAX || Op.getOpcode() == ISD::SMIN) {
    unsigned ZeroOpc = Op.getOpcode() == ISD::SMAX ? BedrockISD::SMAX_ZERO
                                                   : BedrockISD::SMIN_ZERO;
    if (isNullConstant(RHS))
      return DAG.getNode(ZeroOpc, DL, Op.getValueType(), LHS);
    if (isNullConstant(LHS))
      return DAG.getNode(ZeroOpc, DL, Op.getValueType(), RHS);
  }

  return DAG.getNode(getBedrockMinMaxOpcode(Op.getOpcode()), DL,
                     Op.getValueType(), LHS, RHS);
}

static SDValue lowerRegisterSelectCC(SDValue LHS, SDValue RHS,
                                     SDValue TrueValue, SDValue FalseValue,
                                     ISD::CondCode CC, const SDLoc &DL,
                                     SelectionDAG &DAG) {
  EVT CmpVT = LHS.getValueType();
  EVT VT = TrueValue.getValueType();
  bool IntegerCompare = CmpVT == MVT::i32 || CmpVT == MVT::i64;
  bool IntegerResult = VT == MVT::i32 || VT == MVT::i64;
  bool FPCompare = CmpVT == MVT::f32 || CmpVT == MVT::f64;
  bool FPResult = VT == MVT::f32 || VT == MVT::f64;
  if ((!IntegerCompare && !FPCompare) || (!IntegerResult && !FPResult))
    return SDValue();

  if (FPCompare && FPResult) {
    unsigned TargetCC = getBedrockFPCondCode(CC);
    if (TargetCC == 0x0)
      return TrueValue;
    if (TargetCC == 0x1)
      return FalseValue;

    auto *Constant = dyn_cast<ConstantFPSDNode>(RHS);
    bool TestZero = Constant && Constant->getValueAPF().isZero();
    auto MakeSelect = [&](SDValue TVal, SDValue FVal,
                          unsigned Cond) -> SDValue {
      if (TestZero)
        return DAG.getNode(BedrockISD::FP_SELECT_TEST, DL, VT, LHS, TVal,
                           FVal, DAG.getConstant(Cond, DL, MVT::i32));
      return DAG.getNode(BedrockISD::FP_SELECT_CC, DL, VT, LHS, RHS, TVal,
                         FVal, DAG.getConstant(Cond, DL, MVT::i32));
    };
    if (TargetCC == BedrockFPOneCond)
      return MakeSelect(FalseValue, TrueValue,
                        BedrockFPEqualOrUnorderedCond);
    return MakeSelect(TrueValue, FalseValue, TargetCC);
  }

  if (FPCompare || !IntegerCompare)
    return SDValue();
  if (IntegerResult &&
      (!isNonConstantRegisterValue(TrueValue) ||
       !isNonConstantRegisterValue(FalseValue)))
    return SDValue();

  SDValue TargetCC = DAG.getConstant(getBedrockCondCode(CC), DL, MVT::i32);
  return DAG.getNode(BedrockISD::SELECT_CC, DL, VT, LHS, RHS, TrueValue,
                     FalseValue, TargetCC);
}

SDValue BedrockTargetLowering::LowerSELECT(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Cond = Op.getOperand(0);
  SDValue TrueValue = Op.getOperand(1);
  SDValue FalseValue = Op.getOperand(2);

  if (Cond.getOpcode() == ISD::SETCC) {
    if (SDValue MinMax = lowerSelectCCToMinMax(
            Cond.getOperand(0), Cond.getOperand(1), TrueValue, FalseValue,
            getCondCodeOperand(Cond.getOperand(2), "SELECT"), DL, DAG))
      return MinMax;
    if (SDValue Select = lowerRegisterSelectCC(
            Cond.getOperand(0), Cond.getOperand(1), TrueValue, FalseValue,
            getCondCodeOperand(Cond.getOperand(2), "SELECT"), DL, DAG))
      return Select;
  }

  if (TrueValue.getValueType().isFloatingPoint()) {
    Cond = fitIntegerToVT(Cond, MVT::i64, DL, DAG);
    SDValue Zero = DAG.getConstant(0, DL, MVT::i64);
    SDValue TargetCC =
        DAG.getConstant(getBedrockCondCode(ISD::SETNE), DL, MVT::i32);
    return DAG.getNode(BedrockISD::SELECT_CC, DL, TrueValue.getValueType(),
                       Cond, Zero, TrueValue, FalseValue, TargetCC);
  }

  return lowerSelectFromZeroOrOne(Op.getOperand(0), Op.getOperand(1),
                                  Op.getOperand(2), DL, DAG);
}

SDValue BedrockTargetLowering::LowerSELECT_CC(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueValue = Op.getOperand(2);
  SDValue FalseValue = Op.getOperand(3);
  SDValue CC = Op.getOperand(4);
  SDLoc DL(Op);

  if (SDValue MinMax = lowerSelectCCToMinMax(
          LHS, RHS, TrueValue, FalseValue, getCondCodeOperand(CC, "SELECT_CC"),
          DL, DAG))
    return MinMax;

  if (SDValue Select =
          lowerRegisterSelectCC(LHS, RHS, TrueValue, FalseValue,
                                getCondCodeOperand(CC, "SELECT_CC"), DL, DAG))
    return Select;

  SDValue Cond =
      DAG.getSetCC(DL, MVT::i64, LHS, RHS, getCondCodeOperand(CC, "SELECT_CC"));
  return lowerSelectFromZeroOrOne(Cond, TrueValue, FalseValue, DL, DAG);
}

static Register getSegmentRegister(unsigned Selector) {
  switch (Selector) {
  case 0:
    return Bedrock::DS;
  case 1:
    return Bedrock::SS;
  case 2:
    return Bedrock::GS0;
  case 3:
    return Bedrock::GS1;
  case 4:
    return Bedrock::GS2;
  case 5:
    return Bedrock::GS3;
  case 6:
    return Bedrock::GS4;
  case 7:
    return Bedrock::GS5;
  default:
    llvm_unreachable("invalid Bedrock segment-register selector");
  }
}

void BedrockTargetLowering::AdjustInstrPostInstrSelection(
    MachineInstr &MI, SDNode *) const {
  bool IsDef;
  switch (MI.getOpcode()) {
  case Bedrock::BEDROCK_RDSEG:
    IsDef = false;
    break;
  case Bedrock::BEDROCK_WRSEG:
    IsDef = true;
    break;
  default:
    llvm_unreachable("unexpected Bedrock post-isel instruction");
  }

  assert(MI.getOperand(1).isImm() && "segment selector must be immediate");
  Register Segment = getSegmentRegister(MI.getOperand(1).getImm());
  MI.addOperand(MachineOperand::CreateReg(Segment, IsDef, /*IsImp=*/true));
}

MachineBasicBlock *BedrockTargetLowering::EmitInstrWithCustomInserter(
    MachineInstr &MI, MachineBasicBlock *MBB) const {
  if (MI.getOpcode() == Bedrock::FP_SET_CC_S ||
      MI.getOpcode() == Bedrock::FP_SET_CC_D) {
    const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
    MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
    DebugLoc DL = MI.getDebugLoc();
    Register Dst = MI.getOperand(0).getReg();
    Register LHS = MI.getOperand(1).getReg();
    Register RHS = MI.getOperand(2).getReg();
    int64_t CC = MI.getOperand(3).getImm();
    Register LHSResult = MRI.createVirtualRegister(&Bedrock::GPR64RegClass);
    Register RHSResult = MRI.createVirtualRegister(&Bedrock::GPR64RegClass);

    BuildMI(*MBB, MI, DL,
            TII.get(MI.getOpcode() == Bedrock::FP_SET_CC_D
                        ? Bedrock::FCMPDrr
                        : Bedrock::FCMPSrr))
        .addReg(LHS)
        .addReg(RHS);
    if (CC == BedrockFPOneCond) {
      BuildMI(*MBB, MI, DL, TII.get(Bedrock::SETCC), LHSResult).addImm(0x3);
      BuildMI(*MBB, MI, DL, TII.get(Bedrock::SETCC), RHSResult).addImm(0x9);
      BuildMI(*MBB, MI, DL, TII.get(Bedrock::ANDQ3rr), Dst)
          .addReg(LHSResult)
          .addReg(RHSResult);
    } else {
      assert(CC == BedrockFPEqualOrUnorderedCond &&
             "unexpected compound floating-point condition");
      BuildMI(*MBB, MI, DL, TII.get(Bedrock::SETCC), LHSResult).addImm(0x2);
      BuildMI(*MBB, MI, DL, TII.get(Bedrock::SETCC), RHSResult).addImm(0x8);
      BuildMI(*MBB, MI, DL, TII.get(Bedrock::ORQ3rr), Dst)
          .addReg(LHSResult)
          .addReg(RHSResult);
    }
    MI.eraseFromParent();
    return MBB;
  }

  assert((MI.getOpcode() == Bedrock::SELECT_CC_L ||
          MI.getOpcode() == Bedrock::SELECT_CC_Q ||
          MI.getOpcode() == Bedrock::SELECT_CC_S ||
          MI.getOpcode() == Bedrock::SELECT_CC_D) &&
         "unexpected Bedrock custom inserter opcode");

  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineFunction *MF = MBB->getParent();
  MachineFunction::iterator InsertPt = ++MBB->getIterator();

  MachineBasicBlock *ThisMBB = MBB;
  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(InsertPt, FalseMBB);
  MF->insert(InsertPt, SinkMBB);

  SinkMBB->splice(SinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(MBB);

  MBB->addSuccessor(FalseMBB);
  MBB->addSuccessor(SinkMBB);

  Register Dst = MI.getOperand(0).getReg();

  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  Register TrueValue = MI.getOperand(3).getReg();
  Register FalseValue = MI.getOperand(4).getReg();
  int64_t CC = MI.getOperand(5).getImm();
  unsigned CmpOpcode = MI.getOpcode() == Bedrock::SELECT_CC_L
                           ? Bedrock::CMPLrr
                           : Bedrock::CMPQrr;

  BuildMI(*MBB, MI, DL, TII.get(CmpOpcode)).addReg(RHS).addReg(LHS);
  BuildMI(*MBB, MI, DL, TII.get(Bedrock::BRCC)).addMBB(SinkMBB).addImm(CC);

  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(Bedrock::PHI), Dst)
      .addReg(FalseValue)
      .addMBB(FalseMBB)
      .addReg(TrueValue)
      .addMBB(ThisMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

SDValue BedrockTargetLowering::LowerSIGN_EXTEND_INREG(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDValue Value = Op.getOperand(0);
  EVT VT = Value.getValueType();
  EVT ExtVT = getVTSDNodeOperand(Op.getOperand(1), "SIGN_EXTEND_INREG");
  SDLoc DL(Op);

  unsigned Shift = VT.getScalarSizeInBits() - ExtVT.getScalarSizeInBits();
  EVT ShiftVT = getScalarShiftAmountTy(DAG.getDataLayout(), VT);
  SDValue ShiftValue = DAG.getConstant(Shift, DL, ShiftVT);
  SDValue Shifted = DAG.getNode(ISD::SHL, DL, VT, Value, ShiftValue);
  return DAG.getNode(ISD::SRA, DL, VT, Shifted, ShiftValue);
}

static SDValue convertLocVT(SDValue Value, const CCValAssign &VA,
                            const SDLoc &DL, SelectionDAG &DAG) {
  switch (VA.getLocInfo()) {
  case CCValAssign::Full:
    return Value;
  case CCValAssign::SExt:
    Value = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Value,
                        DAG.getValueType(VA.getValVT()));
    return DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Value);
  case CCValAssign::ZExt:
    Value = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Value,
                        DAG.getValueType(VA.getValVT()));
    return DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Value);
  case CCValAssign::AExt:
    return DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Value);
  default:
    llvm_unreachable("unknown argument location info");
  }
}

static SDValue promoteToLocVT(SDValue Value, const CCValAssign &VA,
                              const SDLoc &DL, SelectionDAG &DAG) {
  switch (VA.getLocInfo()) {
  case CCValAssign::Full:
    return Value;
  case CCValAssign::SExt:
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::ZExt:
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::AExt:
    return DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Value);
  default:
    llvm_unreachable("unknown argument location info");
  }
}

static bool isSupportedCallingConv(CallingConv::ID CallConv) {
  return CallConv == CallingConv::C || CallConv == CallingConv::Fast;
}

static bool referencesFrameIndex(SDValue Value,
                                 SmallPtrSetImpl<SDNode *> &Visited) {
  SDNode *Node = Value.getNode();
  if (!Node || !Visited.insert(Node).second)
    return false;
  if (isa<FrameIndexSDNode>(Node))
    return true;
  return any_of(Node->ops(), [&](const SDUse &Use) {
    return referencesFrameIndex(Use.get(), Visited);
  });
}

SDValue BedrockTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (!isSupportedCallingConv(CallConv))
    report_fatal_error("Bedrock only supports C-compatible calling conventions");
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  BedrockCCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Bedrock);

  if (IsVarArg) {
    // Named stack arguments precede the unnamed area. The fixed object uses
    // the callee's entry-SP view, where ordinary arguments start at entry
    // SP + 16 after the return address and alignment padding.
    int64_t EntryOffset = 16 + CCInfo.getStackSize();
    int FI = MFI.CreateFixedObject(1, EntryOffset, /*IsImmutable=*/true);
    MFI.setObjectAlignment(FI, commonAlignment(Align(16), EntryOffset));
    MF.getInfo<BedrockMachineFunctionInfo>()->setVarArgsFrameIndex(FI);
  }

  SmallVector<SDValue, 8> ArgChains;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue ArgValue;
    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC =
          VA.getLocVT().isFloatingPoint() ? &Bedrock::FPR64RegClass
                                          : &Bedrock::GPR64RegClass;
      Register VReg = RegInfo.createVirtualRegister(RC);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());
      ArgChains.push_back(ArgValue.getValue(1));
    } else {
      assert(VA.isMemLoc() && "argument must be in a register or on stack");
      int64_t EntryOffset = 16 + VA.getLocMemOffset();
      uint64_t ObjSize = VA.getLocVT().getStoreSize();
      int FI = MFI.CreateFixedObject(ObjSize, EntryOffset, /*IsImmutable=*/true);
      MFI.setObjectAlignment(FI, commonAlignment(Align(16), EntryOffset));
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      ArgValue = DAG.getLoad(
          VA.getLocVT(), DL, Chain, FIN,
          MachinePointerInfo::getFixedStack(MF, FI));
      ArgChains.push_back(ArgValue.getValue(1));
    }
    InVals.push_back(convertLocVT(ArgValue, VA, DL, DAG));
  }

  if (!ArgChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, ArgChains);

  for (unsigned I = 0, E = Ins.size(); I != E; ++I) {
    if (!Ins[I].Flags.isSRet())
      continue;
    auto *FuncInfo = MF.getInfo<BedrockMachineFunctionInfo>();
    Register SRetReg = RegInfo.createVirtualRegister(&Bedrock::GPR64RegClass);
    FuncInfo->setSRetReturnReg(SRetReg);
    SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), DL, SRetReg, InVals[I]);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chain, Copy);
    break;
  }
  return Chain;
}

SDValue
BedrockTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                 SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  bool TailCallRequested = CLI.IsTailCall;
  CLI.IsTailCall = false;

  if (!isSupportedCallingConv(CallConv))
    report_fatal_error("Bedrock only supports C-compatible calling conventions");

  // Opaque pointers allow hand-written IR to omit the variadic call type even
  // when the directly referenced declaration is variadic. Recover the fixed
  // prefix so such calls still obey the target ABI.
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    if (const auto *F = dyn_cast<Function>(G->getGlobal());
        F && F->isVarArg()) {
      IsVarArg = true;
      for (ISD::OutputArg &Out : Outs)
        if (Out.OrigArgIndex >= F->arg_size())
          Out.Flags.setVarArg();
    }
  }
  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<CCValAssign, 16> ArgLocs;
  BedrockCCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_Bedrock);

  bool HasByVal = any_of(Outs, [](const ISD::OutputArg &Out) {
    return Out.Flags.isByVal();
  });
  bool CalleeUsesSRet = any_of(Outs, [](const ISD::OutputArg &Out) {
    return Out.Flags.isSRet();
  });
  bool PassesCallerFrameAddress = false;
  for (unsigned I = 0, E = Outs.size(); I != E; ++I) {
    if (!Outs[I].OrigTy || !Outs[I].OrigTy->isPointerTy())
      continue;
    SmallPtrSet<SDNode *, 8> Visited;
    PassesCallerFrameAddress |= referencesFrameIndex(OutVals[I], Visited);
  }
  const Function &Caller = MF.getFunction();
  bool TailCallEligible =
      TailCallRequested && CallConv == Caller.getCallingConv() &&
      CCInfo.getStackSize() == 0 && !HasByVal && !PassesCallerFrameAddress &&
      Caller.hasStructRetAttr() == CalleeUsesSRet &&
      CLI.OrigRetTy == Caller.getReturnType() &&
      Caller.hasRetAttribute(Attribute::SExt) == CLI.RetSExt &&
      Caller.hasRetAttribute(Attribute::ZExt) == CLI.RetZExt;
  if (!TailCallEligible && CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("failed to perform Bedrock tail call elimination on a "
                       "call site marked musttail");

  SmallVector<SDValue, 16> ArgValues(OutVals.begin(), OutVals.end());
  for (unsigned I = 0, E = Outs.size(); I != E; ++I) {
    ISD::ArgFlagsTy Flags = Outs[I].Flags;
    if (!Flags.isByVal())
      continue;

    unsigned Size = Flags.getByValSize();
    Align Alignment(16);
    int FI = MF.getFrameInfo().CreateStackObject(Size, Alignment,
                                                  /*isSpillSlot=*/false);
    SDValue FIPtr =
        DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    SDValue SizeNode = DAG.getConstant(Size, DL, MVT::i64);
    Chain = DAG.getMemcpy(Chain, DL, FIPtr, ArgValues[I], SizeNode, Alignment,
                          /*IsVolatile=*/false, /*AlwaysInline=*/false,
                          /*CI=*/nullptr, std::nullopt, MachinePointerInfo(),
                          MachinePointerInfo());
    ArgValues[I] = FIPtr;
  }

  uint64_t CallFrameSize = 8 + CCInfo.getStackSize();
  Chain = DAG.getCALLSEQ_START(Chain, CallFrameSize, 0, DL);

  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  struct StackArgStore {
    int64_t Offset;
    SDValue Value;
    SDValue Address;
  };
  SmallVector<StackArgStore, 8> StackArgs;
  SDValue StackPtr;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue ArgValue = promoteToLocVT(ArgValues[I], VA, DL, DAG);
    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), ArgValue});
      continue;
    }

    assert(VA.isMemLoc() && "argument must be in a register or on stack");
    if (!StackPtr.getNode())
      StackPtr = DAG.getCopyFromReg(
          Chain, DL, Bedrock::SP, getPointerTy(DAG.getDataLayout()));
    int64_t CallerOffset = 8 + VA.getLocMemOffset();
    SDValue Address = DAG.getNode(
        ISD::ADD, DL, getPointerTy(DAG.getDataLayout()), StackPtr,
        DAG.getIntPtrConstant(CallerOffset, DL));
    StackArgs.push_back({CallerOffset, ArgValue, Address});
  }

  // The C ABI makes the right-to-left materialization order observable even
  // though the stack slots do not alias. Chain the stores so later scheduling
  // cannot reorder them as independent memory operations.
  llvm::sort(StackArgs, [](const StackArgStore &LHS,
                           const StackArgStore &RHS) {
    return LHS.Offset > RHS.Offset;
  });
  for (const StackArgStore &Store : StackArgs)
    Chain = DAG.getStore(Chain, DL, Store.Value, Store.Address,
                         MachinePointerInfo(), std::nullopt,
                         MachineMemOperand::MOVolatile);

  SDValue InGlue;
  for (const auto &[Reg, Value] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Value, InGlue);
    InGlue = Chain.getValue(1);
  }

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    bool IsLarge = DAG.getTarget().getCodeModel() == CodeModel::Large;
    if (IsLarge) {
      unsigned Flag;
      if (!DAG.getTarget().isPositionIndependent())
        Flag = BedrockII::MO_ABS64;
      else if (G->getGlobal()->isDSOLocal())
        Flag = BedrockII::MO_PCREL64;
      else
        Flag = BedrockII::MO_GOTPCREL64;
      SDValue Symbol = DAG.getTargetGlobalAddress(
          G->getGlobal(), DL, getPointerTy(DAG.getDataLayout()),
          G->getOffset(), Flag);
      Callee = DAG.getNode(BedrockISD::CALL_ADDRESS, DL,
                           getPointerTy(DAG.getDataLayout()), Symbol);
    } else {
      unsigned Flag = DAG.getTarget().isPositionIndependent() &&
                              !G->getGlobal()->isDSOLocal()
                          ? BedrockII::MO_PLT32
                          : BedrockII::MO_NONE;
      Callee = DAG.getTargetGlobalAddress(
          G->getGlobal(), DL, getPointerTy(DAG.getDataLayout()),
          G->getOffset(), Flag);
    }
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    bool IsLarge = DAG.getTarget().getCodeModel() == CodeModel::Large;
    if (IsLarge) {
      unsigned Flag = DAG.getTarget().isPositionIndependent()
                          ? BedrockII::MO_GOTPCREL64
                          : BedrockII::MO_ABS64;
      SDValue Symbol = DAG.getTargetExternalSymbol(
          E->getSymbol(), getPointerTy(DAG.getDataLayout()), Flag);
      Callee = DAG.getNode(BedrockISD::CALL_ADDRESS, DL,
                           getPointerTy(DAG.getDataLayout()), Symbol);
    } else {
      unsigned Flag = DAG.getTarget().isPositionIndependent()
                          ? BedrockII::MO_PLT32
                          : BedrockII::MO_NONE;
      Callee = DAG.getTargetExternalSymbol(
          E->getSymbol(), getPointerTy(DAG.getDataLayout()), Flag);
    }
  }

  const BedrockRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
  assert(Mask && "missing Bedrock call preserved mask");

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 16> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  Ops.push_back(DAG.getRegisterMask(Mask));
  for (const auto &[Reg, Value] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, Value.getValueType()));
  if (InGlue.getNode())
    Ops.push_back(InGlue);

  unsigned CallOpcode = TailCallEligible ? BedrockISD::TAIL_CALL_CANDIDATE
                                         : BedrockISD::CALL;
  Chain = DAG.getNode(CallOpcode, DL, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, CallFrameSize, 0, InGlue, DL);
  InGlue = Chain.getValue(1);

  return LowerCallResult(Chain, InGlue, CallConv, IsVarArg, Ins, DL, DAG,
                         InVals);
}

SDValue BedrockTargetLowering::LowerVASTART(SDValue Op,
                                             SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto *FuncInfo = MF.getInfo<BedrockMachineFunctionInfo>();
  if (!FuncInfo->hasVarArgsFrameIndex())
    report_fatal_error("va_start used in a non-variadic Bedrock function");
  int FI = FuncInfo->getVarArgsFrameIndex();

  SDValue ListAddress = Op.getOperand(1);
  SDValue FirstUnnamed = DAG.getFrameIndex(FI, ListAddress.getValueType());
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), SDLoc(Op), FirstUnnamed, ListAddress,
                      MachinePointerInfo(SV));
}

SDValue BedrockTargetLowering::LowerGlobalTLSAddress(
    SDValue Op, SelectionDAG &DAG) const {
  auto *GA = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GA->getGlobal();
  SDLoc DL(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  TLSModel::Model Model = getTargetMachine().getTLSModel(GV);
  bool Is64BitField = DAG.getTarget().getCodeModel() == CodeModel::Large;

  if (Model == TLSModel::LocalExec) {
    unsigned Flag = Is64BitField ? BedrockII::MO_TLS_LE64
                                 : BedrockII::MO_TLS_LE32;
    SDValue Symbol =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, GA->getOffset(), Flag);
    return DAG.getNode(BedrockISD::TLS_ADDRESS, DL, PtrVT, Symbol);
  }

  unsigned Flag = Is64BitField ? BedrockII::MO_TLSDESC64
                               : BedrockII::MO_TLSDESC32;
  SDValue Symbol =
      DAG.getTargetGlobalAddress(GV, DL, PtrVT, GA->getOffset(), Flag);
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Chain = DAG.getNode(BedrockISD::TLSDESC_CALL, DL, NodeTys,
                              DAG.getEntryNode(), Symbol);
  return DAG.getCopyFromReg(Chain, DL, Bedrock::R0, PtrVT, Chain.getValue(1));
}

SDValue BedrockTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC_Bedrock);

  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    const CCValAssign &VA = RVLocs[I];
    SDValue RetValue =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), InGlue);
    Chain = RetValue.getValue(1);
    InGlue = RetValue.getValue(2);
    InVals.push_back(convertLocVT(RetValue, VA, DL, DAG));
  }
  return Chain;
}

bool BedrockTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_Bedrock);
}

SDValue
BedrockTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                   bool IsVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   const SmallVectorImpl<SDValue> &OutVals,
                                   const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 4> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_Bedrock);

  SDValue Glue;
  SmallVector<SDValue, 8> RetOps(1, Chain);
  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    const CCValAssign &VA = RVLocs[I];
    SDValue Value = promoteToLocVT(OutVals[I], VA, DL, DAG);
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Value, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  MachineFunction &MF = DAG.getMachineFunction();
  if (MF.getFunction().hasStructRetAttr()) {
    Register SRetReg =
        MF.getInfo<BedrockMachineFunctionInfo>()->getSRetReturnReg();
    if (!SRetReg)
      report_fatal_error("missing Bedrock sret return register");

    SDValue SRetValue = DAG.getCopyFromReg(Chain, DL, SRetReg, MVT::i64);
    Chain = SRetValue.getValue(1);
    Chain = DAG.getCopyToReg(Chain, DL, Bedrock::R0, SRetValue, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(Bedrock::R0, MVT::i64));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(BedrockISD::RET_FLAG, DL, MVT::Other, RetOps);
}
