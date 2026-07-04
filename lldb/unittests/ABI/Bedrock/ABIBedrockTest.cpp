//===-- ABIBedrockTest.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ABI/Bedrock/ABIBedrock.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicRegisterInfo.h"
#include "lldb/Utility/ArchSpec.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "gtest/gtest.h"

using namespace lldb;
using namespace lldb_private;

class ABIBedrockTest : public testing::Test {
public:
  static void SetUpTestCase() {
    LLVMInitializeBedrockTargetInfo();
    LLVMInitializeBedrockTargetMC();
    ABIBedrock::Initialize();
  }

  static void TearDownTestCase() {
    ABIBedrock::Terminate();
    llvm::llvm_shutdown();
  }
};

TEST_F(ABIBedrockTest, FindPlugin) {
  ABISP abi_sp =
      ABI::FindPlugin(ProcessSP(), ArchSpec("bedrock-unknown-unknown"));
  ASSERT_TRUE(abi_sp);
  EXPECT_EQ(abi_sp->GetPluginName(), "bedrock-c");
}

TEST_F(ABIBedrockTest, AugmentRegisterInfo) {
  ABISP abi_sp =
      ABI::FindPlugin(ProcessSP(), ArchSpec("bedrock-unknown-unknown"));
  ASSERT_TRUE(abi_sp);

  using Register = DynamicRegisterInfo::Register;
  std::vector<Register> regs;
  for (llvm::StringRef name : {"PC", "SP", "A7", "D0", "A0", "f8"}) {
    Register reg;
    reg.name = ConstString(name);
    reg.set_name = ConstString("Bedrock");
    regs.push_back(reg);
  }

  abi_sp->AugmentRegisterInfo(regs);

  EXPECT_EQ(regs[0].regnum_dwarf, 17u);
  EXPECT_EQ(regs[0].regnum_ehframe, 17u);
  EXPECT_EQ(regs[0].regnum_generic,
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_PC));

  EXPECT_EQ(regs[1].regnum_dwarf, 16u);
  EXPECT_EQ(regs[1].regnum_ehframe, 16u);
  EXPECT_EQ(regs[1].regnum_generic,
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_SP));

  EXPECT_EQ(regs[2].regnum_dwarf, 15u);
  EXPECT_EQ(regs[2].regnum_ehframe, 15u);
  EXPECT_EQ(regs[2].regnum_generic,
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_FP));

  EXPECT_EQ(regs[3].regnum_dwarf, 0u);
  EXPECT_EQ(regs[3].regnum_ehframe, 0u);
  EXPECT_EQ(regs[3].regnum_generic,
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_ARG1));

  EXPECT_EQ(regs[4].regnum_dwarf, 8u);
  EXPECT_EQ(regs[4].regnum_ehframe, 8u);
  EXPECT_EQ(regs[4].regnum_generic, LLDB_INVALID_REGNUM);

  EXPECT_EQ(regs[5].regnum_dwarf, 40u);
  EXPECT_EQ(regs[5].regnum_ehframe, 40u);
  EXPECT_EQ(regs[5].regnum_generic, LLDB_INVALID_REGNUM);
}
