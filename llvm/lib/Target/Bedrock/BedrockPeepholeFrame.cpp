//===-- BedrockPeepholeFrame.cpp - Bedrock peepholes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPeephole.h"

bool BedrockPeephole::foldPrologue(MachineFunction &MF) const {
  if (MF.empty())
    return false;

  MachineBasicBlock &MBB = MF.front();
  auto I = MBB.begin();
  while (I != MBB.end() && isFrameMetaInstruction(*I))
    ++I;
  if (I == MBB.end())
    return false;

  int64_t Total = 0;
  if (!isStackAdjust(*I, Bedrock::SUB64ri, Total))
    return false;
  MachineInstr &Sub = *I;

  SmallVector<MachineInstr *, 8> Stores;
  uint16_t Mask = 0;
  for (++I; I != MBB.end(); ++I) {
    if (isFrameMetaInstruction(*I))
      continue;
    Register Reg;
    int64_t Offset = 0;
    if (!isCalleeSaveStore(*I, Reg, Offset))
      break;
    std::optional<unsigned> Bit = getMaskBit(Reg);
    if (!Bit || (Mask & (uint16_t(1) << *Bit)) != 0)
      return false;
    Mask |= uint16_t(1) << *Bit;
    Stores.push_back(&*I);
  }

  unsigned MinSavedRegs = MF.getFunction().hasMinSize() ? 1 : 2;
  if (Stores.size() < MinSavedRegs || int64_t(Stores.size()) * 8 > Total)
    return false;

  auto RecomputeValidMask = [&]() -> std::optional<uint16_t> {
    uint16_t CandidateMask = 0;
    for (MachineInstr *Store : Stores) {
      Register Reg;
      int64_t Offset = 0;
      if (!isCalleeSaveStore(*Store, Reg, Offset))
        return std::nullopt;
      std::optional<unsigned> Bit = getMaskBit(Reg);
      if (!Bit || (CandidateMask & (uint16_t(1) << *Bit)) != 0)
        return std::nullopt;
      CandidateMask |= uint16_t(1) << *Bit;
    }
    if (Stores.size() < MinSavedRegs || int64_t(Stores.size()) * 8 > Total)
      return std::nullopt;
    for (MachineInstr *Store : Stores) {
      Register Reg;
      int64_t Offset = 0;
      if (!isCalleeSaveStore(*Store, Reg, Offset) ||
          !expectedStoreOffset(CandidateMask, Reg, Total, Offset))
        return std::nullopt;
    }
    return CandidateMask;
  };

  while (Stores.size() >= MinSavedRegs) {
    if (std::optional<uint16_t> CandidateMask = RecomputeValidMask()) {
      Mask = *CandidateMask;
      break;
    }
    Stores.pop_back();
  }
  if (Stores.size() < MinSavedRegs)
    return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  DebugLoc DL = Sub.getDebugLoc();
  MachineInstrBuilder Push =
      buildPushForMask(MBB, Sub.getIterator(), DL, TII, Mask);
  Push.setMIFlag(MachineInstr::FrameSetup);

  int64_t Residual = Total - int64_t(Stores.size()) * 8;
  if (Residual == 0) {
    Sub.eraseFromParent();
  } else {
    Sub.getOperand(2).setImm(Residual);
  }
  for (MachineInstr *Store : Stores)
    Store->eraseFromParent();
  return true;
}

bool BedrockPeephole::foldEpilogue(MachineBasicBlock &MBB,
                                       MachineFunction &MF) const {
  MachineBasicBlock::iterator RetI = MBB.getLastNonDebugInstr();
  if (RetI == MBB.end() || RetI->getOpcode() != Bedrock::RET)
    return false;
  MachineInstr *Ret = &*RetI;

  MachineInstr *Add = nullptr;
  for (auto I = Ret->getIterator(); I != MBB.begin();) {
    --I;
    if (isFrameMetaInstruction(*I))
      continue;
    Add = &*I;
    break;
  }
  if (!Add)
    return false;

  int64_t Total = 0;
  if (!isStackAdjust(*Add, Bedrock::ADD64ri, Total))
    return false;

  SmallVector<MachineInstr *, 8> Loads;
  uint16_t Mask = 0;
  for (auto I = Add->getIterator(); I != MBB.begin();) {
    --I;
    if (isFrameMetaInstruction(*I))
      continue;
    Register Reg;
    int64_t Offset = 0;
    if (!isCalleeSaveLoad(*I, Reg, Offset))
      break;
    std::optional<unsigned> Bit = getMaskBit(Reg);
    if (!Bit || (Mask & (uint16_t(1) << *Bit)) != 0)
      return false;
    Mask |= uint16_t(1) << *Bit;
    Loads.push_back(&*I);
  }

  unsigned MinSavedRegs = MF.getFunction().hasMinSize() ? 1 : 2;
  if (Loads.size() < MinSavedRegs || int64_t(Loads.size()) * 8 > Total)
    return false;
  for (MachineInstr *Load : Loads) {
    Register Reg;
    int64_t Offset = 0;
    if (!isCalleeSaveLoad(*Load, Reg, Offset) ||
        !expectedStoreOffset(Mask, Reg, Total, Offset))
      return false;
  }

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  DebugLoc DL = Add->getDebugLoc();
  int64_t Residual = Total - int64_t(Loads.size()) * 8;
  if (Residual == 0) {
    MachineInstrBuilder Pop =
        buildPopForMask(MBB, Add->getIterator(), DL, TII, Mask);
    Pop.setMIFlag(MachineInstr::FrameDestroy);
    Add->eraseFromParent();
  } else {
    Add->getOperand(2).setImm(Residual);
    MachineInstrBuilder Pop =
        buildPopForMask(MBB, std::next(Add->getIterator()), DL, TII, Mask);
    Pop.setMIFlag(MachineInstr::FrameDestroy);
  }

  for (MachineInstr *Load : Loads)
    Load->eraseFromParent();
  return true;
}

bool BedrockPeephole::foldDeadFrameTopPadding(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.hasVarSizedObjects() || MFI.hasOpaqueSPAdjustment())
    return false;

  MachineInstr *Sub = nullptr;
  SmallVector<MachineInstr *, 4> Adds;
  int64_t Amount = 0;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      int64_t Adjust = 0;
      if (isStackAdjust(MI, Bedrock::SUB64ri, Adjust)) {
        if (!MI.getFlag(MachineInstr::FrameSetup) || Sub ||
            MI.getParent() != &MF.front())
          return false;
        Sub = &MI;
        Amount = Adjust;
        continue;
      }
      if (isStackAdjust(MI, Bedrock::ADD64ri, Adjust)) {
        if (!MI.getFlag(MachineInstr::FrameDestroy))
          return false;
        Adds.push_back(&MI);
        continue;
      }
    }
  }

  if (!Sub || Adds.empty() || Amount <= 0)
    return false;

  SmallPtrSet<MachineInstr *, 8> Ignored;
  Ignored.insert(Sub);
  for (MachineInstr *Add : Adds) {
    int64_t AddAmount = 0;
    if (!isStackAdjust(*Add, Bedrock::ADD64ri, AddAmount) ||
        AddAmount != Amount)
      return false;
    Ignored.insert(Add);
  }

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint64_t MaxSPMemEnd = 0;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || Ignored.contains(&MI))
        continue;

      unsigned AccessSize = memSizeForOpcode(MI.getOpcode());
      for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
        const MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || !regsOverlap(TRI, MO.getReg(), Bedrock::SP))
          continue;
        if (MO.isImplicit())
          continue;
        if (!isMemoryBaseOperand(MI, I, Bedrock::SP, TRI))
          return false;
        if (AccessSize == 0)
          return false;
        int64_t Offset = MI.getOperand(I + 1).getImm();
        if (Offset < 0)
          return false;
        MaxSPMemEnd =
            std::max(MaxSPMemEnd, uint64_t(Offset) + uint64_t(AccessSize));
      }
    }
  }

  uint64_t Needed = std::max<uint64_t>(MaxSPMemEnd, MFI.getMaxCallFrameSize());
  Align StackAlign = Align(16);
  if (const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering())
    StackAlign = TFI->getStackAlign();
  if (MFI.hasCalls())
    Needed = alignTo(Needed + 8, StackAlign) - 8;
  else
    Needed = alignTo(Needed, StackAlign);

  if (Needed >= uint64_t(Amount))
    return false;

  if (Needed == 0) {
    Sub->eraseFromParent();
    for (MachineInstr *Add : Adds)
      Add->eraseFromParent();
    return true;
  }

  Sub->getOperand(2).setImm(Needed);
  for (MachineInstr *Add : Adds)
    Add->getOperand(2).setImm(Needed);
  return true;
}

bool BedrockPeephole::foldDeadStackAdjust(MachineFunction &MF) const {
  SmallVector<MachineInstr *, 2> Subs;
  SmallVector<MachineInstr *, 4> Adds;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      int64_t Amount = 0;
      if (isStackAdjust(MI, Bedrock::SUB64ri, Amount) &&
          MI.getFlag(MachineInstr::FrameSetup)) {
        Subs.push_back(&MI);
        continue;
      }
      if (isStackAdjust(MI, Bedrock::ADD64ri, Amount) &&
          MI.getFlag(MachineInstr::FrameDestroy)) {
        Adds.push_back(&MI);
        continue;
      }
    }
  }

  if (Subs.size() != 1 || Adds.empty() || Subs[0]->getParent() != &MF.front())
    return false;

  int64_t Amount = 0;
  if (!isStackAdjust(*Subs[0], Bedrock::SUB64ri, Amount) || Amount == 0)
    return false;

  SmallPtrSet<MachineInstr *, 8> Ignored;
  Ignored.insert(Subs[0]);
  for (MachineInstr *Add : Adds) {
    int64_t AddAmount = 0;
    if (!isStackAdjust(*Add, Bedrock::ADD64ri, AddAmount) ||
        AddAmount != Amount)
      return false;
    Ignored.insert(Add);
  }

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || Ignored.contains(&MI))
        continue;

      for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
        const MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || MO.isImplicit() ||
            !regsOverlap(TRI, MO.getReg(), Bedrock::SP))
          continue;
        return false;
      }
    }
  }

  for (MachineInstr *Add : Adds)
    Add->eraseFromParent();
  Subs[0]->eraseFromParent();
  return true;
}

bool BedrockPeephole::shrinkUnusedPushPopMask(MachineFunction &MF) const {
  MachineInstr *PushM = nullptr;
  SmallVector<MachineInstr *, 4> PopMs;
  uint16_t Mask = 0;
  if (!collectConsistentPushPopMask(MF, PushM, PopMs, Mask))
    return false;

  SmallPtrSet<const MachineInstr *, 8> Ignored;
  Ignored.insert(PushM);
  for (MachineInstr *PopM : PopMs)
    Ignored.insert(PopM);

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint16_t NewMask = Mask;
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    uint16_t BitMask = uint16_t(1) << Bit;
    if ((Mask & BitMask) == 0)
      continue;
    if (!regTouchedOutsideInstrs(MF, getRegForMaskBit(Bit), Ignored, TRI))
      NewMask &= ~BitMask;
  }

  if (NewMask == Mask)
    return false;

  if (NewMask == 0) {
    for (MachineInstr *PopM : PopMs)
      PopM->eraseFromParent();
    PushM->eraseFromParent();
    return true;
  }

  uint16_t RemovedMask = Mask & ~NewMask;
  replaceWithPushPopMask(MF, *PushM, PopMs, NewMask);
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((RemovedMask & (uint16_t(1) << Bit)) != 0)
      removeRegLiveInsWithoutUses(MF, getRegForMaskBit(Bit), TRI);
  return true;
}

bool BedrockPeephole::legalizeLargeStackAdjustments(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  auto EmitChunked = [&](MachineBasicBlock &MBB, MachineInstr &MI,
                         unsigned Opcode, uint64_t Amount) {
    DebugLoc DL = MI.getDebugLoc();
    unsigned Flags = MI.getFlags();
    MachineBasicBlock::iterator Insert = MI.getIterator();
    while (Amount != 0) {
      uint64_t Chunk = std::min<uint64_t>(Amount, 63);
      while (!fitsImm6(Chunk))
        --Chunk;
      BuildMI(MBB, Insert, DL, TII.get(Opcode), Bedrock::SP)
          .addReg(Bedrock::SP)
          .addImm(Chunk)
          .setMIFlags(Flags);
      Amount -= Chunk;
    }
    MI.eraseFromParent();
  };

  auto EmitScratch = [&](MachineBasicBlock &MBB, MachineInstr &MI,
                         unsigned RegOpcode, Register Scratch,
                         uint64_t Amount) {
    DebugLoc DL = MI.getDebugLoc();
    unsigned Flags = MI.getFlags();
    MachineBasicBlock::iterator Insert = MI.getIterator();
    BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV32ri), Scratch)
        .addImm(Amount)
        .setMIFlags(Flags);
    BuildMI(MBB, Insert, DL, TII.get(RegOpcode), Bedrock::SP)
        .addReg(Bedrock::SP)
        .addReg(Scratch)
        .setMIFlags(Flags);
    MI.eraseFromParent();
  };

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      int64_t Amount = 0;
      bool IsSub = isStackAdjust(MI, Bedrock::SUB64ri, Amount);
      bool IsAdd = !IsSub && isStackAdjust(MI, Bedrock::ADD64ri, Amount);
      if ((!IsSub && !IsAdd) || fitsImm6(Amount))
        continue;

      std::optional<Register> Scratch;
      if (IsSub) {
        MachineBasicBlock::iterator Prev = prevNonDebug(MI.getIterator(), MBB);
        if (Prev != MBB.end() && isPushOpcode(Prev->getOpcode()))
          Scratch = getSavedDRegFromPushPopInstr(*Prev);
      } else {
        MachineBasicBlock::iterator Next = nextNonDebug(MI.getIterator(), MBB);
        if (Next != MBB.end() && isPopOpcode(Next->getOpcode()))
          Scratch = getSavedDRegFromPushPopInstr(*Next);
      }

      if (Scratch && Amount <= std::numeric_limits<int32_t>::max()) {
        EmitScratch(MBB, MI, IsSub ? Bedrock::SUB64rr : Bedrock::ADD64rr,
                    *Scratch, Amount);
      } else {
        EmitChunked(MBB, MI, IsSub ? Bedrock::SUB64ri : Bedrock::ADD64ri,
                    Amount);
      }
      Changed = true;
    }
  }

  return Changed;
}

