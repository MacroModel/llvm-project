//===-- WebAssemblySubtarget.cpp - WebAssembly Subtarget Information ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the WebAssembly-specific subclass of
/// TargetSubtarget.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblySubtarget.h"
#include "GISel/WebAssemblyCallLowering.h"
#include "GISel/WebAssemblyLegalizerInfo.h"
#include "GISel/WebAssemblyRegisterBankInfo.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyInstrInfo.h"
#include "WebAssemblyTargetMachine.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-subtarget"

#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_TARGET_DESC
#include "WebAssemblyGenSubtargetInfo.inc"

static WasmExecutionProfile computeExecutionProfile(StringRef TuneCPU) {
  WasmExecutionProfile Profile;

  if (TuneCPU == "u2-aapcs64" || TuneCPU == "uwvm2-aapcs64" ||
      TuneCPU == "uwvm2-aarch64-aapcs64") {
    Profile.Kind = WasmTuneKind::U2AAPCS64;
    Profile.HasRegisterRing = true;
    Profile.HasUWVM2RegisterRingModel = true;
    Profile.FPRingCapacity = 8;
    Profile.IntRingCapacity = 5;
    Profile.IntTuningBoundary = 5;
    if (TuneCPU.starts_with("uwvm2"))
      Profile.StrictFPAccumBoundary = 2;
    Profile.PreferredFPBank = 8;
    Profile.PreferredIntBank = 5;
    Profile.SpillCost = 5;
    Profile.FillCost = 5;
    return Profile;
  }

  if (TuneCPU == "u2-sysv" || TuneCPU == "u2-x86_64-sysv") {
    Profile.Kind = WasmTuneKind::U2SysV;
    Profile.HasRegisterRing = true;
    Profile.HasUWVM2RegisterRingModel = true;
    Profile.FPRingCapacity = 8;
    Profile.FPTuningBoundary = 12;
    Profile.IntRingCapacity = 3;
    Profile.IntTuningBoundary = 7;
    Profile.StrictFPAccumBoundary = 2;
    Profile.PreferredFPBank = 8;
    Profile.PreferredIntBank = 5;
    Profile.SpillCost = 5;
    Profile.FillCost = 5;
    return Profile;
  }

  if (TuneCPU == "uwvm2" || TuneCPU == "uwvm2-int" ||
      TuneCPU == "uwvm2-sysv" || TuneCPU == "uwvm2-x86_64-sysv") {
    Profile.Kind = WasmTuneKind::U2SysV;
    Profile.HasRegisterRing = true;
    Profile.HasUWVM2RegisterRingModel = true;
    Profile.FPRingCapacity = 8;
    Profile.IntRingCapacity = 3;
    // UWVM2's delay_local and register-ring translator can absorb a little
    // more transient integer stack pressure than the physical SysV ring before
    // extra local traffic becomes profitable.
    Profile.FPTuningBoundary = 12;
    Profile.IntTuningBoundary = 7;
    Profile.StrictFPAccumBoundary = 2;
    Profile.PreferredFPBank = 8;
    Profile.PreferredIntBank = 5;
    Profile.SpillCost = 5;
    Profile.FillCost = 5;
    return Profile;
  }

  if (TuneCPU == "m3") {
    Profile.Kind = WasmTuneKind::M3;
    Profile.HasM3SlotProviderModel = true;
    Profile.FPRingCapacity = 1;
    Profile.IntRingCapacity = 1;
    Profile.PreferredFPBank = 1;
    Profile.PreferredIntBank = 1;
    Profile.LocalGetCost = 1;
    Profile.LocalSetCost = 1;
    Profile.SpillCost = 2;
    Profile.FillCost = 2;
    return Profile;
  }

  return Profile;
}

WebAssemblySubtarget &WebAssemblySubtarget::initializeSubtargetDependencies(
    StringRef CPU, StringRef TuneCPU, StringRef FS) {
  // Determine default and user-specified characteristics
  LLVM_DEBUG(llvm::dbgs() << "initializeSubtargetDependencies\n");

  if (CPU.empty())
    CPU = "generic";
  if (TuneCPU.empty())
    TuneCPU = CPU;

  // Feature legality is controlled by CPU/FS. TuneCPU only selects backend
  // execution-profile heuristics and must not enable post-MVP features.
  ParseSubtargetFeatures(CPU, /*TuneCPU*/ CPU, FS);
  TuneCPUName = TuneCPU.str();
  ExecProfile = computeExecutionProfile(TuneCPU);

  LLVM_DEBUG(llvm::dbgs() << "  cpu=" << CPU << " tune-cpu=" << TuneCPU
                          << " fp-ring=" << ExecProfile.FPRingCapacity
                          << " int-ring=" << ExecProfile.IntRingCapacity
                          << "\n");

  // WASIP3 uses cooperative multithreading, which implies using libcall
  // thread context.
  if (TargetTriple.getOS() == Triple::WASIp3) {
    HasCooperativeMultithreading = true;
    HasLibcallThreadContext = true;
  }

  FeatureBitset Bits = getFeatureBits();

  // bulk-memory implies bulk-memory-opt
  if (HasBulkMemory) {
    HasBulkMemoryOpt = true;
    Bits.set(WebAssembly::FeatureBulkMemoryOpt);
  }

  // gc implies reference-types
  if (HasGC) {
    HasReferenceTypes = true;
  }

  // reference-types implies call-indirect-overlong
  if (HasReferenceTypes) {
    HasCallIndirectOverlong = true;
    Bits.set(WebAssembly::FeatureCallIndirectOverlong);
  }

  // In case we changed any bits, update `MCSubtargetInfo`'s `FeatureBitset`.
  setFeatureBits(Bits);

  return *this;
}

WebAssemblySubtarget::WebAssemblySubtarget(const Triple &TT,
                                           const std::string &CPU,
                                           const std::string &TuneCPU,
                                           const std::string &FS,
                                           const TargetMachine &TM)
    : WebAssemblyGenSubtargetInfo(TT, CPU, TuneCPU.empty() ? CPU : TuneCPU, FS),
      TargetTriple(TT),
      InstrInfo(initializeSubtargetDependencies(CPU, TuneCPU, FS)),
      TLInfo(TM, *this) {
  CallLoweringInfo.reset(new WebAssemblyCallLowering(*getTargetLowering()));
  Legalizer.reset(new WebAssemblyLegalizerInfo(*this));
  auto *RBI = new WebAssemblyRegisterBankInfo(*getRegisterInfo());
  RegBankInfo.reset(RBI);

  InstSelector.reset(createWebAssemblyInstructionSelector(
      *static_cast<const WebAssemblyTargetMachine *>(&TM), *this, *RBI));
}

bool WebAssemblySubtarget::enableAtomicExpand() const {
  // If atomics are disabled, atomic ops are lowered instead of expanded
  return hasAtomics();
}

bool WebAssemblySubtarget::enableMachineScheduler() const {
  // Disable the MachineScheduler for now. Even with ShouldTrackPressure set and
  // enableMachineSchedDefaultSched overridden, it appears to have an overall
  // negative effect for the kinds of register optimizations we're doing.
  return false;
}

bool WebAssemblySubtarget::useAA() const { return true; }

const CallLowering *WebAssemblySubtarget::getCallLowering() const {
  return CallLoweringInfo.get();
}

InstructionSelector *WebAssemblySubtarget::getInstructionSelector() const {
  return InstSelector.get();
}

const LegalizerInfo *WebAssemblySubtarget::getLegalizerInfo() const {
  return Legalizer.get();
}

const RegisterBankInfo *WebAssemblySubtarget::getRegBankInfo() const {
  return RegBankInfo.get();
}
