//===-- ABIBedrock.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIBedrock.h"

#include <array>
#include <cstring>
#include <iterator>
#include <limits>

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABIBedrock, ABIBedrock)

namespace {
namespace dwarf {
enum regnums {
  d0 = 0,
  d1,
  d2,
  d3,
  d4,
  d5,
  d6,
  d7,
  a0,
  a1,
  a2,
  a3,
  a4,
  a5,
  a6,
  a7,
  sp,
  pc,
  flags,
  status,
  f0 = 32,
  f1,
  f2,
  f3,
  f4,
  f5,
  f6,
  f7,
  f8,
  f9,
  f10,
  f11,
  f12,
  f13,
  f14,
  f15,
};
} // namespace dwarf

#define DEFINE_REGISTER(name, alt, dwarf_num, byte_size, encoding, format,     \
                        generic_num)                                           \
  {                                                                            \
      name,                                                                    \
      alt,                                                                     \
      byte_size,                                                               \
      0,                                                                       \
      encoding,                                                                \
      format,                                                                  \
      {dwarf_num, dwarf_num, generic_num, LLDB_INVALID_REGNUM, dwarf_num},     \
      nullptr,                                                                 \
      nullptr,                                                                 \
      nullptr,                                                                 \
  }

#define DEFINE_GPR(name, alt, dwarf_num, generic_num)                          \
  DEFINE_REGISTER(name, alt, dwarf_num, 8, eEncodingUint, eFormatHex,          \
                  generic_num)

#define DEFINE_FPR(name, alt, dwarf_num)                                       \
  DEFINE_REGISTER(name, alt, dwarf_num, 8, eEncodingIEEE754, eFormatFloat,     \
                  LLDB_INVALID_REGNUM)

static const std::array<RegisterInfo, 36> g_register_infos = {
    {DEFINE_GPR("D0", "d0", dwarf::d0, LLDB_REGNUM_GENERIC_ARG1),
     DEFINE_GPR("D1", "d1", dwarf::d1, LLDB_REGNUM_GENERIC_ARG2),
     DEFINE_GPR("D2", "d2", dwarf::d2, LLDB_REGNUM_GENERIC_ARG3),
     DEFINE_GPR("D3", "d3", dwarf::d3, LLDB_REGNUM_GENERIC_ARG4),
     DEFINE_GPR("D4", "d4", dwarf::d4, LLDB_REGNUM_GENERIC_ARG5),
     DEFINE_GPR("D5", "d5", dwarf::d5, LLDB_REGNUM_GENERIC_ARG6),
     DEFINE_GPR("D6", "d6", dwarf::d6, LLDB_INVALID_REGNUM),
     DEFINE_GPR("D7", "d7", dwarf::d7, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A0", "a0", dwarf::a0, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A1", "a1", dwarf::a1, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A2", "a2", dwarf::a2, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A3", "a3", dwarf::a3, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A4", "a4", dwarf::a4, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A5", "a5", dwarf::a5, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A6", "a6", dwarf::a6, LLDB_INVALID_REGNUM),
     DEFINE_GPR("A7", "fp", dwarf::a7, LLDB_REGNUM_GENERIC_FP),
     DEFINE_GPR("SP", "sp", dwarf::sp, LLDB_REGNUM_GENERIC_SP),
     DEFINE_GPR("PC", "pc", dwarf::pc, LLDB_REGNUM_GENERIC_PC),
     DEFINE_REGISTER("FLAGS", "flags", dwarf::flags, 2, eEncodingUint,
                     eFormatHex, LLDB_INVALID_REGNUM),
     DEFINE_REGISTER("STATUS", "status", dwarf::status, 2, eEncodingUint,
                     eFormatHex, LLDB_INVALID_REGNUM),
     DEFINE_FPR("F0", "f0", dwarf::f0),
     DEFINE_FPR("F1", "f1", dwarf::f1),
     DEFINE_FPR("F2", "f2", dwarf::f2),
     DEFINE_FPR("F3", "f3", dwarf::f3),
     DEFINE_FPR("F4", "f4", dwarf::f4),
     DEFINE_FPR("F5", "f5", dwarf::f5),
     DEFINE_FPR("F6", "f6", dwarf::f6),
     DEFINE_FPR("F7", "f7", dwarf::f7),
     DEFINE_FPR("F8", "f8", dwarf::f8),
     DEFINE_FPR("F9", "f9", dwarf::f9),
     DEFINE_FPR("F10", "f10", dwarf::f10),
     DEFINE_FPR("F11", "f11", dwarf::f11),
     DEFINE_FPR("F12", "f12", dwarf::f12),
     DEFINE_FPR("F13", "f13", dwarf::f13),
     DEFINE_FPR("F14", "f14", dwarf::f14),
     DEFINE_FPR("F15", "f15", dwarf::f15)}};

#undef DEFINE_FPR
#undef DEFINE_GPR
#undef DEFINE_REGISTER

static constexpr llvm::StringLiteral kIntegerArgRegs[] = {"D0", "D1", "D2",
                                                          "D3", "D4", "D5"};
static constexpr llvm::StringLiteral kPointerArgRegs[] = {"A0", "A1", "A2",
                                                          "A3", "A4", "A5"};
static constexpr llvm::StringLiteral kFloatArgRegs[] = {"F0", "F1", "F2", "F3",
                                                        "F4", "F5", "F6", "F7"};

static const RegisterInfo *GetRegisterInfoByName(RegisterContext &reg_ctx,
                                                 llvm::StringRef name) {
  const RegisterInfo *reg_info = reg_ctx.GetRegisterInfoByName(name);
  if (!reg_info)
    LLDB_LOG(GetLog(LLDBLog::Expressions), "Missing Bedrock register {0}",
             name);
  return reg_info;
}

static bool WriteRegister(RegisterContext &reg_ctx, llvm::StringRef name,
                          addr_t value) {
  const RegisterInfo *reg_info = GetRegisterInfoByName(reg_ctx, name);
  return reg_info && reg_ctx.WriteRegisterFromUnsigned(reg_info, value);
}

template <typename T>
static void SetInteger(Scalar &scalar, uint64_t raw_value, bool is_signed) {
  static_assert(std::is_unsigned<T>::value, "T must be unsigned.");
  raw_value &= std::numeric_limits<T>::max();
  if (is_signed)
    scalar = static_cast<typename std::make_signed<T>::type>(raw_value);
  else
    scalar = static_cast<T>(raw_value);
}

static bool SetSizedInteger(Scalar &scalar, uint64_t raw_value,
                            uint64_t raw_high_value, uint8_t size_in_bytes,
                            bool is_signed) {
  switch (size_in_bytes) {
  default:
    return false;
  case 16: {
    std::array<uint64_t, 2> words = {raw_value, raw_high_value};
    scalar = llvm::APSInt(llvm::APInt(128, words), !is_signed);
    return true;
  }
  case sizeof(uint64_t):
    SetInteger<uint64_t>(scalar, raw_value, is_signed);
    return true;
  case sizeof(uint32_t):
    SetInteger<uint32_t>(scalar, raw_value, is_signed);
    return true;
  case sizeof(uint16_t):
    SetInteger<uint16_t>(scalar, raw_value, is_signed);
    return true;
  case sizeof(uint8_t):
    SetInteger<uint8_t>(scalar, raw_value, is_signed);
    return true;
  }
}

static bool SetSizedFloat(Scalar &scalar, uint64_t raw_value,
                          uint8_t size_in_bytes) {
  switch (size_in_bytes) {
  default:
    return false;
  case sizeof(uint64_t): {
    double value;
    std::memcpy(&value, &raw_value, sizeof(value));
    scalar = value;
    return true;
  }
  case sizeof(uint32_t): {
    uint32_t raw32 = static_cast<uint32_t>(raw_value);
    float value;
    std::memcpy(&value, &raw32, sizeof(value));
    scalar = value;
    return true;
  }
  }
}

static bool WriteStackValue(Process &process, addr_t address, addr_t value) {
  Status error;
  return process.WritePointerToMemory(address, value, error);
}

static bool IsPointerArgument(llvm::Type &type) { return type.isPointerTy(); }

static bool IsFloatArgument(llvm::Type &type) {
  return type.isFloatTy() || type.isDoubleTy();
}

static uint64_t ReadRegisterUnsigned(RegisterContext &reg_ctx,
                                     llvm::StringRef name) {
  const RegisterInfo *reg_info = GetRegisterInfoByName(reg_ctx, name);
  if (!reg_info)
    return 0;
  return reg_ctx.ReadRegisterAsUnsigned(reg_info, 0);
}

} // namespace

const RegisterInfo *ABIBedrock::GetRegisterInfoArray(uint32_t &count) {
  count = g_register_infos.size();
  return g_register_infos.data();
}

ABISP ABIBedrock::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() != llvm::Triple::bedrock)
    return ABISP();
  return ABISP(new ABIBedrock(std::move(process_sp), MakeMCRegisterInfo(arch)));
}

bool ABIBedrock::PrepareTrivialCall(Thread &thread, addr_t sp, addr_t func_addr,
                                    addr_t return_addr,
                                    llvm::ArrayRef<addr_t> args) const {
  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  ProcessSP process_sp = thread.GetProcess();
  if (!reg_ctx || !process_sp || args.size() > std::size(kIntegerArgRegs))
    return false;

  for (size_t i = 0; i < args.size(); ++i)
    if (!WriteRegister(*reg_ctx, kIntegerArgRegs[i], args[i]))
      return false;

  sp &= ~0xfull;
  sp -= 8;
  if (!WriteStackValue(*process_sp, sp, return_addr))
    return false;

  return WriteRegister(*reg_ctx, "SP", sp) &&
         WriteRegister(*reg_ctx, "PC", func_addr);
}

bool ABIBedrock::PrepareTrivialCall(Thread &thread, addr_t sp, addr_t func_addr,
                                    addr_t return_addr, llvm::Type &prototype,
                                    llvm::ArrayRef<CallArgument> args) const {
  auto *function_type = llvm::dyn_cast<llvm::FunctionType>(&prototype);
  if (!function_type)
    return false;

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  ProcessSP process_sp = thread.GetProcess();
  if (!reg_ctx || !process_sp)
    return false;

  struct StackArg {
    addr_t Value;
    addr_t Offset;
  };
  llvm::SmallVector<StackArg, 4> stack_args;
  size_t next_int = 0;
  size_t next_ptr = 0;
  size_t next_float = 0;
  addr_t stack_offset = 0;
  addr_t host_data_sp = sp & ~0xfull;

  for (size_t i = 0; i < args.size(); ++i) {
    const CallArgument &arg = args[i];
    addr_t arg_value = arg.value;
    llvm::Type *param_type = i < function_type->getNumParams()
                                 ? function_type->getParamType(i)
                                 : nullptr;
    if (!param_type && !function_type->isVarArg())
      return false;

    if (arg.type == CallArgument::HostPointer) {
      host_data_sp -= llvm::alignTo(arg.size, 16);
      Status error;
      if (process_sp->WriteMemory(host_data_sp, arg.data_up.get(), arg.size,
                                  error) < arg.size ||
          error.Fail())
        return false;
      arg_value = host_data_sp;
      param_type = nullptr;
    }

    bool must_stack =
        i >= function_type->getNumParams() && function_type->isVarArg();
    bool is_ptr = !must_stack && param_type && IsPointerArgument(*param_type);
    bool is_float = !must_stack && param_type && IsFloatArgument(*param_type);

    if (is_ptr && next_ptr < std::size(kPointerArgRegs)) {
      if (!WriteRegister(*reg_ctx, kPointerArgRegs[next_ptr++], arg_value))
        return false;
      continue;
    }

    if (is_float && next_float < std::size(kFloatArgRegs)) {
      if (!WriteRegister(*reg_ctx, kFloatArgRegs[next_float++], arg_value))
        return false;
      continue;
    }

    if (!is_ptr && !is_float && !must_stack &&
        next_int < std::size(kIntegerArgRegs)) {
      if (!WriteRegister(*reg_ctx, kIntegerArgRegs[next_int++], arg_value))
        return false;
      continue;
    }

    stack_args.push_back({arg_value, stack_offset});
    stack_offset += 16;
  }

  addr_t entry_sp = (host_data_sp - stack_offset) & ~0xfull;
  entry_sp -= 8;
  if (!WriteStackValue(*process_sp, entry_sp, return_addr))
    return false;

  for (const StackArg &arg : stack_args)
    if (!WriteStackValue(*process_sp, entry_sp + 8 + arg.Offset, arg.Value))
      return false;

  return WriteRegister(*reg_ctx, "SP", entry_sp) &&
         WriteRegister(*reg_ctx, "PC", func_addr);
}

bool ABIBedrock::GetArgumentValues(Thread &thread, ValueList &values) const {
  return false;
}

Status ABIBedrock::SetReturnValueObject(StackFrameSP &frame_sp,
                                        ValueObjectSP &new_value_sp) {
  Status result;
  if (!new_value_sp)
    return Status::FromErrorString("Empty value object for return value.");

  CompilerType compiler_type = new_value_sp->GetCompilerType();
  if (!compiler_type)
    return Status::FromErrorString("Null clang type for return value.");

  RegisterContextSP reg_ctx_sp = frame_sp->GetThread()->GetRegisterContext();
  if (!reg_ctx_sp)
    return Status::FromErrorString("No register context.");

  DataExtractor data;
  size_t num_bytes = new_value_sp->GetData(data, result);
  if (result.Fail())
    return result;

  offset_t offset = 0;
  uint64_t raw_value = data.GetMaxU64(&offset, std::min<size_t>(num_bytes, 8));

  bool is_signed = false;
  if (compiler_type.IsPointerType())
    return WriteRegister(*reg_ctx_sp, "A0", raw_value)
               ? Status()
               : Status::FromErrorString("Couldn't write pointer return A0.");

  bool is_complex = false;
  if (compiler_type.IsFloatingPointType(is_complex) && !is_complex)
    return WriteRegister(*reg_ctx_sp, "F0", raw_value)
               ? Status()
               : Status::FromErrorString("Couldn't write floating return F0.");

  if (!compiler_type.IsIntegerOrEnumerationType(is_signed))
    return Status::FromErrorString("Unsupported Bedrock return type.");

  if (num_bytes > 16)
    return Status::FromErrorString("Unsupported Bedrock integer return size.");

  if (!WriteRegister(*reg_ctx_sp, "D0", raw_value))
    return Status::FromErrorString("Couldn't write integer return D0.");

  if (num_bytes > 8) {
    raw_value = data.GetMaxU64(&offset, std::min<size_t>(num_bytes - 8, 8));
    if (!WriteRegister(*reg_ctx_sp, "D1", raw_value))
      return Status::FromErrorString("Couldn't write integer return D1.");
  }

  return Status();
}

ValueObjectSP
ABIBedrock::GetReturnValueObjectSimple(Thread &thread,
                                       CompilerType &compiler_type) const {
  ValueObjectSP return_valobj_sp;
  if (!compiler_type)
    return return_valobj_sp;

  RegisterContextSP reg_ctx = thread.GetRegisterContext();
  if (!reg_ctx)
    return return_valobj_sp;

  Value value;
  value.SetCompilerType(compiler_type);

  const uint32_t type_flags = compiler_type.GetTypeInfo();
  const size_t byte_size =
      llvm::expectedToOptional(compiler_type.GetByteSize(&thread)).value_or(0);

  if (type_flags & eTypeIsPointer) {
    value.GetScalar() = ReadRegisterUnsigned(*reg_ctx, "A0");
    value.SetValueType(Value::ValueType::Scalar);
    return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  }

  if (type_flags & eTypeIsInteger) {
    uint64_t raw_value = ReadRegisterUnsigned(*reg_ctx, "D0");
    uint64_t raw_high_value =
        byte_size > 8 ? ReadRegisterUnsigned(*reg_ctx, "D1") : 0;
    if (!SetSizedInteger(value.GetScalar(), raw_value, raw_high_value,
                         byte_size, type_flags & eTypeIsSigned))
      return return_valobj_sp;
    value.SetValueType(Value::ValueType::Scalar);
    return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  }

  if (type_flags & eTypeIsFloat) {
    bool is_complex = false;
    if (!compiler_type.IsFloatingPointType(is_complex) || is_complex)
      return return_valobj_sp;
    uint64_t raw_value = ReadRegisterUnsigned(*reg_ctx, "F0");
    if (!SetSizedFloat(value.GetScalar(), raw_value, byte_size))
      return return_valobj_sp;
    value.SetValueType(Value::ValueType::Scalar);
    return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                          value, ConstString(""));
  }

  return return_valobj_sp;
}

ValueObjectSP ABIBedrock::GetReturnValueObjectImpl(Thread &thread,
                                                   CompilerType &type) const {
  return GetReturnValueObjectSimple(thread, type);
}

UnwindPlanSP ABIBedrock::CreateFunctionEntryUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf::sp, 8);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf::pc, -8, false);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf::sp, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("bedrock function-entry unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

UnwindPlanSP ABIBedrock::CreateDefaultUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf::sp, 8);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf::pc, -8, false);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf::sp, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("bedrock default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

bool ABIBedrock::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABIBedrock::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info || !reg_info->name)
    return false;

  return llvm::StringSwitch<bool>(reg_info->name)
      .Cases({"SP", "sp"}, true)
      .Cases({"A7", "fp", "a7"}, true)
      .Cases({"D6", "D7", "d6", "d7"}, true)
      .Cases({"A6", "a6"}, true)
      .Cases({"F8", "F9", "F10", "F11"}, true)
      .Cases({"F12", "F13", "F14", "F15"}, true)
      .Cases({"f8", "f9", "f10", "f11"}, true)
      .Cases({"f12", "f13", "f14", "f15"}, true)
      .Default(false);
}

void ABIBedrock::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "Bedrock C ABI",
                                CreateInstance);
}

void ABIBedrock::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
