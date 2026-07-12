//===-- BedrockFrameLowering.cpp - Bedrock frame lowering -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFrameLowering.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
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
  return MF.needsFrameMoves() && MF.getFunction().hasUWTable();
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
  const bool IsFar =
      MF.getFunction().getCallingConv() == CallingConv::Bedrock_Far;
  const int64_t EntryFrameSize = IsFar ? 16 : 8;
  const bool NeedsFrameMoves = needsDwarfCFI(MF);

  auto emitCFI = [&](MCCFIInstruction CFI) {
    if (!NeedsFrameMoves)
      return;
    unsigned CFIIndex = MF.addFrameInst(CFI);
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  };

  // CALL places the near PC at entry SP. LCALL places PC and CS in the
  // two-word far linkage frame. Define the caller's CFA before describing any
  // persistent body-frame allocation.
  emitCFI(MCCFIInstruction::cfiDefCfa(
      nullptr, TRI.getDwarfRegNum(Bedrock::SP, true), EntryFrameSize));
  emitCFI(MCCFIInstruction::createOffset(
      nullptr, TRI.getDwarfRegNum(Bedrock::PC, true), -EntryFrameSize));
  if (IsFar)
    emitCFI(MCCFIInstruction::createOffset(
        nullptr, TRI.getDwarfRegNum(Bedrock::CS, true), -8));

  uint64_t StackSize = MFI.getStackSize();
  // A function observes a 16-byte-aligned SP on entry and keeps its body
  // frame aligned.  Near-call padding and outgoing arguments are allocated
  // dynamically around each call rather than being folded into this frame.
  StackSize = alignTo(StackSize, Align(16));
  MFI.setStackSize(StackSize);
  if (StackSize == 0)
    return;

  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_DOWN))
      .addImm(StackSize)
      .setMIFlag(MachineInstr::FrameSetup);
  emitCFI(
      MCCFIInstruction::cfiDefCfaOffset(nullptr, StackSize + EntryFrameSize));

  const auto &CSI = MFI.getCalleeSavedInfo();
  std::advance(MBBI, CSI.size());
  for (const CalleeSavedInfo &Info : CSI) {
    int64_t Offset = MFI.getObjectOffset(Info.getFrameIdx()) - EntryFrameSize;
    emitCFI(MCCFIInstruction::createOffset(
        nullptr, TRI.getDwarfRegNum(Info.getReg(), true), Offset));
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
  const bool IsFar =
      MF.getFunction().getCallingConv() == CallingConv::Bedrock_Far;
  const int64_t EntryFrameSize = IsFar ? 16 : 8;

  if (hasFP(MF)) {
    MachineBasicBlock::iterator RestoreI = MBBI;
    const auto &CSI = MFI.getCalleeSavedInfo();
    for (size_t I = 0; I != CSI.size() && RestoreI != MBB.begin(); ++I)
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

  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_UP))
      .addImm(StackSize)
      .setMIFlag(MachineInstr::FrameDestroy);
  if (needsDwarfCFI(MF)) {
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, EntryFrameSize));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameDestroy);
  }
}

void BedrockFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                BitVector &SavedRegs,
                                                RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  if (hasFP(MF))
    SavedRegs.set(Bedrock::R15);
  if (needsStackRealignment(MF))
    SavedRegs.set(Bedrock::R14);
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
