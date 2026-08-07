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

multiclass BedrockApproxUnaryBuiltins {
  def _f32 : BedrockBuiltin<"float(float)", "fptransa">;
  def _f64 : BedrockBuiltin<"double(double)", "fptransa">;
}

defm facosa : BedrockApproxUnaryBuiltins;
defm fasina : BedrockApproxUnaryBuiltins;
defm fatana : BedrockApproxUnaryBuiltins;
defm fatanha : BedrockApproxUnaryBuiltins;
defm fcosa : BedrockApproxUnaryBuiltins;
defm fcosha : BedrockApproxUnaryBuiltins;
defm fetoxa : BedrockApproxUnaryBuiltins;
defm fetoxm1a : BedrockApproxUnaryBuiltins;
defm flog10a : BedrockApproxUnaryBuiltins;
defm flog2a : BedrockApproxUnaryBuiltins;
defm flogna : BedrockApproxUnaryBuiltins;
defm flognp1a : BedrockApproxUnaryBuiltins;
defm fsina : BedrockApproxUnaryBuiltins;
defm fsinha : BedrockApproxUnaryBuiltins;
defm ftana : BedrockApproxUnaryBuiltins;
defm ftanha : BedrockApproxUnaryBuiltins;
defm ftentoxa : BedrockApproxUnaryBuiltins;
defm ftwotoxa : BedrockApproxUnaryBuiltins;
}

let Attributes = [NoThrow] in {
def fsincosa_f32 :
    BedrockBuiltin<"void(float, float *, float *)", "fptransa">;
def fsincosa_f64 :
    BedrockBuiltin<"void(double, double *, double *)", "fptransa">;

def write_status : BedrockBuiltin<"void(unsigned short)">;
def read_control_register :
    BedrockBuiltin<"uint64_t(_Constant unsigned int)">;
def write_control_register :
    BedrockBuiltin<"void(_Constant unsigned int, uint64_t)">;
def read_segment_register :
    BedrockBuiltin<"uint64_t(_Constant unsigned int)">;
def read_code_segment : BedrockBuiltin<"uint64_t()">;
def write_segment_register :
    BedrockBuiltin<"void(_Constant unsigned int, uint64_t)">;

def flush_dcache : BedrockBuiltin<"void(void *, unsigned long int)">;
def invalidate_dcache : BedrockBuiltin<"void(void *, unsigned long int)">;
def invalidate_icache : BedrockBuiltin<"void(void *, unsigned long int)">;
def writeback_dcache : BedrockBuiltin<"void(void *, unsigned long int)">;
def sync_cache : BedrockBuiltin<"void(void *, unsigned long int)">;

def invalidate_tlb : BedrockBuiltin<"void()">;
def invalidate_page : BedrockBuiltin<"void(void const *)">;
def invalidate_asid :
    BedrockBuiltin<"void(_Constant unsigned int)">;
def switch_page_table : BedrockBuiltin<"void(uint64_t)">;
def switch_page_table_asid :
    BedrockBuiltin<"void(uint64_t, unsigned short)">;
def virtual_to_physical :
    BedrockBuiltin<"void(uint64_t, uint64_t *, unsigned short *)">;
def page_table_query :
    BedrockBuiltin<"void(_Constant unsigned int, uint64_t, uint64_t *, unsigned short *)">;

def save_processor_state : BedrockBuiltin<"void(void *)">;
def restore_processor_state : BedrockBuiltin<"void(void const *)">;
}
