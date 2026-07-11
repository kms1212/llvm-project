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
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

BedrockFrameLowering::BedrockFrameLowering(const BedrockSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(16), 0,
                          Align(8)) {}

void BedrockFrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  // CALL pushes an eight-byte return address.  Choose the smallest caller
  // frame whose size is 8 modulo 16, instead of first rounding the raw frame
  // to 16 and then adding another eight bytes.  A leaf only needs the ABI's
  // eight-byte internal alignment.  Object offsets remain relative to the
  // 16-byte-aligned incoming SP, so their declared alignment is preserved.
  StackSize = MFI.hasCalls() ? alignTo(StackSize + 8, Align(16)) - 8
                             : alignTo(StackSize, Align(8));
  MFI.setStackSize(StackSize);
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_DOWN)).addImm(StackSize);
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
  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_UP)).addImm(StackSize);
}

MachineBasicBlock::iterator BedrockFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  return MBB.erase(MI);
}

bool BedrockFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return false;
}
