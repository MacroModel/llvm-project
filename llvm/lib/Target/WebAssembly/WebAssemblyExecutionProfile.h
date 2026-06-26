//===-- WebAssemblyExecutionProfile.h - Execution tuning helpers -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYEXECUTIONPROFILE_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYEXECUTIONPROFILE_H

#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include <cstdint>

namespace llvm {

class WebAssemblyFunctionInfo;

enum class WasmValueClass : uint8_t {
  Other,
  Int,
  FP,
  Ref,
  V128,
};

struct WasmExecPressureResult {
  unsigned PeakFP = 0;
  unsigned PeakInt = 0;
  unsigned PeakRef = 0;
  unsigned PeakV128 = 0;

  unsigned ResultFP = 0;
  unsigned ResultInt = 0;
  unsigned ResultRef = 0;
  unsigned ResultV128 = 0;

  unsigned EstimatedRingSpills = 0;
  unsigned EstimatedRingFills = 0;
  unsigned EstimatedLocalGets = 0;
  unsigned EstimatedLocalSets = 0;
  unsigned EstimatedTees = 0;
  unsigned EstimatedDispatch = 0;

  unsigned Nodes = 0;
  bool HitLimit = false;

  int64_t Score = 0;
};

WasmValueClass classifyWasmReg(Register Reg, const MachineRegisterInfo &MRI);

bool isWasmFPReg(Register Reg, const MachineRegisterInfo &MRI);
bool isWasmIntReg(Register Reg, const MachineRegisterInfo &MRI);

WasmExecPressureResult estimateStackifiedTreePressure(
    MachineInstr &Root, const MachineRegisterInfo &MRI,
    const WebAssemblyFunctionInfo &MFI, unsigned NodeLimit);

WasmExecPressureResult estimateStackifiedTreePressure(
    MachineInstr &Root, Register CandidateReg, MachineInstr &CandidateDef,
    const MachineRegisterInfo &MRI, const WebAssemblyFunctionInfo &MFI,
    unsigned NodeLimit);

// Scores are profile-local: they are intended for before/after comparisons
// within one execution profile, not for ranking different profiles.
int64_t scorePressure(const WasmExecutionProfile &Profile,
                      const WasmExecPressureResult &R);

} // end namespace llvm

#endif
