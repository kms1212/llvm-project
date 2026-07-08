//===-- BedrockTargetMachine.cpp - Bedrock target machine -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockTargetMachine.h"
#include "Bedrock.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockTarget() {
  RegisterTargetMachine<BedrockTargetMachine> X(getTheBedrockTarget());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeBedrockAsmPrinterPass(PR);
  initializeBedrockDAGToDAGISelLegacyPass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

BedrockTargetMachine::BedrockTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, CPU, FS, *this, Options, getCodeModel(), OL) {
  initAsmInfo();
}

namespace {
class BedrockPassConfig : public TargetPassConfig {
public:
  BedrockPassConfig(BedrockTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  BedrockTargetMachine &getBedrockTargetMachine() const {
    return getTM<BedrockTargetMachine>();
  }

  bool addInstSelector() override;
};
} // namespace

TargetPassConfig *BedrockTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new BedrockPassConfig(*this, PM);
}

bool BedrockPassConfig::addInstSelector() {
  addPass(createBedrockISelDag(getBedrockTargetMachine(), getOptLevel()));
  return false;
}
