//=- WebAssemblySubtarget.h - Define Subtarget for the WebAssembly -*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the WebAssembly-specific subclass of
/// TargetSubtarget.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYSUBTARGET_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYSUBTARGET_H

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssemblyFrameLowering.h"
#include "WebAssemblyISelLowering.h"
#include "WebAssemblyInstrInfo.h"
#include "WebAssemblySelectionDAGInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <cstdint>
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "WebAssemblyGenSubtargetInfo.inc"

namespace llvm {

// Defined in WebAssemblyGenSubtargetInfo.inc.
extern const SubtargetFeatureKV
    WebAssemblyFeatureKV[WebAssembly::NumSubtargetFeatures];

enum class WasmTuneKind : uint8_t {
  Generic,
  U2AAPCS64,
  U2SysV,
  M3,
};

struct WasmExecutionProfile {
  WasmTuneKind Kind = WasmTuneKind::Generic;

  unsigned FPRingCapacity = 0;
  unsigned IntRingCapacity = 0;
  // Physical register-ring capacity is used for dumps and diagnostics.
  // Tuning boundaries are allowed to be softer when a real engine can absorb
  // mild transient pressure more cheaply than extra local traffic.
  unsigned IntTuningBoundary = 0;

  bool HasRegisterRing = false;
  bool HasUWVM2RegisterRingModel = false;
  bool HasM3SlotProviderModel = false;

  unsigned DispatchCost = 1;
  unsigned SpillCost = 4;
  unsigned FillCost = 4;
  unsigned LocalGetCost = 2;
  unsigned LocalSetCost = 2;
  unsigned TeeCost = 2;
  unsigned CodeSizeCost = 1;
  unsigned CriticalPathCost = 1;

  unsigned PreferredFPBank = 1;
  unsigned PreferredIntBank = 1;
  unsigned StrictFPAccumBoundary = 0;
};

class WebAssemblySubtarget final : public WebAssemblyGenSubtargetInfo {
  enum SIMDEnum {
    NoSIMD,
    SIMD128,
    RelaxedSIMD,
  } SIMDLevel = NoSIMD;

  bool HasAtomics = false;
  bool HasBulkMemory = false;
  bool HasBulkMemoryOpt = false;
  bool HasCallIndirectOverlong = false;
  bool HasCompactImports = false;
  bool HasExceptionHandling = false;
  bool HasExtendedConst = false;
  bool HasFP16 = false;
  bool HasGC = false;
  bool HasCooperativeMultithreading = false;
  bool HasLibcallThreadContext = false;
  bool HasMultiMemory = false;
  bool HasMultivalue = false;
  bool HasMutableGlobals = false;
  bool HasNontrappingFPToInt = false;
  bool HasReferenceTypes = false;
  bool HasRelaxedAtomics = false;
  bool HasSignExt = false;
  bool HasTailCall = false;
  bool HasWideArithmetic = false;

  std::string TuneCPUName;
  WasmExecutionProfile ExecProfile;

  /// What processor and OS we're targeting.
  Triple TargetTriple;

  WebAssemblyFrameLowering FrameLowering;
  WebAssemblyInstrInfo InstrInfo;
  WebAssemblySelectionDAGInfo TSInfo;
  WebAssemblyTargetLowering TLInfo;

  std::unique_ptr<CallLowering> CallLoweringInfo;
  std::unique_ptr<InstructionSelector> InstSelector;
  std::unique_ptr<LegalizerInfo> Legalizer;
  std::unique_ptr<RegisterBankInfo> RegBankInfo;

  WebAssemblySubtarget &initializeSubtargetDependencies(StringRef CPU,
                                                        StringRef TuneCPU,
                                                        StringRef FS);

public:
  /// This constructor initializes the data members to match that
  /// of the specified triple.
  WebAssemblySubtarget(const Triple &TT, const std::string &CPU,
                       const std::string &TuneCPU, const std::string &FS,
                       const TargetMachine &TM);

  const WebAssemblySelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
  const WebAssemblyFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const WebAssemblyTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const WebAssemblyInstrInfo *getInstrInfo() const override {
    return &InstrInfo;
  }
  const WebAssemblyRegisterInfo *getRegisterInfo() const override {
    return &getInstrInfo()->getRegisterInfo();
  }
  const Triple &getTargetTriple() const { return TargetTriple; }
  bool enableAtomicExpand() const override;
  bool enableIndirectBrExpand() const override { return true; }
  bool enableMachineScheduler() const override;
  bool useAA() const override;

  // Predicates used by WebAssemblyInstrInfo.td.
  bool hasAddr64() const { return TargetTriple.isArch64Bit(); }
  bool hasAtomics() const { return HasAtomics; }
  bool hasBulkMemory() const { return HasBulkMemory; }
  bool hasBulkMemoryOpt() const { return HasBulkMemoryOpt; }
  bool hasCallIndirectOverlong() const { return HasCallIndirectOverlong; }
  bool hasCompactImports() const { return HasCompactImports; }
  bool hasExceptionHandling() const { return HasExceptionHandling; }
  bool hasExtendedConst() const { return HasExtendedConst; }
  bool hasFP16() const { return HasFP16; }
  bool hasGC() const { return HasGC; }
  bool hasCooperativeMultithreading() const {
    return HasCooperativeMultithreading;
  }
  bool hasLibcallThreadContext() const { return HasLibcallThreadContext; }
  bool hasMultiMemory() const { return HasMultiMemory; }
  bool hasMultivalue() const { return HasMultivalue; }
  bool hasMutableGlobals() const { return HasMutableGlobals; }
  bool hasNontrappingFPToInt() const { return HasNontrappingFPToInt; }
  bool hasReferenceTypes() const { return HasReferenceTypes; }
  bool hasRelaxedAtomics() const { return HasRelaxedAtomics; }
  bool hasRelaxedSIMD() const { return SIMDLevel >= RelaxedSIMD; }
  bool hasSignExt() const { return HasSignExt; }
  bool hasSIMD128() const { return SIMDLevel >= SIMD128; }
  bool hasTailCall() const { return HasTailCall; }
  bool hasWideArithmetic() const { return HasWideArithmetic; }
  WasmTuneKind getTuneKind() const { return ExecProfile.Kind; }
  StringRef getTuneCPUName() const { return TuneCPUName; }
  const WasmExecutionProfile &getExecutionProfile() const {
    return ExecProfile;
  }

  /// Parses features string setting specified subtarget options. Definition of
  /// function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  const CallLowering *getCallLowering() const override;
  InstructionSelector *getInstructionSelector() const override;
  const LegalizerInfo *getLegalizerInfo() const override;
  const RegisterBankInfo *getRegBankInfo() const override;
};

} // end namespace llvm

#endif
