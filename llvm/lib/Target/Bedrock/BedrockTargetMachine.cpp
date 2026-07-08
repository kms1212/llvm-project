//===-- BedrockTargetMachine.cpp - Bedrock TargetMachine ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockTargetMachine.h"

#include "Bedrock.h"
#include "BedrockMachineFunctionInfo.h"
#include "BedrockTargetTransformInfo.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockTarget() {
  RegisterTargetMachine<BedrockTargetMachine> X(getTheBedrockTarget());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeBedrockAsmPrinterPass(PR);
  initializeBedrockBoundBranchPass(PR);
  initializeBedrockDAGToDAGISelLegacyPass(PR);
  initializeBedrockPeepholePass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static std::string computeDataLayout() {
  return "e-m:e-p:64:64-i64:64-i128:128-n8:16:32:64-S128";
}

static BedrockPeepholeProfile
getPeepholeProfile(CodeGenOptLevel OptLevel) {
  switch (OptLevel) {
  case CodeGenOptLevel::None:
    return BedrockPeepholeProfile::O0;
  case CodeGenOptLevel::Less:
    return BedrockPeepholeProfile::O1;
  case CodeGenOptLevel::Default:
    return BedrockPeepholeProfile::O2;
  case CodeGenOptLevel::Aggressive:
    return BedrockPeepholeProfile::O3;
  }
  llvm_unreachable("unknown Bedrock codegen optimization level");
}

BedrockTargetMachine::BedrockTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, computeDataLayout(), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
}

const BedrockSubtarget *
BedrockTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  StringRef CPU = !CPUAttr.hasAttribute(Attribute::None)
                      ? CPUAttr.getValueAsString()
                      : TargetCPU;
  StringRef FS = !FSAttr.hasAttribute(Attribute::None)
                     ? FSAttr.getValueAsString()
                     : TargetFS;
  if (CPU.empty())
    CPU = "generic";

  std::string Key = (CPU + FS).str();
  auto &I = SubtargetMap[Key];
  if (!I)
    I = std::make_unique<BedrockSubtarget>(TargetTriple, CPU, FS, *this);
  return I.get();
}

TargetTransformInfo
BedrockTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<BedrockTTIImpl>(this, F));
}

MachineFunctionInfo *BedrockTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return BedrockMachineFunctionInfo::create<BedrockMachineFunctionInfo>(
      Allocator, F, STI);
}

namespace {
class BedrockPassConfig : public TargetPassConfig {
public:
  BedrockPassConfig(BedrockTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  BedrockTargetMachine &getBedrockTargetMachine() const {
    return getTM<BedrockTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};
} // end anonymous namespace

TargetPassConfig *BedrockTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new BedrockPassConfig(*this, PM);
}

void BedrockPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool BedrockPassConfig::addInstSelector() {
  addPass(createBedrockISelDag(getBedrockTargetMachine(), getOptLevel()));
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createBedrockBoundBranchPass());
  return false;
}

void BedrockPassConfig::addPreEmitPass() {
  addPass(createBedrockPeepholePass(getPeepholeProfile(getOptLevel())));
}
