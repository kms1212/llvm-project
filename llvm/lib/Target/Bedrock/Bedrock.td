//===-- Bedrock.td - Describe the Bedrock Target Machine ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

include "BedrockRegisterInfo.td"
include "BedrockCallingConv.td"
include "BedrockInstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPR64>;

def BedrockInstrInfo : InstrInfo;

def : Processor<"generic", NoItineraries, []>;

def BedrockInstPrinter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def BedrockAsmParser : AsmParser;

def Bedrock : Target {
  let InstructionSet = BedrockInstrInfo;
  let AssemblyWriters = [BedrockInstPrinter];
  let AssemblyParsers = [BedrockAsmParser];
}
