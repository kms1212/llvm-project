//===-- BedrockTargetMachine.cpp - Bedrock target machine -----------------===//
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
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalMerge.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Scalar/LoopFlatten.h"
#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include <optional>

using namespace llvm;

namespace {

static void collectAddTerms(Value *V, APInt &Base,
                            SmallVectorImpl<Value *> &Terms,
                            bool &SawConstant) {
  if (auto *BO = dyn_cast<BinaryOperator>(V);
      BO && BO->getOpcode() == Instruction::Add) {
    collectAddTerms(BO->getOperand(0), Base, Terms, SawConstant);
    collectAddTerms(BO->getOperand(1), Base, Terms, SawConstant);
    return;
  }
  if (auto *C = dyn_cast<ConstantInt>(V)) {
    Base += C->getValue().zextOrTrunc(Base.getBitWidth());
    SawConstant = true;
    return;
  }
  Terms.push_back(V);
}

// Bedrock uses integral flat pointers.  Recover a GEP from integer address
// arithmetic so SCEV and the loop optimizers can see the affine recurrence.
static bool canonicalizeIntToPtr(ArrayRef<BasicBlock *> Blocks) {
  if (Blocks.empty())
    return false;
  const DataLayout &DL = Blocks.front()->getDataLayout();
  SmallVector<IntToPtrInst *, 8> Casts;
  for (BasicBlock *BB : Blocks)
    for (Instruction &I : *BB)
      if (auto *Cast = dyn_cast<IntToPtrInst>(&I))
        Casts.push_back(Cast);

  bool Changed = false;
  for (IntToPtrInst *Cast : Casts) {
    auto *IntTy = dyn_cast<IntegerType>(Cast->getOperand(0)->getType());
    auto *PtrTy = dyn_cast<PointerType>(Cast->getType());
    if (!IntTy || !PtrTy || PtrTy->getAddressSpace() != 0 ||
        DL.isNonIntegralPointerType(PtrTy) ||
        IntTy->getBitWidth() != DL.getPointerSizeInBits(0))
      continue;

    APInt Base(IntTy->getBitWidth(), 0);
    SmallVector<Value *, 4> Terms;
    bool SawConstant = false;
    collectAddTerms(Cast->getOperand(0), Base, Terms, SawConstant);
    if (!SawConstant || Base.isZero() || Terms.empty())
      continue;

    IRBuilder<> Builder(Cast);
    Value *Offset = Terms.front();
    for (Value *Term : ArrayRef(Terms).drop_front())
      Offset = Builder.CreateAdd(Offset, Term, "bedrock.addr.offset");

    Constant *BaseInt = ConstantInt::get(IntTy, Base);
    Constant *BasePtr = ConstantExpr::getIntToPtr(BaseInt, PtrTy);
    Value *GEP = Builder.CreateGEP(Builder.getInt8Ty(), BasePtr, Offset,
                                   "bedrock.addr");
    Cast->replaceAllUsesWith(GEP);
    Cast->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

class BedrockCanonicalizeIntToPtrPass
    : public PassInfoMixin<BedrockCanonicalizeIntToPtrPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    SmallVector<BasicBlock *, 16> Blocks;
    for (BasicBlock &BB : F)
      Blocks.push_back(&BB);
    bool Changed = canonicalizeIntToPtr(Blocks);

    if (!Changed)
      return PreservedAnalyses::all();
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }
};

class BedrockCanonicalizeIntToPtrLoopPass
    : public PassInfoMixin<BedrockCanonicalizeIntToPtrLoopPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &,
                        LoopStandardAnalysisResults &AR, LPMUpdater &) {
    if (!canonicalizeIntToPtr(L.getBlocks()))
      return PreservedAnalyses::all();
    AR.SE.forgetLoop(&L);
    auto PA = getLoopPassPreservedAnalyses();
    if (AR.MSSA)
      PA.preserve<MemorySSAAnalysis>();
    return PA;
  }
};

} // namespace

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
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
  setMachineOutliner(true);
  setSupportsDefaultOutlining(true);
}

const BedrockSubtarget *
BedrockTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  std::string Key = CPU + FS;
  auto &I = SubtargetMap[Key];
  if (!I) {
    resetTargetOptions(F);
    I = std::make_unique<BedrockSubtarget>(
        TargetTriple, CPU, FS, *this, Options, getCodeModel(), getOptLevel());
  }
  return I.get();
}

TargetTransformInfo
BedrockTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<BedrockTTIImpl>(this, F));
}

void BedrockTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerPeepholeEPCallback(
      [](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level != OptimizationLevel::O0)
          FPM.addPass(BedrockCanonicalizeIntToPtrPass());
      });
  PB.registerLateLoopOptimizationsEPCallback(
      [](LoopPassManager &LPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;
        LPM.addPass(LoopFlattenPass());
        LPM.addPass(BedrockCanonicalizeIntToPtrLoopPass());
        LPM.addPass(LoopIdiomRecognizePass());
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
