//===-- BedrockISelLowering.cpp - Bedrock DAG lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockISelLowering.h"
#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

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

  setLoadExtAction(ISD::EXTLOAD, MVT::i64, MVT::i32, Legal);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MVT::i32, Legal);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, MVT::i32, Legal);

  setTruncStoreAction(MVT::i32, MVT::i8, Legal);
  setTruncStoreAction(MVT::i32, MVT::i16, Legal);
  setTruncStoreAction(MVT::i64, MVT::i8, Legal);
  setTruncStoreAction(MVT::i64, MVT::i16, Legal);
  setTruncStoreAction(MVT::i64, MVT::i32, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Custom);
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::ConstantFP, VT, Expand);
    setOperationAction(ISD::FADD, VT, Legal);
    setOperationAction(ISD::FSUB, VT, Legal);
    setOperationAction(ISD::FMUL, VT, Legal);
    setOperationAction(ISD::FDIV, VT, Legal);
    setOperationAction(ISD::SINT_TO_FP, VT, Legal);
    setOperationAction(ISD::UINT_TO_FP, VT, Legal);
  }
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setMinimumJumpTableEntries(16);

  computeRegisterProperties(Subtarget.getRegisterInfo());
}

const char *BedrockTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case BedrockISD::RET_FLAG:
    return "BedrockISD::RET_FLAG";
  case BedrockISD::CALL:
    return "BedrockISD::CALL";
  case BedrockISD::CMP:
    return "BedrockISD::CMP";
  case BedrockISD::TEST:
    return "BedrockISD::TEST";
  case BedrockISD::BR_CC:
    return "BedrockISD::BR_CC";
  case BedrockISD::SET_CC:
    return "BedrockISD::SET_CC";
  case BedrockISD::SELECT_CC:
    return "BedrockISD::SELECT_CC";
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
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SETCC:
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
  default:
    llvm_unreachable("unhandled Bedrock lowering operation");
  }
}

static SDValue emitCompare(SDValue LHS, SDValue RHS, const SDLoc &DL,
                           SelectionDAG &DAG) {
  if (isNullConstant(RHS))
    return DAG.getNode(BedrockISD::TEST, DL, MVT::Glue, LHS);
  return DAG.getNode(BedrockISD::CMP, DL, MVT::Glue, LHS, RHS);
}

SDValue BedrockTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = getCondCodeOperand(Op.getOperand(1), "BR_CC");
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  SDValue TargetCC = DAG.getConstant(getBedrockCondCode(CC), DL, MVT::i32);
  SDValue Glue = emitCompare(LHS, RHS, DL, DAG);
  return DAG.getNode(BedrockISD::BR_CC, DL, Op.getValueType(), Chain, Dest,
                     TargetCC, Glue);
}

SDValue BedrockTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = getCondCodeOperand(Op.getOperand(2), "SETCC");
  SDLoc DL(Op);

  SDValue TargetCC = DAG.getConstant(getBedrockCondCode(CC), DL, MVT::i32);
  SDValue Glue = emitCompare(LHS, RHS, DL, DAG);
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
  if (FalseValue.getValueType() != VT || LHS.getValueType() != VT ||
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
  if ((CmpVT != MVT::i32 && CmpVT != MVT::i64) ||
      (VT != MVT::i32 && VT != MVT::i64))
    return SDValue();
  if (!isNonConstantRegisterValue(TrueValue) ||
      !isNonConstantRegisterValue(FalseValue))
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

MachineBasicBlock *BedrockTargetLowering::EmitInstrWithCustomInserter(
    MachineInstr &MI, MachineBasicBlock *MBB) const {
  assert((MI.getOpcode() == Bedrock::SELECT_CC_L ||
          MI.getOpcode() == Bedrock::SELECT_CC_Q) &&
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
  unsigned CmpOpcode =
      MI.getOpcode() == Bedrock::SELECT_CC_Q ? Bedrock::CMPQrr
                                             : Bedrock::CMPLrr;

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

SDValue BedrockTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (!isSupportedCallingConv(CallConv))
    report_fatal_error("Bedrock only supports C-compatible calling conventions");
  if (IsVarArg)
    report_fatal_error("Bedrock varargs lowering is not implemented yet");

  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Bedrock);

  SmallVector<SDValue, 8> ArgChains;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    if (VA.isMemLoc())
      report_fatal_error("Bedrock stack arguments are not implemented yet");

    const TargetRegisterClass *RC =
        VA.getLocVT().isFloatingPoint() ? &Bedrock::FPR64RegClass
                                        : &Bedrock::GPR64RegClass;
    Register VReg = RegInfo.createVirtualRegister(RC);
    RegInfo.addLiveIn(VA.getLocReg(), VReg);
    SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());
    ArgChains.push_back(ArgValue.getValue(ArgValue->getNumValues() - 1));
    InVals.push_back(convertLocVT(ArgValue, VA, DL, DAG));
  }

  if (!ArgChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, ArgChains);
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

  CLI.IsTailCall = false;

  if (!isSupportedCallingConv(CallConv))
    report_fatal_error("Bedrock only supports C-compatible calling conventions");
  if (IsVarArg)
    report_fatal_error("Bedrock vararg calls are not implemented yet");

  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_Bedrock);
  if (CCInfo.getStackSize() != 0)
    report_fatal_error("Bedrock stack call arguments are not implemented yet");

  Chain = DAG.getCALLSEQ_START(Chain, CCInfo.getStackSize(), 0, DL);

  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    const CCValAssign &VA = ArgLocs[I];
    if (VA.isMemLoc())
      report_fatal_error(
          "Bedrock stack call arguments are not implemented yet");
    RegsToPass.push_back(
        {VA.getLocReg(), promoteToLocVT(OutVals[I], VA, DL, DAG)});
  }

  SDValue InGlue;
  for (const auto &[Reg, Value] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Value, InGlue);
    InGlue = Chain.getValue(1);
  }

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(
        G->getGlobal(), DL, getPointerTy(DAG.getDataLayout()), G->getOffset());
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(),
                                         getPointerTy(DAG.getDataLayout()));

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

  Chain = DAG.getNode(BedrockISD::CALL, DL, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, CCInfo.getStackSize(), 0, InGlue, DL);
  InGlue = Chain.getValue(1);

  return LowerCallResult(Chain, InGlue, CallConv, IsVarArg, Ins, DL, DAG,
                         InVals);
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

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return DAG.getNode(BedrockISD::RET_FLAG, DL, MVT::Other, RetOps);
}
