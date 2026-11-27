//===-- ABISysV_mcs251.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_mcs251.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"
#include "lldb/ValueObject/ValueObjectMemory.h"
#include "lldb/ValueObject/ValueObjectRegister.h"

#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_mcs251, ABIMCS251)

// DWARF register numbers matching llvm/lib/Target/MCS251/MCS251RegisterInfo.td.
enum dwarf_regnums {
  dwarf_r0 = 0,
  dwarf_r1 = 1,
  dwarf_r2 = 2,
  dwarf_r3 = 3,
  dwarf_r4 = 4,
  dwarf_r5 = 5,
  dwarf_r6 = 6,
  dwarf_r7 = 7,
  dwarf_r8 = 8,
  dwarf_r9 = 9,
  dwarf_r10 = 10,
  dwarf_r11 = 11,
  dwarf_r12 = 12,
  dwarf_r13 = 13,
  dwarf_r14 = 14,
  dwarf_r15 = 15,
  dwarf_dr16 = 16, // frame anchor (FP)
  dwarf_dpx = 56,  // dr56 (DPX)
  dwarf_a = 58,
  dwarf_b = 59,
  dwarf_spx = 60,  // dr60 (SPX)
  dwarf_dpl = 61,
  dwarf_dph = 62,
  dwarf_dptr = 63,
  dwarf_pc = 64,
  dwarf_psw = 65,
};

#define MCS251_REG8(name, alt, dwarf, generic, off)                            \
  {name, alt, 1, off, eEncodingUint, eFormatHex,                               \
   {dwarf, dwarf, generic, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},          \
   nullptr, nullptr, nullptr}

#define MCS251_REG16(name, alt, dwarf, generic, off)                           \
  {name, alt, 2, off, eEncodingUint, eFormatHex,                               \
   {dwarf, dwarf, generic, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},          \
   nullptr, nullptr, nullptr}

#define MCS251_REG32(name, alt, dwarf, generic, off)                           \
  {name, alt, 4, off, eEncodingUint, eFormatHex,                               \
   {dwarf, dwarf, generic, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},          \
   nullptr, nullptr, nullptr}

static const RegisterInfo g_register_infos[] = {
    MCS251_REG8("r0", "", dwarf_r0, LLDB_INVALID_REGNUM, 0),
    MCS251_REG8("r1", "", dwarf_r1, LLDB_INVALID_REGNUM, 1),
    MCS251_REG8("r2", "", dwarf_r2, LLDB_INVALID_REGNUM, 2),
    MCS251_REG8("r3", "", dwarf_r3, LLDB_INVALID_REGNUM, 3),
    MCS251_REG8("r4", "", dwarf_r4, LLDB_INVALID_REGNUM, 4),
    MCS251_REG8("r5", "", dwarf_r5, LLDB_INVALID_REGNUM, 5),
    MCS251_REG8("r6", "", dwarf_r6, LLDB_INVALID_REGNUM, 6),
    MCS251_REG8("r7", "", dwarf_r7, LLDB_INVALID_REGNUM, 7),
    MCS251_REG8("r8", "", dwarf_r8, LLDB_INVALID_REGNUM, 8),
    MCS251_REG8("r9", "", dwarf_r9, LLDB_INVALID_REGNUM, 9),
    MCS251_REG8("r10", "", dwarf_r10, LLDB_INVALID_REGNUM, 10),
    MCS251_REG8("r11", "", dwarf_r11, LLDB_INVALID_REGNUM, 11),
    MCS251_REG8("r12", "", dwarf_r12, LLDB_INVALID_REGNUM, 12),
    MCS251_REG8("r13", "", dwarf_r13, LLDB_INVALID_REGNUM, 13),
    MCS251_REG8("r14", "", dwarf_r14, LLDB_INVALID_REGNUM, 14),
    MCS251_REG8("r15", "", dwarf_r15, LLDB_INVALID_REGNUM, 15),
    MCS251_REG32("dr16", "fp", dwarf_dr16, LLDB_REGNUM_GENERIC_FP, 16),
    MCS251_REG32("dpx", "dr56", dwarf_dpx, LLDB_INVALID_REGNUM, 20),
    MCS251_REG32("spx", "sp", dwarf_spx, LLDB_REGNUM_GENERIC_SP, 24),
    MCS251_REG8("a", "acc", dwarf_a, LLDB_INVALID_REGNUM, 28),
    MCS251_REG8("b", "", dwarf_b, LLDB_INVALID_REGNUM, 29),
    MCS251_REG8("dpl", "", dwarf_dpl, LLDB_REGNUM_GENERIC_ARG1, 30),
    MCS251_REG8("dph", "", dwarf_dph, LLDB_INVALID_REGNUM, 31),
    MCS251_REG16("dptr", "", dwarf_dptr, LLDB_INVALID_REGNUM, 32),
    MCS251_REG32("pc", "", dwarf_pc, LLDB_REGNUM_GENERIC_PC, 34),
    MCS251_REG8("psw", "flags", dwarf_psw, LLDB_REGNUM_GENERIC_FLAGS, 38),
};

#undef MCS251_REG8
#undef MCS251_REG16
#undef MCS251_REG32

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);

const lldb_private::RegisterInfo *
ABISysV_mcs251::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_mcs251::GetRedZoneSize() const { return 0; }

ABISP
ABISysV_mcs251::CreateInstance(lldb::ProcessSP process_sp,
                               const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::mcs251) {
    return ABISP(
        new ABISysV_mcs251(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABISysV_mcs251::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                        lldb::addr_t pc, lldb::addr_t ra,
                                        llvm::ArrayRef<addr_t> args) const {
  return false;
}

bool ABISysV_mcs251::GetArgumentValues(Thread &thread,
                                       ValueList &values) const {
  return false;
}

Status ABISysV_mcs251::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                            lldb::ValueObjectSP &new_value_sp) {
  return Status();
}

ValueObjectSP ABISysV_mcs251::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

ValueObjectSP ABISysV_mcs251::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  return GetReturnValueObjectSimple(thread, return_compiler_type);
}

// MCS-251 stack grows UPWARD: `ecall` pushes a 3-byte return address onto
// [SPX_caller + 1 .. SPX_caller + 3] and leaves SPX = SPX_caller + 3.
// Therefore at function entry, CFA = SPX - 3, caller SPX = CFA, and the
// 24-bit return PC lives at [CFA + 1 .. CFA + 3] (stored at CFA + 1).
UnwindPlanSP ABISysV_mcs251::CreateFunctionEntryUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_spx, -3);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, 1, true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_spx, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("mcs251 at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

UnwindPlanSP ABISysV_mcs251::CreateDefaultUnwindPlan() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_spx, -3);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, 1, true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_spx, 0, true);
  row.SetRegisterLocationToUnspecified(dwarf_dr16, true);
  row.SetUnspecifiedRegistersAreUndefined(true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("mcs251 default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

bool ABISysV_mcs251::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

// Route B stack-argument ABI: DR12 (r12..r15), WR8 (r8..r9), frame anchor DR16,
// and SPX are callee-saved; r0..r7, a, b, dpl, dph, dptr, dpx, psw are volatile.
bool ABISysV_mcs251::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info || !reg_info->name)
    return false;
  llvm::StringRef Name(reg_info->name);
  return Name == "r8" || Name == "r9" || Name == "r12" || Name == "r13" ||
         Name == "r14" || Name == "r15" || Name == "wr8" || Name == "wr12" ||
         Name == "wr14" || Name == "dr12" || Name == "dr16" || Name == "spx" ||
         Name == "dr60";
}

void ABISysV_mcs251::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for MCS-251 targets", CreateInstance);
}

void ABISysV_mcs251::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
