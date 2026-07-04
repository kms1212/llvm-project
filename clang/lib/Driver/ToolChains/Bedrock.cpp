//===--- Bedrock.cpp - Bedrock ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

void tools::bedrock::Assembler::ConstructJob(Compilation &C,
                                             const JobAction &JA,
                                             const InputInfo &Output,
                                             const InputInfoList &Inputs,
                                             const ArgList &Args,
                                             const char *LinkingOutput) const {
  claimNoWarnArgs(Args);
  ArgStringList CmdArgs;

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  for (const auto &Input : Inputs)
    CmdArgs.push_back(Input.getFilename());

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("bedrock-as"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

void tools::bedrock::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                          const InputInfo &Output,
                                          const InputInfoList &Inputs,
                                          const ArgList &Args,
                                          const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  CmdArgs.push_back("-m");
  CmdArgs.push_back("elf64bedrock");

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  TC.AddFilePathLibArgs(Args, CmdArgs);
  for (const auto &LibPath : TC.getLibraryPaths())
    CmdArgs.push_back(Args.MakeArgString(llvm::Twine("-L", LibPath)));

  Args.addAllArgs(CmdArgs, {options::OPT_T_Group, options::OPT_s,
                            options::OPT_t, options::OPT_r,
                            options::OPT_u});

  if (D.isUsingLTO())
    addLTOOptions(TC, Args, CmdArgs, Output, Inputs,
                  D.getLTOMode() == LTOK_Thin);

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    AddRunTimeLibs(TC, D, CmdArgs, Args);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  const char *Exec = Args.MakeArgString(TC.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

Tool *BedrockToolChain::buildAssembler() const {
  return new tools::bedrock::Assembler(*this);
}

Tool *BedrockToolChain::buildLinker() const {
  return new tools::bedrock::Linker(*this);
}

std::string BedrockToolChain::getCompilerRTPath() const {
  llvm::SmallString<128> Path(getDriver().ResourceDir);
  llvm::sys::path::append(Path, "lib", "generic");
  return std::string(Path);
}
