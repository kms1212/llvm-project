//===-- BedrockFrameLowering.cpp - Bedrock frame lowering -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFrameLowering.h"
#include "BedrockMachineFunctionInfo.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static bool needsStackRealignment(const MachineFunction &MF) {
  // The persistent Bedrock C frame is always kept at 16-byte alignment even
  // though transient near-call padding makes the generic call-frame quantum
  // eight bytes.
  return MF.getFrameInfo().getMaxAlign() > Align(16);
}

static bool needsDwarfCFI(const MachineFunction &MF) {
  // Runtime unwind tables are function properties, while -g requests the
  // non-allocating .debug_frame through a module compile unit.  Do not use
  // needsFrameMoves() alone: llc may force frame moves for textual assembly
  // even when neither output is requested.
  const Function &F = MF.getFunction();
  return F.hasUWTable() || MF.getTarget().Options.ForceDwarfFrameSection ||
         !F.getParent()->debug_compile_units().empty();
}

static bool isCalleeSavedFPR(Register Reg) {
  return Reg >= Bedrock::F8 && Reg <= Bedrock::F15;
}

static void getFPPairRegs(unsigned PairIndex, Register &First,
                          Register &Second) {
  switch (PairIndex) {
  case 0:
    First = Bedrock::F14;
    Second = Bedrock::F15;
    return;
  case 1:
    First = Bedrock::F12;
    Second = Bedrock::F13;
    return;
  case 2:
    First = Bedrock::F10;
    Second = Bedrock::F11;
    return;
  case 3:
    First = Bedrock::F8;
    Second = Bedrock::F9;
    return;
  default:
    llvm_unreachable("invalid Bedrock floating-point pair index");
  }
}

static unsigned getSavedFPPairMask(const MachineFrameInfo &MFI) {
  unsigned Mask = 0;
  for (unsigned PairIndex = 0; PairIndex != 4; ++PairIndex) {
    Register First;
    Register Second;
    getFPPairRegs(PairIndex, First, Second);
    bool SavesFirst = false;
    bool SavesSecond = false;
    for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo()) {
      SavesFirst |= Register(Info.getReg()) == First;
      SavesSecond |= Register(Info.getReg()) == Second;
    }
    assert(SavesFirst == SavesSecond &&
           "Bedrock floating-point callee saves must be paired");
    if (SavesFirst)
      Mask |= 1u << PairIndex;
  }
  return Mask;
}

static uint64_t getFPPairStackSize(unsigned PairMask) {
  uint64_t Size = 0;
  for (unsigned PairIndex = 0; PairIndex != 4; ++PairIndex)
    if (PairMask & (1u << PairIndex))
      Size += 16;
  return Size;
}

BedrockFrameLowering::BedrockFrameLowering(const BedrockSubtarget &STI)
    // Generic call-frame tracking rounds adjustments to this value.  Near
    // calls need an exact eight-byte phase adjustment; emitPrologue still
    // rounds every persistent function body frame to the ABI's 16 bytes.
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(8), 0,
                          Align(8)) {}

void BedrockFrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  const int64_t EntryFrameSize = 8;
  const bool NeedsFrameMoves = needsDwarfCFI(MF);

  auto emitCFI = [&](MCCFIInstruction CFI) {
    if (!NeedsFrameMoves)
      return;
    unsigned CFIIndex = MF.addFrameInst(CFI);
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  };

  // CALL places the return PC at entry SP. Define the caller's CFA before
  // describing any persistent body-frame allocation.
  emitCFI(MCCFIInstruction::cfiDefCfa(
      nullptr, TRI.getDwarfRegNum(Bedrock::SP, true), EntryFrameSize));
  emitCFI(MCCFIInstruction::createOffset(
      nullptr, TRI.getDwarfRegNum(Bedrock::PC, true), -EntryFrameSize));
  uint64_t StackSize = MFI.getStackSize();
  // A function observes a 16-byte-aligned SP on entry and keeps its body
  // frame aligned.  Near-call padding and outgoing arguments are allocated
  // dynamically around each call rather than being folded into this frame.
  StackSize = alignTo(StackSize, Align(16));
  MFI.setStackSize(StackSize);
  if (StackSize == 0)
    return;

  const unsigned FPPairMask = getSavedFPPairMask(MFI);
  const uint64_t FPPairStackSize = getFPPairStackSize(FPPairMask);
  assert(StackSize >= FPPairStackSize &&
         "floating-point save area exceeds the Bedrock frame");
  const uint64_t RegularStackSize = StackSize - FPPairStackSize;
  const auto *BFI = MF.getInfo<BedrockMachineFunctionInfo>();

  uint64_t PushedFPBytes = 0;
  for (unsigned PairIndex = 0; PairIndex != 4; ++PairIndex) {
    if (!(FPPairMask & (1u << PairIndex)))
      continue;
    Register First;
    Register Second;
    getFPPairRegs(PairIndex, First, Second);
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::FPUSHPi))
        .addImm(PairIndex)
        .addReg(First, RegState::Implicit)
        .addReg(Second, RegState::Implicit)
        .setMIFlag(MachineInstr::FrameSetup);
    PushedFPBytes += 16;
    emitCFI(MCCFIInstruction::cfiDefCfaOffset(
        nullptr, PushedFPBytes + EntryFrameSize));
    for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo()) {
      if (Register(Info.getReg()) != First &&
          Register(Info.getReg()) != Second)
        continue;
      int64_t Offset =
          MFI.getObjectOffset(Info.getFrameIdx()) - EntryFrameSize;
      emitCFI(MCCFIInstruction::createOffset(
          nullptr, TRI.getDwarfRegNum(Info.getReg(), true), Offset));
    }
  }

  if (RegularStackSize != 0) {
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_DOWN))
        .addImm(RegularStackSize)
        .setMIFlag(MachineInstr::FrameSetup);
    emitCFI(MCCFIInstruction::cfiDefCfaOffset(
        nullptr, StackSize + EntryFrameSize));
  }

  const auto &CSI = MFI.getCalleeSavedInfo();
  std::advance(MBBI, llvm::count_if(CSI, [](const CalleeSavedInfo &Info) {
                 return !isCalleeSavedFPR(Info.getReg());
               }));
  for (const CalleeSavedInfo &Info : CSI) {
    if (isCalleeSavedFPR(Info.getReg()))
      continue;
    int64_t Offset = MFI.getObjectOffset(Info.getFrameIdx()) - EntryFrameSize;
    emitCFI(MCCFIInstruction::createOffset(
        nullptr, TRI.getDwarfRegNum(Info.getReg(), true), Offset));
  }

  if (BFI->hasFStatusFrameIndex()) {
    // The dedicated FSTATUS spill follows the ordinary GPR spills. R15 has
    // been forced into the callee-save list and is therefore available as a
    // temporary once its incoming value has reached the stack.
    std::advance(MBBI, 2);
    int64_t Offset =
        MFI.getObjectOffset(BFI->getFStatusFrameIndex()) - EntryFrameSize;
    emitCFI(MCCFIInstruction::createOffset(
        nullptr, TRI.getDwarfRegNum(Bedrock::FSTATUS, true), Offset));
  }

  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::MOVQsr), Bedrock::R15)
        .setMIFlag(MachineInstr::FrameSetup);
    emitCFI(MCCFIInstruction::cfiDefCfa(nullptr,
                                        TRI.getDwarfRegNum(Bedrock::R15, true),
                                        StackSize + EntryFrameSize));

    if (needsStackRealignment(MF)) {
      BuildMI(MBB, MBBI, DL, TII.get(Bedrock::MOVQsr), Bedrock::R14)
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ANDQ3ri), Bedrock::R14)
          .addReg(Bedrock::R14)
          .addImm(-int64_t(MFI.getMaxAlign().value()))
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII.get(Bedrock::MOVQrs))
          .addReg(Bedrock::R14)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }
}

void BedrockFrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  const int64_t EntryFrameSize = 8;
  const unsigned FPPairMask = getSavedFPPairMask(MFI);
  const uint64_t FPPairStackSize = getFPPairStackSize(FPPairMask);
  assert(StackSize >= FPPairStackSize &&
         "floating-point save area exceeds the Bedrock frame");
  const uint64_t RegularStackSize = StackSize - FPPairStackSize;
  const auto *BFI = MF.getInfo<BedrockMachineFunctionInfo>();

  if (hasFP(MF)) {
    MachineBasicBlock::iterator RestoreI = MBBI;
    const auto &CSI = MFI.getCalleeSavedInfo();
    size_t RestoreCount = llvm::count_if(CSI, [](const CalleeSavedInfo &Info) {
      return !isCalleeSavedFPR(Info.getReg());
    });
    if (BFI->hasFStatusFrameIndex())
      RestoreCount += needsDwarfCFI(MF) ? 3 : 2;
    for (size_t I = 0; I != RestoreCount && RestoreI != MBB.begin(); ++I)
      --RestoreI;
    BuildMI(MBB, RestoreI, DL, TII.get(Bedrock::MOVQrs))
        .addReg(Bedrock::R15)
        .setMIFlag(MachineInstr::FrameDestroy);
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
    if (needsDwarfCFI(MF)) {
      unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
          nullptr, TRI.getDwarfRegNum(Bedrock::SP, true),
          StackSize + EntryFrameSize));
      BuildMI(MBB, RestoreI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameDestroy);
    }
  }

  auto emitDestroyCFI = [&](MCCFIInstruction CFI) {
    if (!needsDwarfCFI(MF))
      return;
    unsigned CFIIndex = MF.addFrameInst(CFI);
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameDestroy);
  };

  if (RegularStackSize != 0) {
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_UP))
        .addImm(RegularStackSize)
        .setMIFlag(MachineInstr::FrameDestroy);
    emitDestroyCFI(MCCFIInstruction::cfiDefCfaOffset(
        nullptr, FPPairStackSize + EntryFrameSize));
  }

  uint64_t RemainingFPBytes = FPPairStackSize;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  for (unsigned PairIndex = 4; PairIndex-- != 0;) {
    if (!(FPPairMask & (1u << PairIndex)))
      continue;
    Register First;
    Register Second;
    getFPPairRegs(PairIndex, First, Second);
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::FPOPPi))
        .addImm(PairIndex)
        .addReg(First, RegState::ImplicitDefine)
        .addReg(Second, RegState::ImplicitDefine)
        .setMIFlag(MachineInstr::FrameDestroy);
    RemainingFPBytes -= 16;
    emitDestroyCFI(MCCFIInstruction::cfiDefCfaOffset(
        nullptr, RemainingFPBytes + EntryFrameSize));
    emitDestroyCFI(MCCFIInstruction::createRestore(
        nullptr, TRI.getDwarfRegNum(First, true)));
    emitDestroyCFI(MCCFIInstruction::createRestore(
        nullptr, TRI.getDwarfRegNum(Second, true)));
  }
}

void BedrockFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                BitVector &SavedRegs,
                                                RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  auto *BFI = MF.getInfo<BedrockMachineFunctionInfo>();
  if (MF.getRegInfo().isPhysRegModified(Bedrock::FSTATUS) &&
      !BFI->hasFStatusFrameIndex()) {
    int FrameIndex = MF.getFrameInfo().CreateStackObject(8, Align(8), true);
    BFI->setFStatusFrameIndex(FrameIndex);
  }
  if (BFI->hasFStatusFrameIndex()) {
    // FSTATUS can only move through a GPR. Preserve R15 first, then use it as
    // a post-RA scratch register for the dedicated status spill and reload.
    SavedRegs.set(Bedrock::R15);
  }
  if (hasFP(MF))
    SavedRegs.set(Bedrock::R15);
  if (needsStackRealignment(MF))
    SavedRegs.set(Bedrock::R14);

  for (unsigned PairIndex = 0; PairIndex != 4; ++PairIndex) {
    Register First;
    Register Second;
    getFPPairRegs(PairIndex, First, Second);
    if (SavedRegs.test(First) || SavedRegs.test(Second)) {
      SavedRegs.set(First);
      SavedRegs.set(Second);
    }
  }
}

bool BedrockFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  std::vector<CalleeSavedInfo> Ordered;
  Ordered.reserve(CSI.size());
  for (unsigned PairIndex = 0; PairIndex != 4; ++PairIndex) {
    Register First;
    Register Second;
    getFPPairRegs(PairIndex, First, Second);
    for (Register Reg : {First, Second}) {
      auto I = llvm::find_if(CSI, [Reg](const CalleeSavedInfo &Info) {
        return Register(Info.getReg()) == Reg;
      });
      if (I != CSI.end())
        Ordered.push_back(*I);
    }
  }
  for (const CalleeSavedInfo &Info : CSI)
    if (!isCalleeSavedFPR(Info.getReg()))
      Ordered.push_back(Info);
  CSI = std::move(Ordered);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  for (CalleeSavedInfo &Info : CSI) {
    const TargetRegisterClass *RC =
        TRI->getMinimalPhysRegClass(Info.getReg());
    unsigned Size = TRI->getSpillSize(*RC);
    Align Alignment = std::min(TRI->getSpillAlign(*RC), getStackAlign());
    int FrameIndex = MFI.CreateStackObject(Size, Alignment, true);
    MFI.setIsCalleeSavedObjectIndex(FrameIndex, true);
    Info.setFrameIdx(FrameIndex);
  }
  return true;
}

bool BedrockFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo *TII = MBB.getParent()->getSubtarget().getInstrInfo();
  for (const CalleeSavedInfo &Info : CSI)
    if (!isCalleeSavedFPR(Info.getReg()))
      spillCalleeSavedRegister(MBB, MI, Info, TII, TRI);
  const auto *BFI = MBB.getParent()->getInfo<BedrockMachineFunctionInfo>();
  if (BFI->hasFStatusFrameIndex()) {
    DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
    BuildMI(MBB, MI, DL, TII->get(Bedrock::BEDROCK_RDFSTATUS), Bedrock::R15)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MI, DL, TII->get(Bedrock::STOREQfi))
        .addReg(Bedrock::R15, RegState::Kill)
        .addFrameIndex(BFI->getFStatusFrameIndex())
        .addImm(0)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}

bool BedrockFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI,
    const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo *TII = MBB.getParent()->getSubtarget().getInstrInfo();
  const auto *BFI = MBB.getParent()->getInfo<BedrockMachineFunctionInfo>();
  if (BFI->hasFStatusFrameIndex()) {
    DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
    BuildMI(MBB, MI, DL, TII->get(Bedrock::LOADQfi), Bedrock::R15)
        .addFrameIndex(BFI->getFStatusFrameIndex())
        .addImm(0)
        .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, MI, DL, TII->get(Bedrock::BEDROCK_WRFSTATUS))
        .addReg(Bedrock::R15, RegState::Kill)
        .setMIFlag(MachineInstr::FrameDestroy);
    if (needsDwarfCFI(*MBB.getParent())) {
      unsigned CFIIndex =
          MBB.getParent()->addFrameInst(MCCFIInstruction::createRestore(
              nullptr, TRI->getDwarfRegNum(Bedrock::FSTATUS, true)));
      BuildMI(MBB, MI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameDestroy);
    }
  }
  for (const CalleeSavedInfo &Info : llvm::reverse(CSI))
    if (!isCalleeSavedFPR(Info.getReg()))
      restoreCalleeSavedRegister(MBB, MI, Info, TII, TRI);
  return true;
}

MachineBasicBlock::iterator BedrockFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  int64_t Amount = MI->getOperand(0).getImm();
  if (Amount != 0) {
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
    unsigned Opcode = MI->getOpcode() == Bedrock::ADJCALLSTACKDOWN
                          ? Bedrock::ADJSP_DOWN
                          : Bedrock::ADJSP_UP;
    BuildMI(MBB, MI, MI->getDebugLoc(), TII.get(Opcode)).addImm(Amount);
  }
  return MBB.erase(MI);
}

bool BedrockFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         needsStackRealignment(MF) || MFI.hasVarSizedObjects() ||
         MFI.isFrameAddressTaken();
}
