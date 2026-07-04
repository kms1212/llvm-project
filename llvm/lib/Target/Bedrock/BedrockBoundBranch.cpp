//===-- BedrockBoundBranch.cpp - Bedrock bounds branch folding ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "BedrockInstrInfo.h"
#include "BedrockISelLowering.h"
#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockCondCode.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "bedrock-bound-branch"
#define PASS_NAME "Bedrock bounds branch folding"

namespace {
struct BoundCompare {
  Register Value;
  Register Bound;
  bool IsSigned = false;
  bool Inclusive = false;
  unsigned Size = 0;
};

struct BoundFold {
  Register Value;
  Register Lo;
  Register Hi;
  bool IsSigned = false;
  unsigned Mode = BedrockISD::BND_II;
  unsigned Size = 0;
};

class BedrockBoundBranch : public MachineFunctionPass {
public:
  static char ID;

  BedrockBoundBranch() : MachineFunctionPass(ID) {
    initializeBedrockBoundBranchPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  bool foldBlock(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldSelectBlock(MachineBasicBlock &MBB, MachineFunction &MF) const;
};
} // end anonymous namespace

char BedrockBoundBranch::ID = 0;

INITIALIZE_PASS(BedrockBoundBranch, DEBUG_TYPE, PASS_NAME, false, false)

static unsigned getCmpSize(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::CMP8rr:
    return 8;
  case Bedrock::CMP16rr:
    return 16;
  case Bedrock::CMP32rr:
    return 32;
  case Bedrock::CMP64rr:
    return 64;
  }
}

static const TargetRegisterClass *getDRegClass(unsigned Size) {
  switch (Size) {
  default:
    return nullptr;
  case 8:
    return &Bedrock::D8RegClass;
  case 16:
    return &Bedrock::D16RegClass;
  case 32:
    return &Bedrock::D32RegClass;
  case 64:
    return &Bedrock::D64RegClass;
  }
}

static unsigned getBndOpcode(unsigned Size, unsigned Mode, bool IsSigned) {
  if (IsSigned) {
    switch (Size) {
    default:
      return 0;
    case 8:
      return Mode == BedrockISD::BND_II   ? Bedrock::BNDSII8rrr
             : Mode == BedrockISD::BND_IX ? Bedrock::BNDSIX8rrr
             : Mode == BedrockISD::BND_XI ? Bedrock::BNDSXI8rrr
                                           : Bedrock::BNDSXX8rrr;
    case 16:
      return Mode == BedrockISD::BND_II   ? Bedrock::BNDSII16rrr
             : Mode == BedrockISD::BND_IX ? Bedrock::BNDSIX16rrr
             : Mode == BedrockISD::BND_XI ? Bedrock::BNDSXI16rrr
                                           : Bedrock::BNDSXX16rrr;
    case 32:
      return Mode == BedrockISD::BND_II   ? Bedrock::BNDSII32rrr
             : Mode == BedrockISD::BND_IX ? Bedrock::BNDSIX32rrr
             : Mode == BedrockISD::BND_XI ? Bedrock::BNDSXI32rrr
                                           : Bedrock::BNDSXX32rrr;
    case 64:
      return Mode == BedrockISD::BND_II   ? Bedrock::BNDSII64rrr
             : Mode == BedrockISD::BND_IX ? Bedrock::BNDSIX64rrr
             : Mode == BedrockISD::BND_XI ? Bedrock::BNDSXI64rrr
                                           : Bedrock::BNDSXX64rrr;
    }
  }

  switch (Size) {
  default:
    return 0;
  case 8:
    return Mode == BedrockISD::BND_II   ? Bedrock::BNDUII8rrr
           : Mode == BedrockISD::BND_IX ? Bedrock::BNDUIX8rrr
           : Mode == BedrockISD::BND_XI ? Bedrock::BNDUXI8rrr
                                         : Bedrock::BNDUXX8rrr;
  case 16:
    return Mode == BedrockISD::BND_II   ? Bedrock::BNDUII16rrr
           : Mode == BedrockISD::BND_IX ? Bedrock::BNDUIX16rrr
           : Mode == BedrockISD::BND_XI ? Bedrock::BNDUXI16rrr
                                         : Bedrock::BNDUXX16rrr;
  case 32:
    return Mode == BedrockISD::BND_II   ? Bedrock::BNDUII32rrr
           : Mode == BedrockISD::BND_IX ? Bedrock::BNDUIX32rrr
           : Mode == BedrockISD::BND_XI ? Bedrock::BNDUXI32rrr
                                         : Bedrock::BNDUXX32rrr;
  case 64:
    return Mode == BedrockISD::BND_II   ? Bedrock::BNDUII64rrr
           : Mode == BedrockISD::BND_IX ? Bedrock::BNDUIX64rrr
           : Mode == BedrockISD::BND_XI ? Bedrock::BNDUXI64rrr
                                         : Bedrock::BNDUXX64rrr;
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

static MachineInstr *getPrevNonDebug(MachineBasicBlock &MBB,
                                     MachineInstr &MI) {
  auto I = MI.getIterator();
  while (I != MBB.begin()) {
    --I;
    if (!I->isDebugInstr())
      return &*I;
  }
  return nullptr;
}

static MachineInstr *getNextNonDebug(MachineBasicBlock &MBB,
                                     MachineInstr &MI) {
  for (auto I = std::next(MI.getIterator()); I != MBB.end(); ++I)
    if (!I->isDebugInstr())
      return &*I;
  return nullptr;
}

static bool getTwoTerminators(MachineBasicBlock &MBB, MachineInstr *&First,
                              MachineInstr *&Second) {
  SmallVector<MachineInstr *, 4> Terms;
  for (auto I = MBB.getFirstTerminator(); I != MBB.end(); ++I) {
    if (!I->isDebugInstr())
      Terms.push_back(&*I);
  }
  if (Terms.size() != 2)
    return false;
  First = Terms[0];
  Second = Terms[1];
  return true;
}

static bool isSelectOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::SELECT8:
  case Bedrock::SELECT16:
  case Bedrock::SELECT32:
  case Bedrock::SELECT64:
  case Bedrock::FSELECT32:
  case Bedrock::FSELECT64:
    return true;
  }
}

static std::optional<bool> getSignedness(unsigned CC) {
  switch (CC) {
  default:
    return std::nullopt;
  case BedrockCC::LT:
  case BedrockCC::LE:
  case BedrockCC::GT:
  case BedrockCC::GE:
    return true;
  case BedrockCC::ULT:
  case BedrockCC::ULE:
  case BedrockCC::UGT:
  case BedrockCC::UGE:
    return false;
  }
}

static bool classifyLowInside(const MachineInstr &Cmp, unsigned CC,
                              BoundCompare &Out) {
  std::optional<bool> Signed = getSignedness(CC);
  if (!Signed)
    return false;
  unsigned Size = getCmpSize(Cmp.getOpcode());
  if (Size == 0 || !Cmp.getOperand(0).isReg() || !Cmp.getOperand(1).isReg())
    return false;

  Register LHS = Cmp.getOperand(0).getReg();
  Register RHS = Cmp.getOperand(1).getReg();
  switch (CC) {
  default:
    return false;
  case BedrockCC::GE:
  case BedrockCC::UGE:
    Out = {LHS, RHS, *Signed, true, Size};
    return true;
  case BedrockCC::GT:
  case BedrockCC::UGT:
    Out = {LHS, RHS, *Signed, false, Size};
    return true;
  case BedrockCC::LE:
  case BedrockCC::ULE:
    Out = {RHS, LHS, *Signed, true, Size};
    return true;
  case BedrockCC::LT:
  case BedrockCC::ULT:
    Out = {RHS, LHS, *Signed, false, Size};
    return true;
  }
}

static bool classifyHighInside(const MachineInstr &Cmp, unsigned CC,
                               BoundCompare &Out) {
  std::optional<bool> Signed = getSignedness(CC);
  if (!Signed)
    return false;
  unsigned Size = getCmpSize(Cmp.getOpcode());
  if (Size == 0 || !Cmp.getOperand(0).isReg() || !Cmp.getOperand(1).isReg())
    return false;

  Register LHS = Cmp.getOperand(0).getReg();
  Register RHS = Cmp.getOperand(1).getReg();
  switch (CC) {
  default:
    return false;
  case BedrockCC::LE:
  case BedrockCC::ULE:
    Out = {LHS, RHS, *Signed, true, Size};
    return true;
  case BedrockCC::LT:
  case BedrockCC::ULT:
    Out = {LHS, RHS, *Signed, false, Size};
    return true;
  case BedrockCC::GE:
  case BedrockCC::UGE:
    Out = {RHS, LHS, *Signed, true, Size};
    return true;
  case BedrockCC::GT:
  case BedrockCC::UGT:
    Out = {RHS, LHS, *Signed, false, Size};
    return true;
  }
}

static bool classifyLowOutside(const MachineInstr &Cmp, unsigned CC,
                               BoundCompare &Out) {
  std::optional<bool> Signed = getSignedness(CC);
  if (!Signed)
    return false;
  unsigned Size = getCmpSize(Cmp.getOpcode());
  if (Size == 0 || !Cmp.getOperand(0).isReg() || !Cmp.getOperand(1).isReg())
    return false;

  Register LHS = Cmp.getOperand(0).getReg();
  Register RHS = Cmp.getOperand(1).getReg();
  switch (CC) {
  default:
    return false;
  case BedrockCC::LT:
  case BedrockCC::ULT:
    Out = {LHS, RHS, *Signed, true, Size};
    return true;
  case BedrockCC::LE:
  case BedrockCC::ULE:
    Out = {LHS, RHS, *Signed, false, Size};
    return true;
  case BedrockCC::GT:
  case BedrockCC::UGT:
    Out = {RHS, LHS, *Signed, true, Size};
    return true;
  case BedrockCC::GE:
  case BedrockCC::UGE:
    Out = {RHS, LHS, *Signed, false, Size};
    return true;
  }
}

static bool classifyHighOutside(const MachineInstr &Cmp, unsigned CC,
                                BoundCompare &Out) {
  std::optional<bool> Signed = getSignedness(CC);
  if (!Signed)
    return false;
  unsigned Size = getCmpSize(Cmp.getOpcode());
  if (Size == 0 || !Cmp.getOperand(0).isReg() || !Cmp.getOperand(1).isReg())
    return false;

  Register LHS = Cmp.getOperand(0).getReg();
  Register RHS = Cmp.getOperand(1).getReg();
  switch (CC) {
  default:
    return false;
  case BedrockCC::GT:
  case BedrockCC::UGT:
    Out = {LHS, RHS, *Signed, true, Size};
    return true;
  case BedrockCC::GE:
  case BedrockCC::UGE:
    Out = {LHS, RHS, *Signed, false, Size};
    return true;
  case BedrockCC::LT:
  case BedrockCC::ULT:
    Out = {RHS, LHS, *Signed, true, Size};
    return true;
  case BedrockCC::LE:
  case BedrockCC::ULE:
    Out = {RHS, LHS, *Signed, false, Size};
    return true;
  }
}

static bool makeFold(const BoundCompare &Low, const BoundCompare &High,
                     BoundFold &Fold) {
  if (Low.Value != High.Value || Low.IsSigned != High.IsSigned ||
      Low.Size != High.Size)
    return false;

  Fold.Value = Low.Value;
  Fold.Lo = Low.Bound;
  Fold.Hi = High.Bound;
  Fold.IsSigned = Low.IsSigned;
  Fold.Mode = getBndMode(Low.Inclusive, High.Inclusive);
  Fold.Size = Low.Size;
  return true;
}

static bool constrainToDRegs(MachineRegisterInfo &MRI,
                             const BoundFold &Fold) {
  const TargetRegisterClass *RC = getDRegClass(Fold.Size);
  if (!RC)
    return false;

  for (Register Reg : {Fold.Lo, Fold.Value, Fold.Hi}) {
    if (Reg.isPhysical()) {
      if (!RC->contains(Reg))
        return false;
      continue;
    }
    if (!MRI.constrainRegClass(Reg, RC))
      return false;
  }
  return true;
}

static bool sameRegOperand(const MachineInstr &MI, unsigned OpNo,
                           Register Reg) {
  return MI.getNumOperands() > OpNo && MI.getOperand(OpNo).isReg() &&
         MI.getOperand(OpNo).getReg() == Reg;
}

static bool matchSelectShape(const MachineInstr &Sel1,
                             const MachineInstr &Sel2, bool BranchOnInside,
                             Register &TrueReg, Register &FalseReg) {
  if (!isSelectOpcode(Sel1.getOpcode()) ||
      !isSelectOpcode(Sel2.getOpcode()) ||
      Sel1.getOpcode() != Sel2.getOpcode() ||
      Sel1.getNumOperands() < 4 || Sel2.getNumOperands() < 4 ||
      !Sel1.getOperand(0).isReg() || !Sel1.getOperand(1).isReg() ||
      !Sel1.getOperand(2).isReg() || !Sel2.getOperand(0).isReg() ||
      !Sel2.getOperand(1).isReg() || !Sel2.getOperand(2).isReg())
    return false;

  Register Mid = Sel1.getOperand(0).getReg();
  if (BranchOnInside) {
    TrueReg = Sel1.getOperand(1).getReg();
    FalseReg = Sel1.getOperand(2).getReg();
    return sameRegOperand(Sel2, 1, Mid) && sameRegOperand(Sel2, 2, FalseReg);
  }

  TrueReg = Sel1.getOperand(1).getReg();
  FalseReg = Sel1.getOperand(2).getReg();
  return sameRegOperand(Sel2, 1, TrueReg) && sameRegOperand(Sel2, 2, Mid);
}

static bool makeFoldFromOrderedCompares(const MachineInstr &Cmp1, unsigned CC1,
                                        const MachineInstr &Cmp2, unsigned CC2,
                                        bool BranchOnInside,
                                        BoundFold &Fold) {
  BoundCompare Low;
  BoundCompare High;
  if (BranchOnInside) {
    if (classifyLowInside(Cmp1, CC1, Low) &&
        classifyHighInside(Cmp2, CC2, High) && makeFold(Low, High, Fold))
      return true;
    if (classifyHighInside(Cmp1, CC1, High) &&
        classifyLowInside(Cmp2, CC2, Low) && makeFold(Low, High, Fold))
      return true;
    return false;
  }

  if (classifyLowOutside(Cmp1, CC1, Low) &&
      classifyHighOutside(Cmp2, CC2, High) && makeFold(Low, High, Fold))
    return true;
  if (classifyHighOutside(Cmp1, CC1, High) &&
      classifyLowOutside(Cmp2, CC2, Low) && makeFold(Low, High, Fold))
    return true;
  return false;
}

bool BedrockBoundBranch::foldSelectBlock(MachineBasicBlock &MBB,
                                         MachineFunction &MF) const {
  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Cmp1 = *I++;
    if (Cmp1.isDebugInstr())
      continue;

    MachineInstr *Sel1 = getNextNonDebug(MBB, Cmp1);
    MachineInstr *Cmp2 = Sel1 ? getNextNonDebug(MBB, *Sel1) : nullptr;
    MachineInstr *Sel2 = Cmp2 ? getNextNonDebug(MBB, *Cmp2) : nullptr;
    if (!Sel1 || !Cmp2 || !Sel2 ||
        !isSelectOpcode(Sel1->getOpcode()) ||
        !isSelectOpcode(Sel2->getOpcode()) ||
        getCmpSize(Cmp1.getOpcode()) == 0 ||
        getCmpSize(Cmp2->getOpcode()) == 0 ||
        !Sel1->getOperand(0).isReg() ||
        !Sel1->getOperand(0).getReg().isVirtual() ||
        !MRI.hasOneNonDBGUse(Sel1->getOperand(0).getReg()) ||
        !Sel1->getOperand(3).isImm() || !Sel2->getOperand(3).isImm())
      continue;

    for (bool BranchOnInside : {true, false}) {
      Register TrueReg;
      Register FalseReg;
      if (!matchSelectShape(*Sel1, *Sel2, BranchOnInside, TrueReg, FalseReg))
        continue;

      BoundFold Fold;
      if (!makeFoldFromOrderedCompares(Cmp1, Sel1->getOperand(3).getImm(),
                                       *Cmp2, Sel2->getOperand(3).getImm(),
                                       BranchOnInside, Fold) ||
          !constrainToDRegs(MRI, Fold))
        continue;

      unsigned BndOpcode = getBndOpcode(Fold.Size, Fold.Mode, Fold.IsSigned);
      if (BndOpcode == 0)
        continue;

      Register Dst = Sel2->getOperand(0).getReg();
      DebugLoc DL = Cmp1.getDebugLoc();
      BuildMI(MBB, Cmp1, DL, TII.get(BndOpcode))
          .addReg(Fold.Lo)
          .addReg(Fold.Value)
          .addReg(Fold.Hi);
      BuildMI(MBB, Cmp1, DL, TII.get(Sel2->getOpcode()), Dst)
          .addReg(TrueReg)
          .addReg(FalseReg)
          .addImm(BranchOnInside ? BedrockCC::VC : BedrockCC::VS);

      Sel2->eraseFromParent();
      Cmp2->eraseFromParent();
      Sel1->eraseFromParent();
      Cmp1.eraseFromParent();
      I = MBB.begin();
      Changed = true;
      break;
    }
  }

  return Changed;
}

bool BedrockBoundBranch::foldBlock(MachineBasicBlock &MBB,
                                   MachineFunction &MF) const {
  MachineInstr *JCC1 = nullptr;
  MachineInstr *JMP1 = nullptr;
  if (!getTwoTerminators(MBB, JCC1, JMP1) ||
      JCC1->getOpcode() != Bedrock::JCC || JMP1->getOpcode() != Bedrock::JMP)
    return false;

  MachineBasicBlock *Out = JCC1->getOperand(0).getMBB();
  MachineBasicBlock *Mid = JMP1->getOperand(0).getMBB();
  if (!Mid || !Out || Mid == Out || Mid->pred_size() != 1 ||
      *Mid->pred_begin() != &MBB)
    return false;

  MachineInstr *JCC2 = nullptr;
  MachineInstr *JMP2 = nullptr;
  if (!getTwoTerminators(*Mid, JCC2, JMP2) ||
      JCC2->getOpcode() != Bedrock::JCC || JMP2->getOpcode() != Bedrock::JMP)
    return false;
  if (JCC2->getOperand(0).getMBB() != Out)
    return false;
  MachineBasicBlock *In = JMP2->getOperand(0).getMBB();
  if (!In || In == Out)
    return false;

  MachineInstr *Cmp1 = getPrevNonDebug(MBB, *JCC1);
  MachineInstr *Cmp2 = getPrevNonDebug(*Mid, *JCC2);
  if (!Cmp1 || !Cmp2)
    return false;

  BoundCompare Low;
  BoundCompare High;
  if (!classifyLowOutside(*Cmp1, JCC1->getOperand(1).getImm(), Low) ||
      !classifyHighOutside(*Cmp2, JCC2->getOperand(1).getImm(), High))
    return false;

  BoundFold Fold;
  if (!makeFold(Low, High, Fold))
    return false;

  MachineRegisterInfo &MRI = MF.getRegInfo();
  if (!constrainToDRegs(MRI, Fold))
    return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  unsigned BndOpcode = getBndOpcode(Fold.Size, Fold.Mode, Fold.IsSigned);
  if (BndOpcode == 0)
    return false;

  DebugLoc DL = Cmp1->getDebugLoc();
  BuildMI(MBB, Cmp1, DL, TII.get(BndOpcode))
      .addReg(Fold.Lo)
      .addReg(Fold.Value)
      .addReg(Fold.Hi);
  BuildMI(MBB, Cmp1, DL, TII.get(Bedrock::JCC))
      .addMBB(In)
      .addImm(BedrockCC::VC);
  BuildMI(MBB, Cmp1, DL, TII.get(Bedrock::JMP)).addMBB(Out);

  Cmp1->eraseFromParent();
  JCC1->eraseFromParent();
  JMP1->eraseFromParent();

  for (MachineBasicBlock *Succ : Mid->successors()) {
    for (MachineInstr &MI : *Succ) {
      if (!MI.isPHI())
        break;
      for (MachineOperand &MO : MI.operands()) {
        if (MO.isMBB() && MO.getMBB() == Mid)
          MO.setMBB(&MBB);
      }
    }
  }

  MBB.removeSuccessor(Mid);
  if (!MBB.isSuccessor(In))
    MBB.addSuccessor(In);
  if (!MBB.isSuccessor(Out))
    MBB.addSuccessor(Out);
  while (!Mid->succ_empty())
    Mid->removeSuccessor(Mid->succ_begin());
  Mid->eraseFromParent();
  return true;
}

bool BedrockBoundBranch::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getFunction().hasOptNone())
    return false;

  SmallVector<MachineBasicBlock *, 8> Blocks;
  for (MachineBasicBlock &MBB : MF)
    Blocks.push_back(&MBB);

  bool Changed = false;
  for (MachineBasicBlock *MBB : Blocks) {
    if (MBB->getParent() == &MF) {
      Changed |= foldSelectBlock(*MBB, MF);
      Changed |= foldBlock(*MBB, MF);
    }
  }
  return Changed;
}

FunctionPass *llvm::createBedrockBoundBranchPass() {
  return new BedrockBoundBranch();
}
