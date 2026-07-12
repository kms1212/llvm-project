//===--- BuiltinsBedrock.td - Bedrock builtin database --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

class BedrockBuiltin<string prototype, string features = ""> : TargetBuiltin {
  let Spellings = ["__builtin_bedrock_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

let Attributes = [NoThrow, CustomTypeChecking] in {
def far_ptr_init : BedrockBuiltin<"void *(...)">;
def far_flat_ptr_init : BedrockBuiltin<"void *(...)">;
def far_null : BedrockBuiltin<"void *(...)">;
def far_ptr_from_segment : BedrockBuiltin<"void *(...)">;
def far_address : BedrockBuiltin<"uint64_t(...)">;
def far_same_encoding : BedrockBuiltin<"int(...)">;
}

let Attributes = [NoThrow] in {
def cpuid : BedrockBuiltin<"uint64_t(uint64_t)">;
def read_status : BedrockBuiltin<"unsigned short()">;
def rdpmc : BedrockBuiltin<"uint64_t(_Constant unsigned int)">;
def breakpoint : BedrockBuiltin<"void()">;
def trace : BedrockBuiltin<"void(_Constant unsigned int)">;
def yield : BedrockBuiltin<"void()">;
def wait : BedrockBuiltin<"void()">;

def read_fence : BedrockBuiltin<"void()">;
def write_fence : BedrockBuiltin<"void()">;
def address_fence : BedrockBuiltin<"void()">;
def nontemporal_store_u8 : BedrockBuiltin<"void(unsigned char *, unsigned char)">;
def nontemporal_store_u16 : BedrockBuiltin<"void(unsigned short *, unsigned short)">;
def nontemporal_store_u32 : BedrockBuiltin<"void(unsigned int *, unsigned int)">;
def nontemporal_store_u64 : BedrockBuiltin<"void(uint64_t *, uint64_t)">;

def read_fstatus : BedrockBuiltin<"unsigned short()", "fpu">;
def write_fstatus : BedrockBuiltin<"void(unsigned short)", "fpu">;
def read_fflags : BedrockBuiltin<"unsigned short()", "fpu">;
def write_fflags : BedrockBuiltin<"void(unsigned short)", "fpu">;
}

let Attributes = [NoThrow, Const] in {
def clmul_u8 : BedrockBuiltin<"unsigned char(unsigned char, unsigned char)">;
def clmul_u16 : BedrockBuiltin<"unsigned short(unsigned short, unsigned short)">;
def clmul_u32 : BedrockBuiltin<"unsigned int(unsigned int, unsigned int)">;
def clmul_u64 : BedrockBuiltin<"uint64_t(uint64_t, uint64_t)">;
def fclass_f32 : BedrockBuiltin<"unsigned short(float)", "fpu">;
def fclass_f64 : BedrockBuiltin<"unsigned short(double)", "fpu">;
}
