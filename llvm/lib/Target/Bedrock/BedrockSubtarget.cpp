//===-- BedrockSubtarget.cpp - Bedrock Subtarget --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockSubtarget.h"

#include "Bedrock.h"
#include "llvm/IR/RuntimeLibcalls.h"

using namespace llvm;

#define DEBUG_TYPE "bedrock-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "BedrockGenSubtargetInfo.inc"

BedrockSubtarget::BedrockSubtarget(const Triple &TT, StringRef CPU,
                                   StringRef FS, const TargetMachine &TM)
    : BedrockGenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS), TargetTriple(TT),
      InstrInfo(*this), FrameLowering(*this), TLInfo(TM, *this) {
  if (CPU.empty())
    CPU = "generic";
  ParseSubtargetFeatures(CPU, CPU, FS);
}

void BedrockSubtarget::initLibcallLoweringInfo(
    LibcallLoweringInfo &Info) const {
  const RTLIB::LibcallImpl AtomicHelpers[] = {
      RTLIB::impl___atomic_load,
      RTLIB::impl___atomic_load_1,
      RTLIB::impl___atomic_load_2,
      RTLIB::impl___atomic_load_4,
      RTLIB::impl___atomic_load_8,
      RTLIB::impl___atomic_load_16,
      RTLIB::impl___atomic_store,
      RTLIB::impl___atomic_store_1,
      RTLIB::impl___atomic_store_2,
      RTLIB::impl___atomic_store_4,
      RTLIB::impl___atomic_store_8,
      RTLIB::impl___atomic_store_16,
      RTLIB::impl___atomic_exchange,
      RTLIB::impl___atomic_exchange_1,
      RTLIB::impl___atomic_exchange_2,
      RTLIB::impl___atomic_exchange_4,
      RTLIB::impl___atomic_exchange_8,
      RTLIB::impl___atomic_exchange_16,
      RTLIB::impl___atomic_compare_exchange,
      RTLIB::impl___atomic_compare_exchange_1,
      RTLIB::impl___atomic_compare_exchange_2,
      RTLIB::impl___atomic_compare_exchange_4,
      RTLIB::impl___atomic_compare_exchange_8,
      RTLIB::impl___atomic_compare_exchange_16,
      RTLIB::impl___atomic_fetch_add_1,
      RTLIB::impl___atomic_fetch_add_2,
      RTLIB::impl___atomic_fetch_add_4,
      RTLIB::impl___atomic_fetch_add_8,
      RTLIB::impl___atomic_fetch_add_16,
      RTLIB::impl___atomic_fetch_sub_1,
      RTLIB::impl___atomic_fetch_sub_2,
      RTLIB::impl___atomic_fetch_sub_4,
      RTLIB::impl___atomic_fetch_sub_8,
      RTLIB::impl___atomic_fetch_sub_16,
      RTLIB::impl___atomic_fetch_and_1,
      RTLIB::impl___atomic_fetch_and_2,
      RTLIB::impl___atomic_fetch_and_4,
      RTLIB::impl___atomic_fetch_and_8,
      RTLIB::impl___atomic_fetch_and_16,
      RTLIB::impl___atomic_fetch_or_1,
      RTLIB::impl___atomic_fetch_or_2,
      RTLIB::impl___atomic_fetch_or_4,
      RTLIB::impl___atomic_fetch_or_8,
      RTLIB::impl___atomic_fetch_or_16,
      RTLIB::impl___atomic_fetch_xor_1,
      RTLIB::impl___atomic_fetch_xor_2,
      RTLIB::impl___atomic_fetch_xor_4,
      RTLIB::impl___atomic_fetch_xor_8,
      RTLIB::impl___atomic_fetch_xor_16,
      RTLIB::impl___atomic_fetch_nand_1,
      RTLIB::impl___atomic_fetch_nand_2,
      RTLIB::impl___atomic_fetch_nand_4,
      RTLIB::impl___atomic_fetch_nand_8,
      RTLIB::impl___atomic_fetch_nand_16,
  };

  for (RTLIB::LibcallImpl Impl : AtomicHelpers)
    Info.setLibcallImpl(RTLIB::RuntimeLibcallsInfo::getLibcallFromImpl(Impl),
                        Impl);
}
