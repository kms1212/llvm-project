//===--- Bedrock.cpp - Implement Bedrock target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Bedrock TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

const char *const BedrockTargetInfo::GCCRegNames[] = {
    "D0",    "D1",     "D2", "D3",  "D4",  "D5",  "D6",  "D7",  "A0",
    "A1",    "A2",     "A3", "A4",  "A5",  "A6",  "A7",  "SP",  "PC",
    "FLAGS", "STATUS", "F0", "F1",  "F2",  "F3",  "F4",  "F5",  "F6",
    "F7",    "F8",     "F9", "F10", "F11", "F12", "F13", "F14", "F15"};

ArrayRef<const char *> BedrockTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

const TargetInfo::GCCRegAlias BedrockTargetInfo::GCCRegAliases[] = {
    {{"d0"}, "D0"},       {{"d1"}, "D1"},         {{"d2"}, "D2"},
    {{"d3"}, "D3"},       {{"d4"}, "D4"},         {{"d5"}, "D5"},
    {{"d6"}, "D6"},       {{"d7"}, "D7"},         {{"a0"}, "A0"},
    {{"a1"}, "A1"},       {{"a2"}, "A2"},         {{"a3"}, "A3"},
    {{"a4"}, "A4"},       {{"a5"}, "A5"},         {{"a6"}, "A6"},
    {{"a7"}, "A7"},       {{"sp"}, "SP"},         {{"pc"}, "PC"},
    {{"flags"}, "FLAGS"}, {{"status"}, "STATUS"}, {{"f0"}, "F0"},
    {{"f1"}, "F1"},       {{"f2"}, "F2"},         {{"f3"}, "F3"},
    {{"f4"}, "F4"},       {{"f5"}, "F5"},         {{"f6"}, "F6"},
    {{"f7"}, "F7"},       {{"f8"}, "F8"},         {{"f9"}, "F9"},
    {{"f10"}, "F10"},     {{"f11"}, "F11"},       {{"f12"}, "F12"},
    {{"f13"}, "F13"},     {{"f14"}, "F14"},       {{"f15"}, "F15"}};

ArrayRef<TargetInfo::GCCRegAlias> BedrockTargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

void BedrockTargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__bedrock__");
  Builder.defineMacro("__BEDROCK__");
}
