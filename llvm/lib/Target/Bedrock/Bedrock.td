//===-- Bedrock.td - Describe the Bedrock target ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

def FeatureFPU : SubtargetFeature<"fpu", "HasFPU", "true",
                                  "Enable floating-point instructions">;
def FeatureVirtAccel : SubtargetFeature<"virtaccel", "HasVirtAccel", "true",
                                        "Enable virtualization acceleration">;

def : Proc<"generic", [FeatureFPU]>;

include "BedrockRegisterInfo.td"
include "BedrockCallingConv.td"
include "BedrockInstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPR64>;

def BedrockInstrInfo : InstrInfo;

def BedrockAsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def BedrockAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 1;
}

def Bedrock : Target {
  let InstructionSet = BedrockInstrInfo;
  let AssemblyWriters = [BedrockAsmWriter];
  let AssemblyParsers = [BedrockAsmParser];
}
