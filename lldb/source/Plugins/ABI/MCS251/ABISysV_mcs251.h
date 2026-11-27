//===-- ABISysV_mcs251.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_MCS251_ABISYSV_MCS251_H
#define LLDB_SOURCE_PLUGINS_ABI_MCS251_ABISYSV_MCS251_H

#include "lldb/Target/ABI.h"
#include "lldb/Utility/Flags.h"
#include "lldb/lldb-private.h"

class ABISysV_mcs251 : public lldb_private::RegInfoBasedABI {
public:
  ~ABISysV_mcs251() override = default;

  size_t GetRedZoneSize() const override;

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  lldb::UnwindPlanSP CreateFunctionEntryUnwindPlan() override;

  lldb::UnwindPlanSP CreateDefaultUnwindPlan() override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    return (cfa & 0xFFFF0000ULL) == 0;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override {
    return (pc & ~0x00FFFFFFULL) == 0;
  }

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  static void Initialize();
  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "sysv-mcs251"; }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  void CreateRegisterMapIfNeeded();

  lldb::ValueObjectSP
  GetReturnValueObjectSimple(lldb_private::Thread &thread,
                             lldb_private::CompilerType &ast_type) const;

  bool RegisterIsCalleeSaved(const lldb_private::RegisterInfo *reg_info);

private:
  ABISysV_mcs251(lldb::ProcessSP process_sp,
                 std::unique_ptr<lldb_private::MCRegisterInfo> info_up)
      : lldb_private::RegInfoBasedABI(std::move(process_sp),
                                      std::move(info_up)) {}
};

#endif // LLDB_SOURCE_PLUGINS_ABI_MCS251_ABISYSV_MCS251_H
