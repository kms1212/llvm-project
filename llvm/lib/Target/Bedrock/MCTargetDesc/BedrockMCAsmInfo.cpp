//===-- BedrockMCAsmInfo.cpp - Bedrock asm properties ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockMCAsmInfo.h"
#include "BedrockMCEncoding.h"

using namespace llvm;

void BedrockMCAsmInfo::anchor() {}

BedrockMCAsmInfo::BedrockMCAsmInfo(const Triple &TT) {
  CodePointerSize = 8;
  CalleeSaveStackSlotSize = 8;
  MinInstAlignment = 2;
  CommentString = ";";
  SymbolQuoteCharacter = '`';
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
}

bool BedrockMCAsmInfo::isValidUnquotedName(StringRef Name) const {
  return MCAsmInfoELF::isValidUnquotedName(Name) &&
         !BedrockMC::isReservedAssemblyName(Name);
}
