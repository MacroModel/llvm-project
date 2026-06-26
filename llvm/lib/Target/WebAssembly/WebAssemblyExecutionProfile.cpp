//===-- WebAssemblyExecutionProfile.cpp - Execution tuning helpers --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WebAssemblyExecutionProfile.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <algorithm>

using namespace llvm;

WasmValueClass llvm::classifyWasmReg(Register Reg,
                                     const MachineRegisterInfo &MRI) {
  if (!Reg.isVirtual())
    return WasmValueClass::Other;

  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  if (RC == &WebAssembly::I32RegClass || RC == &WebAssembly::I64RegClass)
    return WasmValueClass::Int;
  if (RC == &WebAssembly::F32RegClass || RC == &WebAssembly::F64RegClass)
    return WasmValueClass::FP;
  if (RC == &WebAssembly::FUNCREFRegClass ||
      RC == &WebAssembly::EXTERNREFRegClass ||
      RC == &WebAssembly::EXNREFRegClass)
    return WasmValueClass::Ref;
  if (RC == &WebAssembly::V128RegClass)
    return WasmValueClass::V128;

  return WasmValueClass::Other;
}

bool llvm::isWasmFPReg(Register Reg, const MachineRegisterInfo &MRI) {
  return classifyWasmReg(Reg, MRI) == WasmValueClass::FP;
}

bool llvm::isWasmIntReg(Register Reg, const MachineRegisterInfo &MRI) {
  return classifyWasmReg(Reg, MRI) == WasmValueClass::Int;
}

static void addResultValue(WasmExecPressureResult &R, WasmValueClass VC) {
  switch (VC) {
  case WasmValueClass::Int:
    ++R.ResultInt;
    R.PeakInt = std::max(R.PeakInt, R.ResultInt);
    break;
  case WasmValueClass::FP:
    ++R.ResultFP;
    R.PeakFP = std::max(R.PeakFP, R.ResultFP);
    break;
  case WasmValueClass::Ref:
    ++R.ResultRef;
    R.PeakRef = std::max(R.PeakRef, R.ResultRef);
    break;
  case WasmValueClass::V128:
    ++R.ResultV128;
    R.PeakV128 = std::max(R.PeakV128, R.ResultV128);
    break;
  case WasmValueClass::Other:
    break;
  }
}

static void addLocalGetLeaf(WasmExecPressureResult &R, WasmValueClass VC) {
  if (VC == WasmValueClass::Other)
    return;
  addResultValue(R, VC);
  ++R.EstimatedLocalGets;
}

static void mergeCosts(WasmExecPressureResult &Dst,
                       const WasmExecPressureResult &Src) {
  Dst.EstimatedRingSpills += Src.EstimatedRingSpills;
  Dst.EstimatedRingFills += Src.EstimatedRingFills;
  Dst.EstimatedLocalGets += Src.EstimatedLocalGets;
  Dst.EstimatedLocalSets += Src.EstimatedLocalSets;
  Dst.EstimatedTees += Src.EstimatedTees;
  Dst.EstimatedDispatch += Src.EstimatedDispatch;
  Dst.Nodes += Src.Nodes;
  Dst.HitLimit |= Src.HitLimit;
}

static void updatePeakWithChild(WasmExecPressureResult &R, unsigned CurFP,
                                unsigned CurInt, unsigned CurRef,
                                unsigned CurV128,
                                const WasmExecPressureResult &Child) {
  R.PeakFP = std::max(R.PeakFP, CurFP + Child.PeakFP);
  R.PeakInt = std::max(R.PeakInt, CurInt + Child.PeakInt);
  R.PeakRef = std::max(R.PeakRef, CurRef + Child.PeakRef);
  R.PeakV128 = std::max(R.PeakV128, CurV128 + Child.PeakV128);
}

static bool isTeeOpcode(unsigned Opcode) {
  switch (Opcode) {
  case WebAssembly::TEE_I32:
  case WebAssembly::TEE_I64:
  case WebAssembly::TEE_F32:
  case WebAssembly::TEE_F64:
  case WebAssembly::TEE_V128:
  case WebAssembly::TEE_EXTERNREF:
  case WebAssembly::TEE_FUNCREF:
  case WebAssembly::TEE_EXNREF:
    return true;
  default:
    return false;
  }
}

static WasmExecPressureResult estimateStackifiedTreePressureImpl(
    MachineInstr &Root, Register CandidateReg, MachineInstr *CandidateDef,
    const MachineRegisterInfo &MRI, const WebAssemblyFunctionInfo &MFI,
    unsigned NodeLimit, bool ResultStackified,
    SmallPtrSetImpl<const MachineInstr *> &Visiting) {
  WasmExecPressureResult R;
  if (NodeLimit == 0) {
    R.HitLimit = true;
    return R;
  }
  if (!Visiting.insert(&Root).second) {
    R.HitLimit = true;
    return R;
  }

  R.Nodes = 1;
  R.EstimatedDispatch = 1;
  if (isTeeOpcode(Root.getOpcode()))
    ++R.EstimatedTees;

  unsigned CurFP = 0;
  unsigned CurInt = 0;
  unsigned CurRef = 0;
  unsigned CurV128 = 0;

  for (MachineOperand &MO : reverse(Root.explicit_uses())) {
    if (R.Nodes >= NodeLimit) {
      R.HitLimit = true;
      break;
    }

    if (!MO.isReg() || MO.isUndef())
      continue;

    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;

    WasmValueClass VC = classifyWasmReg(Reg, MRI);
    WasmExecPressureResult Child;
    if (CandidateDef && Reg == CandidateReg) {
      Child = estimateStackifiedTreePressureImpl(
          *CandidateDef, CandidateReg, CandidateDef, MRI, MFI,
          NodeLimit - R.Nodes, /*ResultStackified=*/true, Visiting);
    } else if (MFI.isVRegStackified(Reg)) {
      if (MachineInstr *Def = MRI.getUniqueVRegDef(Reg)) {
        Child = estimateStackifiedTreePressureImpl(
            *Def, CandidateReg, CandidateDef, MRI, MFI, NodeLimit - R.Nodes,
            /*ResultStackified=*/true, Visiting);
      } else {
        addLocalGetLeaf(Child, VC);
      }
    } else {
      addLocalGetLeaf(Child, VC);
    }

    updatePeakWithChild(R, CurFP, CurInt, CurRef, CurV128, Child);
    CurFP += Child.ResultFP;
    CurInt += Child.ResultInt;
    CurRef += Child.ResultRef;
    CurV128 += Child.ResultV128;
    mergeCosts(R, Child);
  }

  R.ResultFP = 0;
  R.ResultInt = 0;
  R.ResultRef = 0;
  R.ResultV128 = 0;

  for (MachineOperand &MO : Root.defs()) {
    if (!MO.isReg())
      continue;

    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;

    addResultValue(R, classifyWasmReg(Reg, MRI));
    if (!ResultStackified && !MFI.isVRegStackified(Reg))
      ++R.EstimatedLocalSets;
  }

  Visiting.erase(&Root);
  return R;
}

WasmExecPressureResult llvm::estimateStackifiedTreePressure(
    MachineInstr &Root, const MachineRegisterInfo &MRI,
    const WebAssemblyFunctionInfo &MFI, unsigned NodeLimit) {
  SmallPtrSet<const MachineInstr *, 16> Visiting;
  return estimateStackifiedTreePressureImpl(
      Root, Register(), nullptr, MRI, MFI, NodeLimit,
      /*ResultStackified=*/false, Visiting);
}

WasmExecPressureResult llvm::estimateStackifiedTreePressure(
    MachineInstr &Root, Register CandidateReg, MachineInstr &CandidateDef,
    const MachineRegisterInfo &MRI, const WebAssemblyFunctionInfo &MFI,
    unsigned NodeLimit) {
  SmallPtrSet<const MachineInstr *, 16> Visiting;
  return estimateStackifiedTreePressureImpl(
      Root, CandidateReg, &CandidateDef, MRI, MFI, NodeLimit,
      /*ResultStackified=*/false, Visiting);
}

int64_t llvm::scorePressure(const WasmExecutionProfile &P,
                            const WasmExecPressureResult &R) {
  int64_t Score = 0;

  Score += int64_t(P.DispatchCost) * R.EstimatedDispatch;
  Score += int64_t(P.LocalGetCost) * R.EstimatedLocalGets;
  Score += int64_t(P.LocalSetCost) * R.EstimatedLocalSets;
  Score += int64_t(P.TeeCost) * R.EstimatedTees;

  if (P.HasRegisterRing) {
    unsigned FPOverflow = P.FPRingCapacity && R.PeakFP > P.FPRingCapacity
                              ? R.PeakFP - P.FPRingCapacity
                              : 0;
    unsigned IntOverflow = P.IntRingCapacity && R.PeakInt > P.IntRingCapacity
                               ? R.PeakInt - P.IntRingCapacity
                               : 0;
    Score += int64_t(P.SpillCost + P.FillCost) * (FPOverflow + IntOverflow);
  }

  if (P.HasM3SlotProviderModel) {
    Score += int64_t(R.PeakFP > 1 ? (R.PeakFP - 1) * 4 : 0);
    Score += int64_t(R.PeakInt > 1 ? (R.PeakInt - 1) * 2 : 0);
  }

  return Score;
}
