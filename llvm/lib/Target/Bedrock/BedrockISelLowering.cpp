//===-- BedrockISelLowering.cpp - Bedrock DAG Lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockISelLowering.h"

#include "BedrockInstrInfo.h"
#include "BedrockMachineFunctionInfo.h"
#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockCondCode.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

#include <iterator>
#include <limits>
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "bedrock-lower"

#include "BedrockGenCallingConv.inc"

static const TargetRegisterClass *getRegClassForLoc(unsigned Reg, MVT VT) {
  if (Reg >= Bedrock::A0 && Reg <= Bedrock::A7)
    return &Bedrock::A64RegClass;
  if (Reg >= Bedrock::F0 && Reg <= Bedrock::F15)
    return VT == MVT::f32 ? &Bedrock::F32RegClass : &Bedrock::F64RegClass;
  switch (VT.SimpleTy) {
  default:
    return &Bedrock::GPR64RegClass;
  case MVT::i8:
    return &Bedrock::GPR8RegClass;
  case MVT::i16:
    return &Bedrock::GPR16RegClass;
  case MVT::i32:
    return &Bedrock::GPR32RegClass;
  case MVT::i64:
    return &Bedrock::GPR64RegClass;
  }
}

static SDValue convertLocVT(CCValAssign &VA, SDValue Val, const SDLoc &DL,
                            SelectionDAG &DAG) {
  switch (VA.getLocInfo()) {
  case CCValAssign::Full:
    return Val;
  case CCValAssign::SExt:
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Val);
  case CCValAssign::ZExt:
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Val);
  case CCValAssign::AExt:
    return DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Val);
  case CCValAssign::BCvt:
    return DAG.getBitcast(VA.getLocVT(), Val);
  case CCValAssign::Trunc:
    return DAG.getNode(ISD::TRUNCATE, DL, VA.getLocVT(), Val);
  case CCValAssign::Indirect:
  case CCValAssign::FPExt:
  case CCValAssign::SExtUpper:
  case CCValAssign::ZExtUpper:
  case CCValAssign::AExtUpper:
  case CCValAssign::VExt:
    break;
  }
  llvm_unreachable("unsupported Bedrock calling-convention location");
}

static SDValue convertValVT(CCValAssign &VA, SDValue Val, const SDLoc &DL,
                            SelectionDAG &DAG) {
  switch (VA.getLocInfo()) {
  case CCValAssign::Full:
    return Val;
  case CCValAssign::SExt:
  case CCValAssign::ZExt:
  case CCValAssign::AExt:
  case CCValAssign::Trunc:
    return DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
  case CCValAssign::BCvt:
    return DAG.getBitcast(VA.getValVT(), Val);
  case CCValAssign::Indirect:
  case CCValAssign::FPExt:
  case CCValAssign::SExtUpper:
  case CCValAssign::ZExtUpper:
  case CCValAssign::AExtUpper:
  case CCValAssign::VExt:
    break;
  }
  llvm_unreachable("unsupported Bedrock calling-convention location");
}

static unsigned movccOpcodeForSelect(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("unexpected Bedrock select opcode");
  case Bedrock::SELECT8:
    return Bedrock::MOVCC8rr;
  case Bedrock::SELECT16:
    return Bedrock::MOVCC16rr;
  case Bedrock::SELECT32:
    return Bedrock::MOVCC32rr;
  case Bedrock::SELECT64:
    return Bedrock::MOVCC64rr;
  case Bedrock::FSELECT32:
    return Bedrock::FMOVCC32rr;
  case Bedrock::FSELECT64:
    return Bedrock::FMOVCC64rr;
  }
}

static BedrockCC::CondCode getBedrockCondCode(ISD::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("unsupported Bedrock integer condition code");
  case ISD::SETFALSE:
  case ISD::SETFALSE2:
    return BedrockCC::F;
  case ISD::SETTRUE:
  case ISD::SETTRUE2:
    return BedrockCC::T;
  case ISD::SETEQ:
    return BedrockCC::EQ;
  case ISD::SETNE:
    return BedrockCC::NE;
  case ISD::SETULT:
    return BedrockCC::ULT;
  case ISD::SETUGE:
    return BedrockCC::UGE;
  case ISD::SETULE:
    return BedrockCC::ULE;
  case ISD::SETUGT:
    return BedrockCC::UGT;
  case ISD::SETLT:
    return BedrockCC::LT;
  case ISD::SETGE:
    return BedrockCC::GE;
  case ISD::SETLE:
    return BedrockCC::LE;
  case ISD::SETGT:
    return BedrockCC::GT;
  }
}

static BedrockCC::CondCode getBedrockFloatCondCode(ISD::CondCode CC,
                                                   bool &SwapOperands) {
  SwapOperands = false;
  switch (CC) {
  default:
    llvm_unreachable("unsupported Bedrock floating-point condition code");
  case ISD::SETFALSE:
  case ISD::SETFALSE2:
    return BedrockCC::F;
  case ISD::SETTRUE:
  case ISD::SETTRUE2:
    return BedrockCC::T;
  case ISD::SETEQ:
  case ISD::SETOEQ:
    return BedrockCC::EQ;
  case ISD::SETNE:
  case ISD::SETUNE:
    return BedrockCC::NE;
  case ISD::SETO:
    return BedrockCC::VC;
  case ISD::SETUO:
    return BedrockCC::VS;
  case ISD::SETLT:
  case ISD::SETOLT:
    return BedrockCC::MI;
  case ISD::SETGT:
  case ISD::SETOGT:
    return BedrockCC::GT;
  case ISD::SETGE:
  case ISD::SETOGE:
    return BedrockCC::GE;
  case ISD::SETLE:
  case ISD::SETOLE:
    SwapOperands = true;
    return BedrockCC::GE;
  case ISD::SETULT:
    return BedrockCC::LT;
  case ISD::SETUGT:
    SwapOperands = true;
    return BedrockCC::LT;
  case ISD::SETULE:
    return BedrockCC::LE;
  case ISD::SETUGE:
    SwapOperands = true;
    return BedrockCC::LE;
  }
}

static unsigned getBedrockMemoryOrder(AtomicOrdering Order) {
  switch (Order) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
  case AtomicOrdering::Monotonic:
    return 0; // RELAXED
  case AtomicOrdering::Acquire:
    return 1; // ACQUIRE
  case AtomicOrdering::Release:
    return 2; // RELEASE
  case AtomicOrdering::AcquireRelease:
    return 3; // ACQREL
  case AtomicOrdering::SequentiallyConsistent:
    return 4; // SEQCST
  }
  llvm_unreachable("unknown atomic ordering");
}

static bool needsLeadingLoadFence(AtomicOrdering Order) {
  return Order == AtomicOrdering::SequentiallyConsistent;
}

static bool needsTrailingLoadFence(AtomicOrdering Order) {
  return Order == AtomicOrdering::Acquire ||
         Order == AtomicOrdering::SequentiallyConsistent;
}

static bool needsLeadingStoreFence(AtomicOrdering Order) {
  return Order == AtomicOrdering::Release ||
         Order == AtomicOrdering::SequentiallyConsistent;
}

static bool needsTrailingStoreFence(AtomicOrdering Order) {
  return Order == AtomicOrdering::SequentiallyConsistent;
}

static SDValue makeAFENCE(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain) {
  return DAG.getNode(BedrockISD::AFENCE, DL, MVT::Other, Chain);
}

static SDValue lowerBedrockCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                                   const SDLoc &DL, SelectionDAG &DAG,
                                   SDValue &TargetCC) {
  if (LHS.getValueType().isFloatingPoint()) {
    bool SwapOperands = false;
    TargetCC =
        DAG.getConstant(getBedrockFloatCondCode(CC, SwapOperands), DL, MVT::i8);
    if (SwapOperands)
      std::swap(LHS, RHS);
    return DAG.getNode(BedrockISD::FCMP, DL, MVT::Glue, LHS, RHS);
  }

  TargetCC = DAG.getConstant(getBedrockCondCode(CC), DL, MVT::i8);
  return DAG.getNode(BedrockISD::CMP, DL, MVT::Glue, LHS, RHS);
}

static bool isIntegerConstant(SDValue V, int64_t Expected) {
  if (auto *C = dyn_cast<ConstantSDNode>(V))
    return C->getSExtValue() == Expected;
  return false;
}

static bool isZeroExtendOf(SDValue V, SDValue Src) {
  return V.getOpcode() == ISD::ZERO_EXTEND && V.getOperand(0) == Src;
}

static bool isSignMaskOrOf(SDValue V, SDValue ZExt, unsigned SrcBits) {
  if (V.getOpcode() != ISD::OR)
    return false;
  int64_t SignMask = -1LL << SrcBits;
  return (V.getOperand(0) == ZExt &&
          isIntegerConstant(V.getOperand(1), SignMask)) ||
         (V.getOperand(1) == ZExt &&
          isIntegerConstant(V.getOperand(0), SignMask));
}

BedrockTargetLowering::BedrockTargetLowering(const TargetMachine &TM,
                                             const BedrockSubtarget &STI)
    : TargetLowering(TM, STI) {
  addRegisterClass(MVT::i8, &Bedrock::GPR8RegClass);
  addRegisterClass(MVT::i16, &Bedrock::GPR16RegClass);
  addRegisterClass(MVT::i32, &Bedrock::GPR32RegClass);
  addRegisterClass(MVT::i64, &Bedrock::GPR64RegClass);
  addRegisterClass(MVT::f32, &Bedrock::F32RegClass);
  addRegisterClass(MVT::f64, &Bedrock::F64RegClass);

  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(Bedrock::SP);
  setBooleanContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
  setMaxAtomicSizeInBitsSupported(64);

  MVT PtrVT = MVT::i64;
  setOperationAction(ISD::GlobalAddress, PtrVT, Custom);
  setOperationAction(ISD::ExternalSymbol, PtrVT, Custom);
  setOperationAction(ISD::BlockAddress, PtrVT, Custom);
  setOperationAction(ISD::ConstantPool, PtrVT, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  for (MVT VT : {MVT::i1, MVT::i8})
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);
  for (MVT VT : {MVT::i16, MVT::i32, MVT::i64})
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Custom);

  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::ROTL, VT, Legal);
    setOperationAction(ISD::ROTR, VT, Legal);
    setOperationAction(ISD::CTPOP, VT, Legal);
    setOperationAction(ISD::CTLZ, VT, Legal);
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Legal);
    setOperationAction(ISD::CTTZ, VT, Legal);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Legal);
    setOperationAction(ISD::CLMUL, VT, Legal);
    setOperationAction(ISD::CLMULH, VT, Expand);
    setOperationAction(ISD::CLMULR, VT, Expand);
    setOperationAction(ISD::ABS, VT, Legal);
    setOperationAction(ISD::SMIN, VT, Legal);
    setOperationAction(ISD::SMAX, VT, Legal);
    setOperationAction(ISD::UMIN, VT, Legal);
    setOperationAction(ISD::UMAX, VT, Legal);
    setOperationAction(ISD::MUL, VT, Legal);
    setOperationAction(ISD::SDIV, VT, Legal);
    setOperationAction(ISD::UDIV, VT, Legal);
    setOperationAction(ISD::SREM, VT, Legal);
    setOperationAction(ISD::UREM, VT, Legal);
    setOperationAction(ISD::MULHS, VT, Legal);
    setOperationAction(ISD::MULHU, VT, Legal);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SHL_PARTS, VT, Expand);
    setOperationAction(ISD::SRL_PARTS, VT, Expand);
    setOperationAction(ISD::SRA_PARTS, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD, VT, Custom);
    setOperationAction(ISD::ATOMIC_STORE, VT, Custom);
    setOperationAction(ISD::ATOMIC_LOAD_ADD, VT, Custom);
    setOperationAction(ISD::ATOMIC_LOAD_SUB, VT, Custom);
    setOperationAction(ISD::ATOMIC_LOAD_AND, VT, Custom);
    setOperationAction(ISD::ATOMIC_LOAD_OR, VT, Custom);
    setOperationAction(ISD::ATOMIC_LOAD_XOR, VT, Custom);
    setOperationAction(ISD::ATOMIC_CMP_SWAP, VT, Custom);
    setOperationAction(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, VT, Expand);
    setOperationAction(ISD::ATOMIC_SWAP, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_NAND, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_MIN, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_MAX, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_UMIN, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_UMAX, VT, Expand);

    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setTruncStoreAction(VT, MVT::i1, Expand);
  }

  for (MVT ValVT : {MVT::i16, MVT::i32, MVT::i64}) {
    for (MVT MemVT : {MVT::i8, MVT::i16, MVT::i32}) {
      if (MemVT.bitsLT(ValVT))
        setTruncStoreAction(ValVT, MemVT, Expand);
    }
  }

  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::BITCAST, VT, Expand);
    setOperationAction(ISD::FADD, VT, Legal);
    setOperationAction(ISD::FSUB, VT, Legal);
    setOperationAction(ISD::FMUL, VT, Legal);
    setOperationAction(ISD::FDIV, VT, Legal);
    setOperationAction(ISD::FABS, VT, Legal);
    setOperationAction(ISD::FNEG, VT, Legal);
    setOperationAction(ISD::FCOPYSIGN, VT, Legal);
    setOperationAction(ISD::SINT_TO_FP, VT, Legal);
    setOperationAction(ISD::UINT_TO_FP, VT, Legal);
  }

  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Legal);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Legal);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Legal);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Legal);
  setOperationAction(ISD::BITCAST, MVT::i32, Expand);
  setOperationAction(ISD::BITCAST, MVT::i64, Expand);
  setOperationAction(ISD::FP_ROUND, MVT::f32, Legal);
  setOperationAction(ISD::FP_EXTEND, MVT::f64, Legal);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);

  for (MVT MemVT : {MVT::i8, MVT::i16}) {
    for (MVT VT : {MVT::i16, MVT::i32, MVT::i64}) {
      if (MemVT.bitsGE(VT))
        continue;
      setLoadExtAction(ISD::EXTLOAD, VT, MemVT, Expand);
      setLoadExtAction(ISD::SEXTLOAD, VT, MemVT, Legal);
      setLoadExtAction(ISD::ZEXTLOAD, VT, MemVT, Legal);
    }
  }
  setLoadExtAction(ISD::EXTLOAD, MVT::i64, MVT::i32, Expand);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MVT::i32, Legal);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, MVT::i32, Legal);

  setOperationAction(ISD::DYNAMIC_STACKALLOC, PtrVT, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);

  setMinimumJumpTableEntries(std::numeric_limits<unsigned>::max());
}

bool BedrockTargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned AddrSpace, Align Alignment, MachineMemOperand::Flags Flags,
    unsigned *Fast) const {
  if (AddrSpace != 0 || (Flags & MachineMemOperand::MOVolatile))
    return false;

  EVT ScalarVT = VT.getScalarType();
  if (!ScalarVT.isSimple() ||
      (!ScalarVT.isInteger() && !ScalarVT.isFloatingPoint()))
    return false;

  unsigned Bytes = ScalarVT.getStoreSize();
  if (Bytes != 1 && Bytes != 2 && Bytes != 4 && Bytes != 8)
    return false;

  if (Fast)
    *Fast = 1;
  return true;
}

bool BedrockTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                  const AddrMode &AM, Type *Ty,
                                                  unsigned AS,
                                                  Instruction *I) const {
  if (AS != 0 || AM.BaseGV || AM.ScalableOffset)
    return false;
  if (!isInt<32>(AM.BaseOffs))
    return false;

  if (Ty && !Ty->isVoidTy() && !Ty->isIntegerTy() && !Ty->isFloatingPointTy() &&
      !Ty->isPointerTy())
    return false;

  if (AM.Scale == 0)
    return AM.HasBaseReg;

  return AM.HasBaseReg && AM.Scale == 4;
}

const char *BedrockTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case BedrockISD::RET_GLUE:
    return "BedrockISD::RET_GLUE";
  case BedrockISD::CALL:
    return "BedrockISD::CALL";
  case BedrockISD::CALL16:
    return "BedrockISD::CALL16";
  case BedrockISD::CMP:
    return "BedrockISD::CMP";
  case BedrockISD::BR_CC:
    return "BedrockISD::BR_CC";
  case BedrockISD::SELECT_CC:
    return "BedrockISD::SELECT_CC";
  case BedrockISD::Wrapper:
    return "BedrockISD::Wrapper";
  case BedrockISD::AFENCE:
    return "BedrockISD::AFENCE";
  case BedrockISD::FETCHADD:
    return "BedrockISD::FETCHADD";
  case BedrockISD::FETCHSUB:
    return "BedrockISD::FETCHSUB";
  case BedrockISD::FETCHAND:
    return "BedrockISD::FETCHAND";
  case BedrockISD::FETCHOR:
    return "BedrockISD::FETCHOR";
  case BedrockISD::FETCHXOR:
    return "BedrockISD::FETCHXOR";
  case BedrockISD::CMPXCHG:
    return "BedrockISD::CMPXCHG";
  case BedrockISD::FCMP:
    return "BedrockISD::FCMP";
  case BedrockISD::BND:
    return "BedrockISD::BND";
  case BedrockISD::BTEST:
    return "BedrockISD::BTEST";
  default:
    return nullptr;
  }
}

SDValue BedrockTargetLowering::LowerOperation(SDValue Op,
                                              SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::BRCOND:
    return LowerBRCOND(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::SIGN_EXTEND_INREG:
    return LowerSIGN_EXTEND_INREG(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::ATOMIC_FENCE:
    return LowerATOMIC_FENCE(Op, DAG);
  case ISD::ATOMIC_LOAD:
    return LowerATOMIC_LOAD(Op, DAG);
  case ISD::ATOMIC_STORE:
    return LowerATOMIC_STORE(Op, DAG);
  case ISD::ATOMIC_LOAD_ADD:
    return LowerATOMIC_LOAD_OP(Op, DAG, BedrockISD::FETCHADD);
  case ISD::ATOMIC_LOAD_SUB:
    return LowerATOMIC_LOAD_OP(Op, DAG, BedrockISD::FETCHSUB);
  case ISD::ATOMIC_LOAD_AND:
    return LowerATOMIC_LOAD_OP(Op, DAG, BedrockISD::FETCHAND);
  case ISD::ATOMIC_LOAD_OR:
    return LowerATOMIC_LOAD_OP(Op, DAG, BedrockISD::FETCHOR);
  case ISD::ATOMIC_LOAD_XOR:
    return LowerATOMIC_LOAD_OP(Op, DAG, BedrockISD::FETCHXOR);
  case ISD::ATOMIC_CMP_SWAP:
    return LowerATOMIC_CMP_SWAP(Op, DAG);
  case ISD::BlockAddress: {
    auto *BA = cast<BlockAddressSDNode>(Op);
    SDLoc DL(Op);
    SDValue Target = DAG.getTargetBlockAddress(
        BA->getBlockAddress(), MVT::i64, BA->getOffset(), BA->getTargetFlags());
    return DAG.getNode(BedrockISD::Wrapper, DL, MVT::i64, Target);
  }
  case ISD::ConstantPool: {
    auto *CP = cast<ConstantPoolSDNode>(Op);
    SDLoc DL(Op);
    SDValue Target =
        DAG.getTargetConstantPool(CP->getConstVal(), MVT::i64, CP->getAlign(),
                                  CP->getOffset(), CP->getTargetFlags());
    return DAG.getNode(BedrockISD::Wrapper, DL, MVT::i64, Target);
  }
  default:
    llvm_unreachable("unimplemented Bedrock lowering");
  }
}

SDValue BedrockTargetLowering::LowerATOMIC_FENCE(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  AtomicOrdering Order =
      static_cast<AtomicOrdering>(Op.getConstantOperandVal(1));
  SyncScope::ID Scope = static_cast<SyncScope::ID>(Op.getConstantOperandVal(2));

  if (Scope == SyncScope::SingleThread || Order == AtomicOrdering::Monotonic)
    return DAG.getNode(ISD::MEMBARRIER, DL, MVT::Other, Op.getOperand(0));

  return makeAFENCE(DAG, DL, Op.getOperand(0));
}

SDValue BedrockTargetLowering::LowerATOMIC_LOAD(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());
  SDLoc DL(Op);
  AtomicOrdering Order = Node->getMergedOrdering();
  SDValue Chain = Node->getChain();

  if (needsLeadingLoadFence(Order))
    Chain = makeAFENCE(DAG, DL, Chain);

  SDValue Load = Node->getExtensionType() == ISD::NON_EXTLOAD
                     ? DAG.getLoad(Op.getValueType(), DL, Chain,
                                   Node->getBasePtr(), Node->getMemOperand())
                     : DAG.getExtLoad(Node->getExtensionType(), DL,
                                      Op.getValueType(), Chain,
                                      Node->getBasePtr(), Node->getMemoryVT(),
                                      Node->getMemOperand());
  Chain = Load.getValue(1);

  if (needsTrailingLoadFence(Order))
    Chain = makeAFENCE(DAG, DL, Chain);

  return DAG.getMergeValues({Load, Chain}, DL);
}

SDValue BedrockTargetLowering::LowerATOMIC_STORE(SDValue Op,
                                                 SelectionDAG &DAG) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());
  SDLoc DL(Op);
  AtomicOrdering Order = Node->getMergedOrdering();
  SDValue Chain = Node->getChain();

  if (needsLeadingStoreFence(Order))
    Chain = makeAFENCE(DAG, DL, Chain);

  SDValue Val = Node->getVal();
  EVT MemVT = Node->getMemoryVT();
  Chain = Val.getValueType() == MemVT
              ? DAG.getStore(Chain, DL, Val, Node->getBasePtr(),
                             Node->getMemOperand())
              : DAG.getTruncStore(Chain, DL, Val, Node->getBasePtr(), MemVT,
                                  Node->getMemOperand());

  if (needsTrailingStoreFence(Order))
    Chain = makeAFENCE(DAG, DL, Chain);

  return Chain;
}

SDValue BedrockTargetLowering::LowerATOMIC_LOAD_OP(
    SDValue Op, SelectionDAG &DAG, unsigned TargetOpcode) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());
  SDLoc DL(Op);
  SDValue Order =
      DAG.getTargetConstant(getBedrockMemoryOrder(Node->getMergedOrdering()),
                            DL, MVT::i8);
  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Other);
  SDValue Ops[] = {Node->getChain(), Node->getBasePtr(), Node->getVal(), Order};
  return DAG.getMemIntrinsicNode(TargetOpcode, DL, VTs, Ops,
                                 Node->getMemoryVT(), Node->getMemOperand());
}

SDValue BedrockTargetLowering::LowerATOMIC_CMP_SWAP(SDValue Op,
                                                    SelectionDAG &DAG) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());
  SDLoc DL(Op);
  SDValue Order =
      DAG.getTargetConstant(getBedrockMemoryOrder(Node->getMergedOrdering()),
                            DL, MVT::i8);
  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Other);
  SDValue Ops[] = {Node->getChain(), Node->getBasePtr(), Node->getOperand(2),
                   Node->getOperand(3), Order};
  return DAG.getMemIntrinsicNode(BedrockISD::CMPXCHG, DL, VTs, Ops,
                                 Node->getMemoryVT(), Node->getMemOperand());
}

TargetLowering::AtomicExpansionKind
BedrockTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  switch (AI->getOperation()) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::And:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
    return AtomicExpansionKind::None;
  default:
    return AtomicExpansionKind::CmpXChg;
  }
}

SDValue BedrockTargetLowering::LowerGlobalAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  auto *GA = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);
  SDValue Target = DAG.getTargetGlobalAddress(
      GA->getGlobal(), DL, MVT::i64, GA->getOffset(), GA->getTargetFlags());
  return DAG.getNode(BedrockISD::Wrapper, DL, MVT::i64, Target);
}

SDValue BedrockTargetLowering::LowerExternalSymbol(SDValue Op,
                                                   SelectionDAG &DAG) const {
  auto *ES = cast<ExternalSymbolSDNode>(Op);
  SDLoc DL(Op);
  SDValue Target = DAG.getTargetExternalSymbol(ES->getSymbol(), MVT::i64,
                                               ES->getTargetFlags());
  return DAG.getNode(BedrockISD::Wrapper, DL, MVT::i64, Target);
}

SDValue BedrockTargetLowering::LowerVASTART(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  BedrockMachineFunctionInfo *FuncInfo =
      MF.getInfo<BedrockMachineFunctionInfo>();

  SDLoc DL(Op);
  SDValue VAListPtr = Op.getOperand(1);
  SDValue VarArgs =
      DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), MVT::i64);

  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, VarArgs, VAListPtr,
                      MachinePointerInfo(SV));
}

namespace {
struct BoundCompare {
  SDValue Value;
  SDValue Bound;
  bool IsSigned = false;
  bool Inclusive = false;
};

struct BoundBranch {
  SDValue Lo;
  SDValue Value;
  SDValue Hi;
  unsigned Mode = BedrockISD::BND_II;
  bool IsSigned = false;
  bool BranchOnInside = true;
};
} // end anonymous namespace

static std::optional<bool> getIntegerSignedness(ISD::CondCode CC) {
  switch (CC) {
  default:
    return std::nullopt;
  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETGT:
  case ISD::SETGE:
    return true;
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETUGT:
  case ISD::SETUGE:
    return false;
  }
}

static unsigned getBndMode(bool LowInclusive, bool HighInclusive) {
  if (LowInclusive && HighInclusive)
    return BedrockISD::BND_II;
  if (LowInclusive && !HighInclusive)
    return BedrockISD::BND_IX;
  if (!LowInclusive && HighInclusive)
    return BedrockISD::BND_XI;
  return BedrockISD::BND_XX;
}

static bool isMaterializedForFreeInBnd(SDValue V) {
  return !isa<ConstantSDNode>(V);
}

static void collectInsideLowerBounds(SDValue Cmp,
                                     SmallVectorImpl<BoundCompare> &Out) {
  if (Cmp.getOpcode() != ISD::SETCC)
    return;
  auto *CCNode = dyn_cast<CondCodeSDNode>(Cmp.getOperand(2));
  if (!CCNode)
    return;
  std::optional<bool> Signed = getIntegerSignedness(CCNode->get());
  if (!Signed)
    return;

  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  switch (CCNode->get()) {
  default:
    return;
  case ISD::SETGE:
  case ISD::SETUGE:
    Out.push_back({LHS, RHS, *Signed, true});
    return;
  case ISD::SETGT:
  case ISD::SETUGT:
    Out.push_back({LHS, RHS, *Signed, false});
    return;
  case ISD::SETLE:
  case ISD::SETULE:
    Out.push_back({RHS, LHS, *Signed, true});
    return;
  case ISD::SETLT:
  case ISD::SETULT:
    Out.push_back({RHS, LHS, *Signed, false});
    return;
  }
}

static void collectInsideUpperBounds(SDValue Cmp,
                                     SmallVectorImpl<BoundCompare> &Out) {
  if (Cmp.getOpcode() != ISD::SETCC)
    return;
  auto *CCNode = dyn_cast<CondCodeSDNode>(Cmp.getOperand(2));
  if (!CCNode)
    return;
  std::optional<bool> Signed = getIntegerSignedness(CCNode->get());
  if (!Signed)
    return;

  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  switch (CCNode->get()) {
  default:
    return;
  case ISD::SETLE:
  case ISD::SETULE:
    Out.push_back({LHS, RHS, *Signed, true});
    return;
  case ISD::SETLT:
  case ISD::SETULT:
    Out.push_back({LHS, RHS, *Signed, false});
    return;
  case ISD::SETGE:
  case ISD::SETUGE:
    Out.push_back({RHS, LHS, *Signed, true});
    return;
  case ISD::SETGT:
  case ISD::SETUGT:
    Out.push_back({RHS, LHS, *Signed, false});
    return;
  }
}

static void collectOutsideLowerBounds(SDValue Cmp,
                                      SmallVectorImpl<BoundCompare> &Out) {
  if (Cmp.getOpcode() != ISD::SETCC)
    return;
  auto *CCNode = dyn_cast<CondCodeSDNode>(Cmp.getOperand(2));
  if (!CCNode)
    return;
  std::optional<bool> Signed = getIntegerSignedness(CCNode->get());
  if (!Signed)
    return;

  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  switch (CCNode->get()) {
  default:
    return;
  case ISD::SETLT:
  case ISD::SETULT:
    Out.push_back({LHS, RHS, *Signed, true});
    return;
  case ISD::SETLE:
  case ISD::SETULE:
    Out.push_back({LHS, RHS, *Signed, false});
    return;
  case ISD::SETGT:
  case ISD::SETUGT:
    Out.push_back({RHS, LHS, *Signed, true});
    return;
  case ISD::SETGE:
  case ISD::SETUGE:
    Out.push_back({RHS, LHS, *Signed, false});
    return;
  }
}

static void collectOutsideUpperBounds(SDValue Cmp,
                                      SmallVectorImpl<BoundCompare> &Out) {
  if (Cmp.getOpcode() != ISD::SETCC)
    return;
  auto *CCNode = dyn_cast<CondCodeSDNode>(Cmp.getOperand(2));
  if (!CCNode)
    return;
  std::optional<bool> Signed = getIntegerSignedness(CCNode->get());
  if (!Signed)
    return;

  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  switch (CCNode->get()) {
  default:
    return;
  case ISD::SETGT:
  case ISD::SETUGT:
    Out.push_back({LHS, RHS, *Signed, true});
    return;
  case ISD::SETGE:
  case ISD::SETUGE:
    Out.push_back({LHS, RHS, *Signed, false});
    return;
  case ISD::SETLT:
  case ISD::SETULT:
    Out.push_back({RHS, LHS, *Signed, true});
    return;
  case ISD::SETLE:
  case ISD::SETULE:
    Out.push_back({RHS, LHS, *Signed, false});
    return;
  }
}

static bool makeBoundBranch(ArrayRef<BoundCompare> Lows,
                            ArrayRef<BoundCompare> Highs,
                            bool BranchOnInside, BoundBranch &Out) {
  for (const BoundCompare &Low : Lows) {
    for (const BoundCompare &High : Highs) {
      if (Low.Value != High.Value || Low.IsSigned != High.IsSigned)
        continue;
      if (Low.Value.getValueType() != High.Value.getValueType())
        continue;
      if (!isMaterializedForFreeInBnd(Low.Bound) ||
          !isMaterializedForFreeInBnd(Low.Value) ||
          !isMaterializedForFreeInBnd(High.Bound))
        continue;
      Out.Lo = Low.Bound;
      Out.Value = Low.Value;
      Out.Hi = High.Bound;
      Out.Mode = getBndMode(Low.Inclusive, High.Inclusive);
      Out.IsSigned = Low.IsSigned;
      Out.BranchOnInside = BranchOnInside;
      return true;
    }
  }
  return false;
}

static bool matchBoundBranch(SDValue Cond, BoundBranch &Out) {
  if (!Cond.hasOneUse())
    return false;

  if (Cond.getOpcode() == ISD::AND) {
    SmallVector<BoundCompare, 2> Lows;
    SmallVector<BoundCompare, 2> Highs;
    collectInsideLowerBounds(Cond.getOperand(0), Lows);
    collectInsideUpperBounds(Cond.getOperand(1), Highs);
    if (makeBoundBranch(Lows, Highs, true, Out))
      return true;
    Lows.clear();
    Highs.clear();
    collectInsideLowerBounds(Cond.getOperand(1), Lows);
    collectInsideUpperBounds(Cond.getOperand(0), Highs);
    return makeBoundBranch(Lows, Highs, true, Out);
  }

  if (Cond.getOpcode() == ISD::OR) {
    SmallVector<BoundCompare, 2> Lows;
    SmallVector<BoundCompare, 2> Highs;
    collectOutsideLowerBounds(Cond.getOperand(0), Lows);
    collectOutsideUpperBounds(Cond.getOperand(1), Highs);
    if (makeBoundBranch(Lows, Highs, false, Out))
      return true;
    Lows.clear();
    Highs.clear();
    collectOutsideLowerBounds(Cond.getOperand(1), Lows);
    collectOutsideUpperBounds(Cond.getOperand(0), Highs);
    return makeBoundBranch(Lows, Highs, false, Out);
  }

  return false;
}

static bool matchPowerOfTwoAnd(SDValue Op, SDValue &Src, unsigned &Index) {
  if (Op.getOpcode() != ISD::AND)
    return false;

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  auto *C = dyn_cast<ConstantSDNode>(RHS);
  if (!C) {
    C = dyn_cast<ConstantSDNode>(LHS);
    if (!C)
      return false;
    Src = RHS;
  } else {
    Src = LHS;
  }

  APInt Mask = C->getAPIntValue();
  unsigned Width = Op.getValueSizeInBits();
  if (Mask.getBitWidth() != Width)
    Mask = Mask.trunc(Width);
  if (!Mask.isPowerOf2())
    return false;

  Index = Mask.logBase2();
  return Index < Width && Index < 64;
}

static bool shouldUseO1Combines(const TargetMachine &TM,
                                const SelectionDAG &DAG) {
  return TM.getOptLevel() != CodeGenOptLevel::None &&
         !DAG.getMachineFunction().getFunction().hasOptNone();
}

SDValue BedrockTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  SDValue TargetCC;
  if (shouldUseO1Combines(getTargetMachine(), DAG) &&
      (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    if (auto *C = dyn_cast<ConstantSDNode>(RHS); C && C->isZero()) {
      SDValue Tested;
      unsigned Index = 0;
      if (matchPowerOfTwoAnd(LHS, Tested, Index)) {
        SDValue Glue = DAG.getNode(
            BedrockISD::BTEST, DL, MVT::Glue, Tested,
            DAG.getTargetConstant(Index, DL, MVT::i8));
        TargetCC = DAG.getConstant(CC == ISD::SETEQ ? BedrockCC::EQ
                                                    : BedrockCC::NE,
                                   DL, MVT::i8);
        return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain,
                           Dest, TargetCC, Glue);
      }
    }
  }

  SDValue Glue = lowerBedrockCompare(LHS, RHS, CC, DL, DAG, TargetCC);

  return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain, Dest,
                     TargetCC, Glue);
}

SDValue BedrockTargetLowering::LowerBRCOND(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);
  SDLoc DL(Op);

  if (shouldUseO1Combines(getTargetMachine(), DAG)) {
    BoundBranch Bnd;
    if (matchBoundBranch(Cond, Bnd)) {
      SDValue Glue = DAG.getNode(
          BedrockISD::BND, DL, MVT::Glue, Bnd.Lo, Bnd.Value, Bnd.Hi,
          DAG.getTargetConstant(Bnd.Mode, DL, MVT::i8),
          DAG.getTargetConstant(Bnd.IsSigned ? 1 : 0, DL, MVT::i8));
      SDValue TargetCC =
          DAG.getConstant(Bnd.BranchOnInside ? BedrockCC::VC : BedrockCC::VS,
                          DL, MVT::i8);
      return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain, Dest,
                         TargetCC, Glue);
    }

    SDValue Tested;
    unsigned Index = 0;
    if (matchPowerOfTwoAnd(Cond, Tested, Index)) {
      SDValue Glue = DAG.getNode(
          BedrockISD::BTEST, DL, MVT::Glue, Tested,
          DAG.getTargetConstant(Index, DL, MVT::i8));
      SDValue TargetCC = DAG.getConstant(BedrockCC::NE, DL, MVT::i8);
      return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain, Dest,
                         TargetCC, Glue);
    }
  }

  SDValue Zero = DAG.getConstant(0, DL, Cond.getValueType());
  SDValue TargetCC;
  SDValue Glue =
      lowerBedrockCompare(Cond, Zero, ISD::SETNE, DL, DAG, TargetCC);
  return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain, Dest,
                     TargetCC, Glue);
}

SDValue BedrockTargetLowering::LowerSELECT_CC(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  if (CC == ISD::SETLT && isIntegerConstant(RHS, 0) &&
      LHS.getValueType().isInteger() && TrueV.getValueType().isInteger() &&
      LHS.getValueSizeInBits() < TrueV.getValueSizeInBits()) {
    unsigned SrcBits = LHS.getValueSizeInBits();
    if (SrcBits < 63 && isZeroExtendOf(FalseV, LHS) &&
        isSignMaskOrOf(TrueV, FalseV, SrcBits))
      return DAG.getNode(ISD::SIGN_EXTEND, DL, TrueV.getValueType(), LHS);
  }

  SDValue TargetCC;
  SDValue Glue = lowerBedrockCompare(LHS, RHS, CC, DL, DAG, TargetCC);

  return DAG.getNode(BedrockISD::SELECT_CC, DL, Op.getValueType(), TrueV,
                     FalseV, TargetCC, Glue);
}

SDValue BedrockTargetLowering::LowerSIGN_EXTEND_INREG(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  EVT VT = Op.getValueType();
  EVT ExtVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
  unsigned VTBits = VT.getSizeInBits();
  unsigned ExtBits = ExtVT.getSizeInBits();

  if (ExtBits >= VTBits)
    return Src;

  if ((ExtBits == 8 || ExtBits == 16 || ExtBits == 32) && ExtBits < VTBits) {
    SDValue Narrow = DAG.getNode(ISD::TRUNCATE, DL, ExtVT, Src);
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VT, Narrow);
  }

  SDValue ShAmt = DAG.getConstant(VTBits - ExtBits, DL, MVT::i8);
  return DAG.getNode(ISD::SRA, DL, VT,
                     DAG.getNode(ISD::SHL, DL, VT, Src, ShAmt), ShAmt);
}

MachineBasicBlock *BedrockTargetLowering::EmitInstrWithCustomInserter(
    MachineInstr &MI, MachineBasicBlock *BB) const {
  unsigned Opc = MI.getOpcode();
  assert((Opc == Bedrock::SELECT8 || Opc == Bedrock::SELECT16 ||
          Opc == Bedrock::SELECT32 || Opc == Bedrock::SELECT64 ||
          Opc == Bedrock::FSELECT32 || Opc == Bedrock::FSELECT64) &&
         "unexpected Bedrock custom-inserter instruction");

  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  if (BB->getParent()->getTarget().getOptLevel() < CodeGenOptLevel::Default) {
    const BasicBlock *LLVMBB = BB->getBasicBlock();
    MachineFunction *MF = BB->getParent();
    MachineFunction::iterator InsertPt = ++BB->getIterator();

    MachineBasicBlock *ThisMBB = BB;
    MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVMBB);
    MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
    MF->insert(InsertPt, FalseMBB);
    MF->insert(InsertPt, SinkMBB);

    SinkMBB->splice(SinkMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

    BB->addSuccessor(FalseMBB);
    BB->addSuccessor(SinkMBB);
    FalseMBB->addSuccessor(SinkMBB);

    BuildMI(BB, DL, TII.get(Bedrock::JCC))
        .addMBB(SinkMBB)
        .addImm(MI.getOperand(3).getImm());

    BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(Bedrock::PHI),
            MI.getOperand(0).getReg())
        .addReg(MI.getOperand(2).getReg())
        .addMBB(FalseMBB)
        .addReg(MI.getOperand(1).getReg())
        .addMBB(ThisMBB);

    MI.eraseFromParent();
    return SinkMBB;
  }

  Register Dst = MI.getOperand(0).getReg();
  Register TrueReg = MI.getOperand(1).getReg();
  Register FalseReg = MI.getOperand(2).getReg();
  unsigned Cond = MI.getOperand(3).getImm();
  MachineBasicBlock::iterator Insert = MI.getIterator();

  BuildMI(*BB, Insert, DL, TII.get(movccOpcodeForSelect(Opc)), Dst)
      .addReg(FalseReg)
      .addReg(TrueReg)
      .addImm(Cond);
  MI.eraseFromParent();
  return BB;
}

SDValue BedrockTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Bedrock);

  if (IsVarArg) {
    int FI = MFI.CreateFixedObject(/*Size=*/1, CCInfo.getStackSize() + 8,
                                   /*IsImmutable=*/true);
    MF.getInfo<BedrockMachineFunctionInfo>()->setVarArgsFrameIndex(FI);
  }

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    if (VA.isRegLoc()) {
      MVT LocVT = VA.getLocVT();
      Register VReg = MF.addLiveIn(VA.getLocReg(),
                                   getRegClassForLoc(VA.getLocReg(), LocVT));
      SDValue Val = DAG.getCopyFromReg(Chain, DL, VReg, LocVT);
      InVals.push_back(convertValVT(VA, Val, DL, DAG));
      continue;
    }

    assert(VA.isMemLoc() && "unknown argument location");
    unsigned ObjSize = VA.getLocVT().getStoreSize();
    int FI = MFI.CreateFixedObject(ObjSize, VA.getLocMemOffset() + 8,
                                   /*IsImmutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i64);
    SDValue Val = DAG.getLoad(VA.getLocVT(), DL, Chain, FIN,
                              MachinePointerInfo::getFixedStack(MF, FI));
    InVals.push_back(convertValVT(VA, Val, DL, DAG));
  }

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    if (!Ins[I].Flags.isSRet())
      continue;

    BedrockMachineFunctionInfo *FuncInfo =
        MF.getInfo<BedrockMachineFunctionInfo>();
    Register Reg = FuncInfo->getSRetReturnReg();
    if (!Reg) {
      Reg = MF.getRegInfo().createVirtualRegister(&Bedrock::PTR64RegClass);
      FuncInfo->setSRetReturnReg(Reg);
    }

    SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), DL, Reg, InVals[I]);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Copy, Chain);
  }

  return Chain;
}

SDValue
BedrockTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                 SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool IsVarArg = CLI.IsVarArg;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  MachineFunction &MF = DAG.getMachineFunction();

  if (IsTailCall)
    IsTailCall = false;
  if (CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("failed to perform tail call elimination on a call "
                       "site marked musttail");

  SmallVector<CCValAssign, 16> ArgLocs;
  bool UseVarArgCC = IsVarArg && CLI.NumFixedArgs != 0;
  CCState CCInfo(CallConv, UseVarArgCC, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_Bedrock);

  unsigned NumBytes = alignTo(CCInfo.getStackSize(), Align(16)) + 8;
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue Arg = convertLocVT(VA, CLI.OutVals[I], DL, DAG);

    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
      continue;
    }

    assert(VA.isMemLoc() && "unknown call argument location");
    if (!StackPtr)
      StackPtr = DAG.getCopyFromReg(Chain, DL, Bedrock::SP, MVT::i64);

    SDValue PtrOff = StackPtr;
    if (VA.getLocMemOffset() != 0)
      PtrOff = DAG.getNode(ISD::ADD, DL, MVT::i64, StackPtr,
                           DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));

    MemOpChains.push_back(
        DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo()));
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue InGlue;
  for (auto &[Reg, Val] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Val, InGlue);
    InGlue = Chain.getValue(1);
  }

  bool UseShortCall = false;
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    UseShortCall = true;
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i64,
                                        G->getOffset(), G->getTargetFlags());
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i64,
                                         E->getTargetFlags());
  }

  SmallVector<SDValue, 12> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
  assert(Mask && "missing Bedrock call preserved mask");
  Ops.push_back(DAG.getRegisterMask(Mask));

  for (auto &[Reg, Val] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, Val.getValueType()));
  if (InGlue)
    Ops.push_back(InGlue);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(UseShortCall ? BedrockISD::CALL16 : BedrockISD::CALL, DL,
                      NodeTys, Ops);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, DL);
  InGlue = Chain.getValue(1);

  return LowerCallResult(Chain, InGlue, CallConv, IsVarArg, CLI.Ins, DL, DAG,
                         InVals);
}

SDValue BedrockTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 8> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC_Bedrock);

  for (CCValAssign &VA : RVLocs) {
    SDValue Val =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), InGlue);
    Chain = Val.getValue(1);
    InGlue = Val.getValue(2);
    InVals.push_back(convertValVT(VA, Val, DL, DAG));
  }
  return Chain;
}

bool BedrockTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 8> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_Bedrock);
}

SDValue
BedrockTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                   bool IsVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   const SmallVectorImpl<SDValue> &OutVals,
                                   const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 8> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_Bedrock);

  SDValue Glue;
  SmallVector<SDValue, 8> RetOps(1, Chain);

  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    CCValAssign &VA = RVLocs[I];
    SDValue Val = convertLocVT(VA, OutVals[I], DL, DAG);
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  MachineFunction &MF = DAG.getMachineFunction();
  BedrockMachineFunctionInfo *FuncInfo =
      MF.getInfo<BedrockMachineFunctionInfo>();
  if (Register SRetReg = FuncInfo->getSRetReturnReg()) {
    SDValue Val = DAG.getCopyFromReg(Chain, DL, SRetReg, MVT::i64);
    Chain = DAG.getCopyToReg(Chain, DL, Bedrock::A0, Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(Bedrock::A0, MVT::i64));
  }

  RetOps[0] = Chain;
  if (Glue)
    RetOps.push_back(Glue);
  return DAG.getNode(BedrockISD::RET_GLUE, DL, MVT::Other, RetOps);
}
