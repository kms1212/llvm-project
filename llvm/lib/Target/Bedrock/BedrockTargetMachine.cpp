//===-- BedrockTargetMachine.cpp - Bedrock target machine -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockTargetMachine.h"
#include "Bedrock.h"
#include "BedrockAliasAnalysis.h"
#include "BedrockMachineFunctionInfo.h"
#include "BedrockTargetTransformInfo.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/GlobalMerge.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

static cl::opt<cl::boolOrDefault>
    EnableGlobalMerge("bedrock-enable-global-merge", cl::Hidden,
                      cl::desc("Enable the global merge pass"));

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockTarget() {
  RegisterTargetMachine<BedrockTargetMachine> X(getTheBedrockTarget());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeBedrockAsmPrinterPass(PR);
  initializeBedrockDAGToDAGISelLegacyPass(PR);
  initializeBedrockPreEmitPeepholePass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static CodeModel::Model
getBedrockCodeModel(std::optional<CodeModel::Model> CM) {
  // Bedrock uses Tiny and Kernel as the internal spellings of its public low
  // and high placement contracts.
  return CM.value_or(CodeModel::Small);
}

BedrockTargetMachine::BedrockTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getBedrockCodeModel(CM), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, CPU, FS, *this, Options, getCodeModel(), OL) {
  initAsmInfo();
}

TargetTransformInfo
BedrockTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<BedrockTTIImpl>(this, F));
}

void BedrockTargetMachine::registerDefaultAliasAnalyses(AAManager &AAM) {
  AAM.registerFunctionAnalysis<BedrockAA>();
}

void BedrockTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerAnalysisRegistrationCallback([](FunctionAnalysisManager &FAM) {
    FAM.registerPass([] { return BedrockAA(); });
  });
  PB.registerParseAACallback([](StringRef Name, AAManager &AAM) {
    if (Name != "bedrock-aa")
      return false;
    AAM.registerFunctionAnalysis<BedrockAA>();
    return true;
  });
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
  bool addPreISel() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *BedrockTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new BedrockPassConfig(*this, PM);
}

void BedrockPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool BedrockPassConfig::addInstSelector() {
  addPass(createBedrockISelDag(getBedrockTargetMachine(), getOptLevel()));
  return false;
}

bool BedrockPassConfig::addPreISel() {
  if ((TM->getOptLevel() != CodeGenOptLevel::None &&
       EnableGlobalMerge == cl::BOU_UNSET) ||
      EnableGlobalMerge == cl::BOU_TRUE)
    addPass(createGlobalMergePass(TM, /*MaxOffset=*/32767,
                                  /*OnlyOptimizeForSize=*/false,
                                  /*MergeExternalByDefault=*/true));

  return false;
}

void BedrockPassConfig::addPreEmitPass() {
  addPass(createBedrockPreEmitPeepholePass());
}
