//===-- WebAssemblyRegStackify.cpp - Register Stackification --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a register stacking pass.
///
/// This pass reorders instructions to put register uses and defs in an order
/// such that they form single-use expression trees. Registers fitting this form
/// are then marked as "stackified", meaning references to them are replaced by
/// "push" and "pop" from the value stack.
///
/// This is primarily a code size optimization, since temporary values on the
/// value stack don't need to be named.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h" // for WebAssembly::ARGUMENT_*
#include "WebAssembly.h"
#include "WebAssemblyDebugValueManager.h"
#include "WebAssemblyExecutionProfile.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <limits>
using namespace llvm;

#define DEBUG_TYPE "wasm-reg-stackify"

static cl::opt<bool> WasmExecPressureDump(
    "wasm-exec-pressure-dump", cl::Hidden,
    cl::desc("Dump WebAssembly execution-profile pressure estimates"),
    cl::init(false));

static cl::opt<bool> WasmTuneStackify(
    "wasm-tune-stackify", cl::Hidden, cl::init(true),
    cl::desc("Enable execution-profile-aware WebAssembly stackification"));

static cl::opt<unsigned> WasmTuneStackifyNodeLimit(
    "wasm-tune-stackify-node-limit", cl::Hidden, cl::init(32),
    cl::desc("Maximum expression-tree nodes inspected by wasm tuning model"));

static cl::opt<unsigned> WasmTuneStackifyScoreHysteresis(
    "wasm-tune-stackify-score-hysteresis", cl::Hidden, cl::init(8),
    cl::desc("Minimum score regression needed to veto WebAssembly "
             "profile-aware stackification"));

namespace {
class WebAssemblyRegStackify final : public MachineFunctionPass {
  bool Optimize;

  StringRef getPassName() const override {
    return "WebAssembly Register Stackify";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    if (Optimize) {
      AU.addRequired<LiveIntervalsWrapperPass>();
      AU.addRequired<MachineDominatorTreeWrapperPass>();
    }
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addPreservedID(LiveVariablesID);
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyRegStackify(CodeGenOptLevel OptLevel)
      : MachineFunctionPass(ID), Optimize(OptLevel != CodeGenOptLevel::None) {}
  WebAssemblyRegStackify() : WebAssemblyRegStackify(CodeGenOptLevel::Default) {}
};
} // end anonymous namespace

char WebAssemblyRegStackify::ID = 0;
INITIALIZE_PASS(WebAssemblyRegStackify, DEBUG_TYPE,
                "Reorder instructions to use the WebAssembly value stack",
                false, false)

FunctionPass *llvm::createWebAssemblyRegStackify(CodeGenOptLevel OptLevel) {
  return new WebAssemblyRegStackify(OptLevel);
}

// Decorate the given instruction with implicit operands that enforce the
// expression stack ordering constraints for an instruction which is on
// the expression stack.
static void imposeStackOrdering(MachineInstr *MI) {
  // Write the opaque VALUE_STACK register.
  if (!MI->definesRegister(WebAssembly::VALUE_STACK, /*TRI=*/nullptr))
    MI->addOperand(MachineOperand::CreateReg(WebAssembly::VALUE_STACK,
                                             /*isDef=*/true,
                                             /*isImp=*/true));

  // Also read the opaque VALUE_STACK register.
  if (!MI->readsRegister(WebAssembly::VALUE_STACK, /*TRI=*/nullptr))
    MI->addOperand(MachineOperand::CreateReg(WebAssembly::VALUE_STACK,
                                             /*isDef=*/false,
                                             /*isImp=*/true));
}

// Convert an IMPLICIT_DEF instruction into an instruction which defines
// a constant zero value.
static void convertImplicitDefToConstZero(MachineInstr *MI,
                                          MachineRegisterInfo &MRI,
                                          const TargetInstrInfo *TII,
                                          MachineFunction &MF) {
  assert(MI->getOpcode() == TargetOpcode::IMPLICIT_DEF);

  const auto *RegClass = MRI.getRegClass(MI->getOperand(0).getReg());
  if (RegClass == &WebAssembly::I32RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_I32));
    MI->addOperand(MachineOperand::CreateImm(0));
  } else if (RegClass == &WebAssembly::I64RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_I64));
    MI->addOperand(MachineOperand::CreateImm(0));
  } else if (RegClass == &WebAssembly::F32RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_F32));
    auto *Val = cast<ConstantFP>(Constant::getNullValue(
        Type::getFloatTy(MF.getFunction().getContext())));
    MI->addOperand(MachineOperand::CreateFPImm(Val));
  } else if (RegClass == &WebAssembly::F64RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_F64));
    auto *Val = cast<ConstantFP>(Constant::getNullValue(
        Type::getDoubleTy(MF.getFunction().getContext())));
    MI->addOperand(MachineOperand::CreateFPImm(Val));
  } else if (RegClass == &WebAssembly::V128RegClass) {
    MI->setDesc(TII->get(WebAssembly::CONST_V128_I64x2));
    MI->addOperand(MachineOperand::CreateImm(0));
    MI->addOperand(MachineOperand::CreateImm(0));
  } else {
    llvm_unreachable("Unexpected reg class");
  }
}

// Determine whether a call to the callee referenced by
// MI->getOperand(CalleeOpNo) reads memory, writes memory, and/or has side
// effects.
static void queryCallee(const MachineInstr &MI, bool &Read, bool &Write,
                        bool &Effects, bool &StackPointer) {
  // All calls can use the stack pointer.
  StackPointer = true;

  const MachineOperand &MO = WebAssembly::getCalleeOp(MI);
  if (MO.isGlobal()) {
    const Constant *GV = MO.getGlobal();
    if (const auto *GA = dyn_cast<GlobalAlias>(GV))
      if (!GA->isInterposable())
        GV = GA->getAliasee();

    if (const auto *F = dyn_cast<Function>(GV)) {
      if (!F->doesNotThrow())
        Effects = true;
      if (F->doesNotAccessMemory())
        return;
      if (F->onlyReadsMemory()) {
        Read = true;
        return;
      }
    }
  }

  // Assume the worst.
  Write = true;
  Read = true;
  Effects = true;
}

// Determine whether MI reads memory, writes memory, has side effects,
// and/or uses the stack pointer value.
static void query(const MachineInstr &MI, bool &Read, bool &Write,
                  bool &Effects, bool &StackPointer) {
  assert(!MI.isTerminator());

  if (MI.isDebugInstr() || MI.isPosition())
    return;

  // Check for loads.
  if (MI.mayLoad() && !MI.isDereferenceableInvariantLoad())
    Read = true;

  // Check for stores.
  if (MI.mayStore()) {
    Write = true;
  } else if (MI.hasOrderedMemoryRef()) {
    switch (MI.getOpcode()) {
    case WebAssembly::DIV_S_I32:
    case WebAssembly::DIV_S_I64:
    case WebAssembly::REM_S_I32:
    case WebAssembly::REM_S_I64:
    case WebAssembly::DIV_U_I32:
    case WebAssembly::DIV_U_I64:
    case WebAssembly::REM_U_I32:
    case WebAssembly::REM_U_I64:
    case WebAssembly::I32_TRUNC_S_F32:
    case WebAssembly::I64_TRUNC_S_F32:
    case WebAssembly::I32_TRUNC_S_F64:
    case WebAssembly::I64_TRUNC_S_F64:
    case WebAssembly::I32_TRUNC_U_F32:
    case WebAssembly::I64_TRUNC_U_F32:
    case WebAssembly::I32_TRUNC_U_F64:
    case WebAssembly::I64_TRUNC_U_F64:
      // These instruction have hasUnmodeledSideEffects() returning true
      // because they trap on overflow and invalid so they can't be arbitrarily
      // moved, however hasOrderedMemoryRef() interprets this plus their lack
      // of memoperands as having a potential unknown memory reference.
      break;
    default:
      // Record volatile accesses, unless it's a call, as calls are handled
      // specially below.
      if (!MI.isCall()) {
        Write = true;
        Effects = true;
      }
      break;
    }
  }

  // Check for side effects.
  if (MI.hasUnmodeledSideEffects()) {
    switch (MI.getOpcode()) {
    case WebAssembly::DIV_S_I32:
    case WebAssembly::DIV_S_I64:
    case WebAssembly::REM_S_I32:
    case WebAssembly::REM_S_I64:
    case WebAssembly::DIV_U_I32:
    case WebAssembly::DIV_U_I64:
    case WebAssembly::REM_U_I32:
    case WebAssembly::REM_U_I64:
    case WebAssembly::I32_TRUNC_S_F32:
    case WebAssembly::I64_TRUNC_S_F32:
    case WebAssembly::I32_TRUNC_S_F64:
    case WebAssembly::I64_TRUNC_S_F64:
    case WebAssembly::I32_TRUNC_U_F32:
    case WebAssembly::I64_TRUNC_U_F32:
    case WebAssembly::I32_TRUNC_U_F64:
    case WebAssembly::I64_TRUNC_U_F64:
      // These instructions have hasUnmodeledSideEffects() returning true
      // because they trap on overflow and invalid so they can't be arbitrarily
      // moved, however in the specific case of register stackifying, it is safe
      // to move them because overflow and invalid are Undefined Behavior.
      break;
    default:
      Effects = true;
      break;
    }
  }

  // Check for writes to __stack_pointer global.
  if ((MI.getOpcode() == WebAssembly::GLOBAL_SET_I32 ||
       MI.getOpcode() == WebAssembly::GLOBAL_SET_I64) &&
      MI.getOperand(0).isSymbol() &&
      !strcmp(MI.getOperand(0).getSymbolName(), "__stack_pointer"))
    StackPointer = true;

  if (MI.isCall() && MI.getOperand(0).isSymbol() &&
      !strcmp(MI.getOperand(0).getSymbolName(), "__wasm_get_stack_pointer"))
    StackPointer = true;

  // Analyze calls.
  if (MI.isCall()) {
    queryCallee(MI, Read, Write, Effects, StackPointer);
  }
}

// Test whether Def is safe and profitable to rematerialize.
static bool shouldRematerialize(const MachineInstr &Def,
                                const WebAssemblyInstrInfo *TII) {
  return Def.isAsCheapAsAMove() && TII->isTriviallyReMaterializable(Def);
}

// Identify the definition for this register at this point. This is a
// generalization of MachineRegisterInfo::getUniqueVRegDef that uses
// LiveIntervals to handle complex cases.
static MachineInstr *getVRegDef(unsigned Reg, const MachineInstr *Insert,
                                const MachineRegisterInfo &MRI,
                                const LiveIntervals *LIS) {
  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MachineInstr *Def = MRI.getUniqueVRegDef(Reg))
    return Def;

  // MRI doesn't know what the Def is. Try asking LIS.
  if (LIS != nullptr) {
    SlotIndex InstIndex = LIS->getInstructionIndex(*Insert);
    if (const VNInfo *ValNo = LIS->getInterval(Reg).getVNInfoBefore(InstIndex))
      return LIS->getInstructionFromIndex(ValNo->def);
  }

  return nullptr;
}

// Test whether Reg, as defined at Def, has exactly one use. This is a
// generalization of MachineRegisterInfo::hasOneNonDBGUse that uses
// LiveIntervals to handle complex cases in optimized code.
static bool hasSingleUse(unsigned Reg, MachineRegisterInfo &MRI,
                         const MachineFunction &MF, bool Optimize,
                         MachineInstr *Def, LiveIntervals *LIS) {
  auto &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  // The frame base always has an implicit DBG use as DW_AT_frame_base.
  if (MFI.isFrameBaseVirtual() && MFI.getFrameBaseVreg() == Reg) {
    // When using global thread context, the frame base can be encoded
    // as an offset from __stack_pointer, so the vreg can be stackified.
    // However, when using libcall thread context, we need to keep the frame
    // base vreg around if debug info is enabled, because there is no
    // global to refer to.
    bool NeedsRegForDebug =
        MF.getFunction().getSubprogram() &&
        MF.getSubtarget<WebAssemblySubtarget>().hasLibcallThreadContext();
    if (!Optimize || NeedsRegForDebug)
      return false;
  }
  if (!Optimize) {
    // Using "hasOneUse" instead of "hasOneNonDBGUse" here because we don't
    // want to stackify DBG_VALUE operands - WASM stack locations are less
    // useful and less widely supported than WASM local locations.
    if (!MRI.hasOneUse(Reg))
      return false;
    return true;
  }

  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MRI.hasOneNonDBGUse(Reg))
    return true;

  if (LIS == nullptr)
    return false;

  bool HasOne = false;
  const LiveInterval &LI = LIS->getInterval(Reg);
  const VNInfo *DefVNI =
      LI.getVNInfoAt(LIS->getInstructionIndex(*Def).getRegSlot());
  assert(DefVNI);
  for (auto &I : MRI.use_nodbg_operands(Reg)) {
    const auto &Result = LI.Query(LIS->getInstructionIndex(*I.getParent()));
    if (Result.valueIn() == DefVNI) {
      if (!Result.isKill())
        return false;
      if (HasOne)
        return false;
      HasOne = true;
    }
  }
  return HasOne;
}

// Test whether it's safe to move Def to just before Insert.
// TODO: Compute memory dependencies in a way that doesn't require always
// walking the block.
// TODO: Compute memory dependencies in a way that uses AliasAnalysis to be
// more precise.
static bool isSafeToMove(const MachineOperand *Def, const MachineOperand *Use,
                         const MachineInstr *Insert,
                         const WebAssemblyFunctionInfo &MFI,
                         const MachineRegisterInfo &MRI, bool Optimize) {
  const MachineInstr *DefI = Def->getParent();
  assert(DefI->getParent() == Insert->getParent());
  assert(Use->getParent()->getParent() == Insert->getParent());

  // For now avoid stackifying any multi-def instructions. While it's
  // theoretically possible to do so for the first def in some cases this has
  // historically led to bugs such as #199910 and #98323. For now this
  // conservatively skips all multi-def instructions as a consequence. Note that
  // multi-def instructions are expected to be not all that common so this in
  // theory doesn't have a massive impact, but nevertheless this'd still be
  // something to optimize better in the future.
  if (DefI->getNumExplicitDefs() > 1)
    return false;

  // If moving is a semantic nop, it is always allowed
  const MachineBasicBlock *MBB = DefI->getParent();
  auto NextI = std::next(MachineBasicBlock::const_iterator(DefI));
  for (auto E = MBB->end(); NextI != E && NextI->isDebugInstr(); ++NextI)
    ;
  if (NextI == Insert)
    return true;

  // When not optimizing, we only handle the trivial case above
  // to guarantee no impact to debugging and to avoid spending
  // compile time.
  if (!Optimize)
    return false;

  // 'catch' and 'catch_all' should be the first instruction of a BB and cannot
  // move.
  if (WebAssembly::isCatch(DefI->getOpcode()))
    return false;

  // Check for register dependencies.
  SmallVector<unsigned, 4> MutableRegisters;
  for (const MachineOperand &MO : DefI->operands()) {
    if (!MO.isReg() || MO.isUndef())
      continue;
    Register Reg = MO.getReg();

    // If the register is dead here and at Insert, ignore it.
    if (MO.isDead() && Insert->definesRegister(Reg, /*TRI=*/nullptr) &&
        !Insert->readsRegister(Reg, /*TRI=*/nullptr))
      continue;

    if (Reg.isPhysical()) {
      // Ignore ARGUMENTS; it's just used to keep the ARGUMENT_* instructions
      // from moving down, and we've already checked for that.
      if (Reg == WebAssembly::ARGUMENTS)
        continue;
      // If the physical register is never modified, ignore it.
      if (!MRI.isPhysRegModified(Reg))
        continue;
      // Otherwise, it's a physical register with unknown liveness.
      return false;
    }

    // If one of the operands isn't in SSA form, it has different values at
    // different times, and we need to make sure we don't move our use across
    // a different def.
    if (!MO.isDef() && !MRI.hasOneDef(Reg))
      MutableRegisters.push_back(Reg);
  }

  bool Read = false, Write = false, Effects = false, StackPointer = false;
  query(*DefI, Read, Write, Effects, StackPointer);

  // If the instruction does not access memory and has no side effects, it has
  // no additional dependencies.
  bool HasMutableRegisters = !MutableRegisters.empty();
  if (!Read && !Write && !Effects && !StackPointer && !HasMutableRegisters)
    return true;

  // Scan through the intervening instructions between DefI and Insert.
  MachineBasicBlock::const_iterator D(DefI), I(Insert);
  for (--I; I != D; --I) {
    bool InterveningRead = false;
    bool InterveningWrite = false;
    bool InterveningEffects = false;
    bool InterveningStackPointer = false;
    query(*I, InterveningRead, InterveningWrite, InterveningEffects,
          InterveningStackPointer);
    if (Effects && InterveningEffects)
      return false;
    if (Read && InterveningWrite)
      return false;
    if (Write && (InterveningRead || InterveningWrite))
      return false;
    if (StackPointer && InterveningStackPointer)
      return false;

    for (unsigned Reg : MutableRegisters)
      for (const MachineOperand &MO : I->operands())
        if (MO.isReg() && MO.isDef() && MO.getReg() == Reg)
          return false;
  }

  return true;
}

/// Test whether OneUse, a use of Reg, dominates all of Reg's other uses.
static bool oneUseDominatesOtherUses(unsigned Reg, const MachineOperand &OneUse,
                                     const MachineBasicBlock &MBB,
                                     const MachineRegisterInfo &MRI,
                                     const MachineDominatorTree &MDT,
                                     LiveIntervals &LIS,
                                     WebAssemblyFunctionInfo &MFI) {
  const LiveInterval &LI = LIS.getInterval(Reg);

  const MachineInstr *OneUseInst = OneUse.getParent();
  VNInfo *OneUseVNI = LI.getVNInfoBefore(LIS.getInstructionIndex(*OneUseInst));

  for (const MachineOperand &Use : MRI.use_nodbg_operands(Reg)) {
    if (&Use == &OneUse)
      continue;

    const MachineInstr *UseInst = Use.getParent();
    VNInfo *UseVNI = LI.getVNInfoBefore(LIS.getInstructionIndex(*UseInst));

    if (UseVNI != OneUseVNI)
      continue;

    if (UseInst == OneUseInst) {
      // Another use in the same instruction. We need to ensure that the one
      // selected use happens "before" it.
      if (&OneUse > &Use)
        return false;
    } else {
      // Test that the use is dominated by the one selected use.
      while (!MDT.dominates(OneUseInst, UseInst)) {
        // Actually, dominating is over-conservative. Test that the use would
        // happen after the one selected use in the stack evaluation order.
        //
        // This is needed as a consequence of using implicit local.gets for
        // uses and implicit local.sets for defs.
        if (UseInst->getDesc().getNumDefs() == 0)
          return false;
        const MachineOperand &MO = UseInst->getOperand(0);
        if (!MO.isReg())
          return false;
        Register DefReg = MO.getReg();
        if (!DefReg.isVirtual() || !MFI.isVRegStackified(DefReg))
          return false;
        assert(MRI.hasOneNonDBGUse(DefReg));
        const MachineOperand &NewUse = *MRI.use_nodbg_begin(DefReg);
        const MachineInstr *NewUseInst = NewUse.getParent();
        if (NewUseInst == OneUseInst) {
          if (&OneUse > &NewUse)
            return false;
          break;
        }
        UseInst = NewUseInst;
      }
    }
  }
  return true;
}

/// Get the appropriate tee opcode for the given register class.
static unsigned getTeeOpcode(const TargetRegisterClass *RC) {
  if (RC == &WebAssembly::I32RegClass)
    return WebAssembly::TEE_I32;
  if (RC == &WebAssembly::I64RegClass)
    return WebAssembly::TEE_I64;
  if (RC == &WebAssembly::F32RegClass)
    return WebAssembly::TEE_F32;
  if (RC == &WebAssembly::F64RegClass)
    return WebAssembly::TEE_F64;
  if (RC == &WebAssembly::V128RegClass)
    return WebAssembly::TEE_V128;
  if (RC == &WebAssembly::EXTERNREFRegClass)
    return WebAssembly::TEE_EXTERNREF;
  if (RC == &WebAssembly::FUNCREFRegClass)
    return WebAssembly::TEE_FUNCREF;
  if (RC == &WebAssembly::EXNREFRegClass)
    return WebAssembly::TEE_EXNREF;
  llvm_unreachable("Unexpected register class");
}

// Shrink LI to its uses, cleaning up LI.
static void shrinkToUses(LiveInterval &LI, LiveIntervals &LIS) {
  if (LIS.shrinkToUses(&LI)) {
    SmallVector<LiveInterval *, 4> SplitLIs;
    LIS.splitSeparateComponents(LI, SplitLIs);
  }
}

/// A single-use def in the same block with no intervening memory or register
/// dependencies; move the def down and nest it with the current instruction.
static MachineInstr *moveForSingleUse(unsigned Reg, MachineOperand &Op,
                                      MachineInstr *Def, MachineBasicBlock &MBB,
                                      MachineInstr *Insert, LiveIntervals *LIS,
                                      WebAssemblyFunctionInfo &MFI,
                                      MachineRegisterInfo &MRI) {
  LLVM_DEBUG(dbgs() << "Move for single use: "; Def->dump());

  WebAssemblyDebugValueManager DefDIs(Def);
  DefDIs.sink(Insert);
  if (LIS != nullptr)
    LIS->handleMove(*Def);

  if (MRI.hasOneDef(Reg) && MRI.hasOneNonDBGUse(Reg)) {
    // No one else is using this register for anything so we can just stackify
    // it in place.
    MFI.stackifyVReg(MRI, Reg);
  } else {
    // The register may have unrelated uses or defs; create a new register for
    // just our one def and use so that we can stackify it.
    Register NewReg = MRI.createVirtualRegister(MRI.getRegClass(Reg));
    Op.setReg(NewReg);
    DefDIs.updateReg(NewReg);

    if (LIS != nullptr) {
      // Tell LiveIntervals about the new register.
      LIS->createAndComputeVirtRegInterval(NewReg);

      // Tell LiveIntervals about the changes to the old register.
      LiveInterval &LI = LIS->getInterval(Reg);
      LI.removeSegment(LIS->getInstructionIndex(*Def).getRegSlot(),
                       LIS->getInstructionIndex(*Op.getParent()).getRegSlot(),
                       /*RemoveDeadValNo=*/true);
    }

    MFI.stackifyVReg(MRI, NewReg);
    LLVM_DEBUG(dbgs() << " - Replaced register: "; Def->dump());
  }

  imposeStackOrdering(Def);
  return Def;
}

static MachineInstr *getPrevNonDebugInst(MachineInstr *MI) {
  for (auto *I = MI->getPrevNode(); I; I = I->getPrevNode())
    if (!I->isDebugInstr())
      return I;
  return nullptr;
}

/// A trivially cloneable instruction; clone it and nest the new copy with the
/// current instruction.
static MachineInstr *
rematerializeCheapDef(unsigned Reg, MachineOperand &Op, MachineInstr &Def,
                      MachineBasicBlock::instr_iterator Insert,
                      LiveIntervals &LIS, WebAssemblyFunctionInfo &MFI,
                      MachineRegisterInfo &MRI,
                      const WebAssemblyInstrInfo *TII) {
  LLVM_DEBUG(dbgs() << "Rematerializing cheap def: "; Def.dump());
  LLVM_DEBUG(dbgs() << " - for use in "; Op.getParent()->dump());

  WebAssemblyDebugValueManager DefDIs(&Def);

  Register NewReg = MRI.createVirtualRegister(MRI.getRegClass(Reg));
  DefDIs.cloneSink(&*Insert, NewReg);
  Op.setReg(NewReg);
  MachineInstr *Clone = getPrevNonDebugInst(&*Insert);
  assert(Clone);
  LIS.InsertMachineInstrInMaps(*Clone);
  LIS.createAndComputeVirtRegInterval(NewReg);
  MFI.stackifyVReg(MRI, NewReg);
  imposeStackOrdering(Clone);

  LLVM_DEBUG(dbgs() << " - Cloned to "; Clone->dump());

  // Shrink the interval.
  bool IsDead = MRI.use_empty(Reg);
  if (!IsDead) {
    LiveInterval &LI = LIS.getInterval(Reg);
    shrinkToUses(LI, LIS);
    IsDead = !LI.liveAt(LIS.getInstructionIndex(Def).getDeadSlot());
  }

  // If that was the last use of the original, delete the original.
  if (IsDead) {
    LLVM_DEBUG(dbgs() << " - Deleting original\n");
    SlotIndex Idx = LIS.getInstructionIndex(Def).getRegSlot();
    LIS.removePhysRegDefAt(MCRegister::from(WebAssembly::ARGUMENTS), Idx);
    LIS.removeInterval(Reg);
    LIS.RemoveMachineInstrFromMaps(Def);
    DefDIs.removeDef();
  }

  return Clone;
}

/// A multiple-use def in the same block with no intervening memory or register
/// dependencies; move the def down, nest it with the current instruction, and
/// insert a tee to satisfy the rest of the uses. As an illustration, rewrite
/// this:
///
///    Reg = INST ...        // Def
///    INST ..., Reg, ...    // Insert
///    INST ..., Reg, ...
///    INST ..., Reg, ...
///
/// to this:
///
///    DefReg = INST ...     // Def (to become the new Insert)
///    TeeReg, Reg = TEE_... DefReg
///    INST ..., TeeReg, ... // Insert
///    INST ..., Reg, ...
///    INST ..., Reg, ...
///
/// with DefReg and TeeReg stackified. This eliminates a local.get from the
/// resulting code.
static MachineInstr *moveAndTeeForMultiUse(
    unsigned Reg, MachineOperand &Op, MachineInstr *Def, MachineBasicBlock &MBB,
    MachineInstr *Insert, LiveIntervals &LIS, WebAssemblyFunctionInfo &MFI,
    MachineRegisterInfo &MRI, const WebAssemblyInstrInfo *TII) {
  LLVM_DEBUG(dbgs() << "Move and tee for multi-use:"; Def->dump());

  const auto *RegClass = MRI.getRegClass(Reg);
  Register TeeReg = MRI.createVirtualRegister(RegClass);
  Register DefReg = MRI.createVirtualRegister(RegClass);

  // Move Def into place.
  WebAssemblyDebugValueManager DefDIs(Def);
  DefDIs.sink(Insert);
  LIS.handleMove(*Def);

  // Create the Tee and attach the registers.
  MachineOperand &DefMO = Def->getOperand(0);
  MachineInstr *Tee = BuildMI(MBB, Insert, Insert->getDebugLoc(),
                              TII->get(getTeeOpcode(RegClass)), TeeReg)
                          .addReg(Reg, RegState::Define)
                          .addReg(DefReg, getUndefRegState(DefMO.isDead()));
  Op.setReg(TeeReg);
  DefDIs.updateReg(DefReg);
  SlotIndex TeeIdx = LIS.InsertMachineInstrInMaps(*Tee).getRegSlot();
  SlotIndex DefIdx = LIS.getInstructionIndex(*Def).getRegSlot();

  // Tell LiveIntervals we moved the original vreg def from Def to Tee.
  LiveInterval &LI = LIS.getInterval(Reg);
  LiveInterval::iterator I = LI.FindSegmentContaining(DefIdx);
  VNInfo *ValNo = LI.getVNInfoAt(DefIdx);
  I->start = TeeIdx;
  ValNo->def = TeeIdx;
  shrinkToUses(LI, LIS);

  // Finish stackifying the new regs.
  LIS.createAndComputeVirtRegInterval(TeeReg);
  LIS.createAndComputeVirtRegInterval(DefReg);
  MFI.stackifyVReg(MRI, DefReg);
  MFI.stackifyVReg(MRI, TeeReg);
  imposeStackOrdering(Def);
  imposeStackOrdering(Tee);

  // Even though 'TeeReg, Reg = TEE ...', has two defs, we don't need to clone
  // DBG_VALUEs for both of them, given that the latter will cancel the former
  // anyway. Here we only clone DBG_VALUEs for TeeReg, which will be converted
  // to a local index in ExplicitLocals pass.
  DefDIs.cloneSink(Insert, TeeReg, /* CloneDef */ false);

  LLVM_DEBUG(dbgs() << " - Replaced register: "; Def->dump());
  LLVM_DEBUG(dbgs() << " - Tee instruction: "; Tee->dump());
  return Def;
}

namespace {
/// A stack for walking the tree of instructions being built, visiting the
/// MachineOperands in DFS order.
class TreeWalkerState {
  using mop_iterator = MachineInstr::mop_iterator;
  using mop_reverse_iterator = std::reverse_iterator<mop_iterator>;
  using RangeTy = iterator_range<mop_reverse_iterator>;
  SmallVector<RangeTy, 4> Worklist;

public:
  explicit TreeWalkerState(MachineInstr *Insert) {
    const iterator_range<mop_iterator> &Range = Insert->explicit_uses();
    if (!Range.empty())
      Worklist.push_back(reverse(Range));
  }

  bool done() const { return Worklist.empty(); }

  MachineOperand &pop() {
    RangeTy &Range = Worklist.back();
    MachineOperand &Op = *Range.begin();
    Range = drop_begin(Range);
    if (Range.empty())
      Worklist.pop_back();
    assert((Worklist.empty() || !Worklist.back().empty()) &&
           "Empty ranges shouldn't remain in the worklist");
    return Op;
  }

  /// Push Instr's operands onto the stack to be visited.
  void pushOperands(MachineInstr *Instr) {
    const iterator_range<mop_iterator> &Range(Instr->explicit_uses());
    if (!Range.empty())
      Worklist.push_back(reverse(Range));
  }

  /// Some of Instr's operands are on the top of the stack; remove them and
  /// re-insert them starting from the beginning (because we've commuted them).
  void resetTopOperands(MachineInstr *Instr) {
    assert(hasRemainingOperands(Instr) &&
           "Reseting operands should only be done when the instruction has "
           "an operand still on the stack");
    Worklist.back() = reverse(Instr->explicit_uses());
  }

  /// Restart Instr's operands from the beginning. Unlike resetTopOperands, this
  /// also handles the case where we just popped Instr's final operand.
  void restartOperands(MachineInstr *Instr) {
    const iterator_range<mop_iterator> &Range = Instr->explicit_uses();
    if (Range.empty())
      return;
    if (hasRemainingOperands(Instr))
      Worklist.back() = reverse(Range);
    else
      Worklist.push_back(reverse(Range));
  }

  /// Test whether Instr has operands remaining to be visited at the top of
  /// the stack.
  bool hasRemainingOperands(const MachineInstr *Instr) const {
    if (Worklist.empty())
      return false;
    const RangeTy &Range = Worklist.back();
    return !Range.empty() && Range.begin()->getParent() == Instr;
  }

  /// Test whether the given register is present on the stack, indicating an
  /// operand in the tree that we haven't visited yet. Moving a definition of
  /// Reg to a point in the tree after that would change its value.
  ///
  /// This is needed as a consequence of using implicit local.gets for
  /// uses and implicit local.sets for defs.
  bool isOnStack(unsigned Reg) const {
    for (const RangeTy &Range : Worklist)
      for (const MachineOperand &MO : Range)
        if (MO.isReg() && MO.getReg() == Reg)
          return true;
    return false;
  }
};

/// State to keep track of whether commuting is in flight or whether it's been
/// tried for the current instruction and didn't work.
class CommutingState {
  /// There are effectively three states: the initial state where we haven't
  /// started commuting anything and we don't know anything yet, the tentative
  /// state where we've commuted the operands of the current instruction and are
  /// revisiting it, and the declined state where we've reverted the operands
  /// back to their original order and will no longer commute it further.
  bool TentativelyCommuting = false;
  bool Declined = false;

  /// During the tentative state, these hold the operand indices of the commuted
  /// operands.
  unsigned Operand0, Operand1;

public:
  /// Stackification for an operand was not successful due to ordering
  /// constraints. If possible, and if we haven't already tried it and declined
  /// it, commute Insert's operands and prepare to revisit it.
  void maybeCommute(MachineInstr *Insert, TreeWalkerState &TreeWalker,
                    const WebAssemblyInstrInfo *TII) {
    if (TentativelyCommuting) {
      assert(!Declined &&
             "Don't decline commuting until you've finished trying it");
      // Commuting didn't help. Revert it.
      TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
      TentativelyCommuting = false;
      Declined = true;
    } else if (!Declined && TreeWalker.hasRemainingOperands(Insert)) {
      Operand0 = TargetInstrInfo::CommuteAnyOperandIndex;
      Operand1 = TargetInstrInfo::CommuteAnyOperandIndex;
      if (TII->findCommutedOpIndices(*Insert, Operand0, Operand1)) {
        // Tentatively commute the operands and try again.
        TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
        TreeWalker.resetTopOperands(Insert);
        TentativelyCommuting = true;
        Declined = false;
      }
    }
  }

  /// Stackification for some operand was successful. Reset to the default
  /// state.
  void reset() {
    TentativelyCommuting = false;
    Declined = false;
  }
};
} // end anonymous namespace

static unsigned nonDebugDistance(const MachineInstr *From,
                                 const MachineInstr *To, unsigned Limit) {
  if (!From || !To || From->getParent() != To->getParent())
    return Limit + 1;

  unsigned Distance = 0;
  for (const MachineInstr *I = From->getNextNode(); I && I != To;
       I = I->getNextNode()) {
    if (I->isDebugInstr())
      continue;
    if (++Distance > Limit)
      return Distance;
  }
  return Distance;
}

static void addPressure(WasmExecPressureResult &Total,
                        const WasmExecPressureResult &R) {
  Total.PeakFP = std::max(Total.PeakFP, R.PeakFP);
  Total.PeakInt = std::max(Total.PeakInt, R.PeakInt);
  Total.PeakRef = std::max(Total.PeakRef, R.PeakRef);
  Total.PeakV128 = std::max(Total.PeakV128, R.PeakV128);
  Total.EstimatedLocalGets += R.EstimatedLocalGets;
  Total.EstimatedLocalSets += R.EstimatedLocalSets;
  Total.EstimatedTees += R.EstimatedTees;
  Total.EstimatedDispatch += R.EstimatedDispatch;
  Total.Nodes += R.Nodes;
  Total.HitLimit |= R.HitLimit;
}

static bool hasStackifiedDef(const MachineInstr &MI,
                             const WebAssemblyFunctionInfo &MFI) {
  for (const MachineOperand &MO : MI.defs())
    if (MO.isReg() && MO.getReg().isVirtual() &&
        MFI.isVRegStackified(MO.getReg()))
      return true;
  return false;
}

static unsigned fpOverflow(const WasmExecutionProfile &Profile,
                           const WasmExecPressureResult &R) {
  if (!Profile.FPRingCapacity)
    return 0;
  return R.PeakFP > Profile.FPRingCapacity ? R.PeakFP - Profile.FPRingCapacity
                                           : 0;
}

static unsigned intOverflow(const WasmExecutionProfile &Profile,
                            const WasmExecPressureResult &R) {
  if (!Profile.IntRingCapacity)
    return 0;
  return R.PeakInt > Profile.IntRingCapacity
             ? R.PeakInt - Profile.IntRingCapacity
             : 0;
}

static void finishPressureForProfile(const WasmExecutionProfile &Profile,
                                     WasmExecPressureResult &R) {
  unsigned Overflow = fpOverflow(Profile, R) + intOverflow(Profile, R);
  if (Profile.HasRegisterRing) {
    R.EstimatedRingSpills = Overflow;
    R.EstimatedRingFills = Overflow;
  } else {
    R.EstimatedRingSpills = 0;
    R.EstimatedRingFills = 0;
  }
  R.Score = scorePressure(Profile, R);
}

static unsigned totalOverflow(const WasmExecutionProfile &Profile,
                              const WasmExecPressureResult &R) {
  return fpOverflow(Profile, R) + intOverflow(Profile, R);
}

static void dumpExecutionPressure(MachineFunction &MF,
                                  const WebAssemblySubtarget &ST,
                                  const WebAssemblyFunctionInfo &MFI,
                                  const MachineRegisterInfo &MRI) {
  const WasmExecutionProfile &Profile = ST.getExecutionProfile();
  dbgs() << "wasm-exec-pressure: function=" << MF.getName()
         << " tune=" << ST.getTuneCPUName() << "\n";
  dbgs() << "  profile:"
         << " fp-ring=" << Profile.FPRingCapacity
         << " int-ring=" << Profile.IntRingCapacity
         << " register-ring=" << (Profile.HasRegisterRing ? "true" : "false")
         << " m3-slot-provider="
         << (Profile.HasM3SlotProviderModel ? "true" : "false") << "\n";

  WasmExecPressureResult FunctionPressure;
  int64_t FunctionScoreSum = 0;
  for (MachineBasicBlock &MBB : MF) {
    WasmExecPressureResult BBPressure;
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isPosition())
        continue;
      // Stackified defs are accounted for by the instruction that consumes
      // them, which keeps the dump focused on completed expression roots.
      if (hasStackifiedDef(MI, MFI))
        continue;
      WasmExecPressureResult R = estimateStackifiedTreePressure(
          MI, MRI, MFI, WasmTuneStackifyNodeLimit);
      addPressure(BBPressure, R);
    }

    finishPressureForProfile(Profile, BBPressure);
    FunctionScoreSum += BBPressure.Score;
    addPressure(FunctionPressure, BBPressure);

    dbgs() << "  bb." << MBB.getNumber() << ":\n";
    dbgs() << "    peak-fp=" << BBPressure.PeakFP
           << " peak-int=" << BBPressure.PeakInt
           << " peak-ref=" << BBPressure.PeakRef
           << " peak-v128=" << BBPressure.PeakV128 << "\n";
    dbgs() << "    cap-overflow-fp=" << fpOverflow(Profile, BBPressure)
           << " cap-overflow-int=" << intOverflow(Profile, BBPressure) << "\n";
    dbgs() << "    estimated-spills=" << BBPressure.EstimatedRingSpills
           << " estimated-fills=" << BBPressure.EstimatedRingFills << "\n";
    dbgs() << "    local.get=" << BBPressure.EstimatedLocalGets
           << " local.set=" << BBPressure.EstimatedLocalSets
           << " tee=" << BBPressure.EstimatedTees
           << " dispatch=" << BBPressure.EstimatedDispatch << "\n";
    dbgs() << "    score=" << BBPressure.Score << " nodes=" << BBPressure.Nodes
           << " hit-limit=" << (BBPressure.HitLimit ? "true" : "false") << "\n";
  }

  finishPressureForProfile(Profile, FunctionPressure);
  dbgs() << "  function-peak:"
         << " peak-fp=" << FunctionPressure.PeakFP
         << " peak-int=" << FunctionPressure.PeakInt
         << " peak-ref=" << FunctionPressure.PeakRef
         << " peak-v128=" << FunctionPressure.PeakV128 << "\n";
  dbgs() << "  function-score-sum=" << FunctionScoreSum << "\n";
}

static bool shouldUseProfileGate(const WasmExecutionProfile &Profile,
                                 MachineInstr *Insert) {
  if (!WasmTuneStackify || Profile.Kind == WasmTuneKind::Generic)
    return false;
  // A call's operand arity is not something RegStackify can reduce: localizing
  // a producer still leaves the call with the same number of value-stack
  // operands. Keep call-shape tuning for a later, call-aware model.
  if (Insert->isCall())
    return false;
  return true;
}

struct ProfileGateResult {
  bool Allow = true;
  bool ProfileVeto = false;
};

enum class StackifyActionKind {
  KeepLocal,
  Move,
  Rematerialize,
  Tee,
};

struct StackifyActionCandidate {
  StackifyActionKind Kind = StackifyActionKind::KeepLocal;
  bool Legal = false;
  WasmExecPressureResult Pressure;
  int64_t Score = std::numeric_limits<int64_t>::max();
};

static StringRef actionName(StackifyActionKind Kind) {
  switch (Kind) {
  case StackifyActionKind::KeepLocal:
    return "keep-local";
  case StackifyActionKind::Move:
    return "move";
  case StackifyActionKind::Rematerialize:
    return "rematerialize";
  case StackifyActionKind::Tee:
    return "tee";
  }
  llvm_unreachable("unknown stackify action");
}

static unsigned actionPriority(StackifyActionKind Kind) {
  switch (Kind) {
  case StackifyActionKind::Move:
    return 0;
  case StackifyActionKind::Rematerialize:
    return 1;
  case StackifyActionKind::Tee:
    return 2;
  case StackifyActionKind::KeepLocal:
    return 3;
  }
  llvm_unreachable("unknown stackify action");
}

static bool isFPAddOpcode(unsigned Opcode) {
  switch (Opcode) {
  case WebAssembly::ADD_F32:
  case WebAssembly::ADD_F64:
    return true;
  default:
    return false;
  }
}

static bool isFPMulOpcode(unsigned Opcode) {
  switch (Opcode) {
  case WebAssembly::MUL_F32:
  case WebAssembly::MUL_F64:
    return true;
  default:
    return false;
  }
}

// Product-bank splitting is allowed to bias local boundaries, but not to change
// strict FP reduction shape. Require reassoc on every inspected FP add/mul
// node.
static bool isReassocFPProductBankNodeImpl(
    const MachineInstr *MI, const MachineRegisterInfo &MRI, unsigned Limit,
    SmallPtrSetImpl<const MachineInstr *> &Visiting) {
  if (!MI || !MI->getFlag(MachineInstr::FmReassoc))
    return false;
  if (isFPMulOpcode(MI->getOpcode()))
    return true;
  if (!isFPAddOpcode(MI->getOpcode()) || Limit == 0)
    return false;
  if (!Visiting.insert(MI).second)
    return false;

  for (const MachineOperand &MO : MI->explicit_uses()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    if (isReassocFPProductBankNodeImpl(MRI.getUniqueVRegDef(MO.getReg()), MRI,
                                       Limit - 1, Visiting)) {
      Visiting.erase(MI);
      return true;
    }
  }

  Visiting.erase(MI);
  return false;
}

static bool isReassocFPProductBankNode(const MachineInstr *MI,
                                       const MachineRegisterInfo &MRI) {
  SmallPtrSet<const MachineInstr *, 16> Visiting;
  return isReassocFPProductBankNodeImpl(MI, MRI, /*Limit=*/16, Visiting);
}

static unsigned countReassocFPProductBankProductsImpl(
    const MachineInstr *MI, const MachineRegisterInfo &MRI, unsigned Limit,
    SmallPtrSetImpl<const MachineInstr *> &Visiting) {
  if (!MI || !MI->getFlag(MachineInstr::FmReassoc))
    return 0;
  if (isFPMulOpcode(MI->getOpcode()))
    return 1;
  if (!isFPAddOpcode(MI->getOpcode()) || Limit == 0)
    return 0;
  if (!Visiting.insert(MI).second)
    return 0;

  unsigned Products = 0;
  for (const MachineOperand &MO : MI->explicit_uses()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    Products += countReassocFPProductBankProductsImpl(
        MRI.getUniqueVRegDef(MO.getReg()), MRI, Limit - 1, Visiting);
  }

  Visiting.erase(MI);
  return Products;
}

static unsigned countReassocFPProductBankProducts(
    const MachineInstr *MI, const MachineRegisterInfo &MRI) {
  SmallPtrSet<const MachineInstr *, 16> Visiting;
  return countReassocFPProductBankProductsImpl(MI, MRI, /*Limit=*/16,
                                               Visiting);
}

static bool isReassocFPProductBankEdge(Register Reg, MachineInstr *DefI,
                                       MachineInstr *Insert,
                                       const MachineRegisterInfo &MRI) {
  if (!DefI || !Insert)
    return false;
  if (!isWasmFPReg(Reg, MRI))
    return false;
  if (!isFPAddOpcode(Insert->getOpcode()))
    return false;
  return Insert->getFlag(MachineInstr::FmReassoc) &&
         isReassocFPProductBankNode(DefI, MRI);
}

static bool shouldPreferProductBankBoundary(
    const WasmExecutionProfile &Profile, Register Reg, MachineInstr *DefI,
    MachineInstr *Insert, const MachineRegisterInfo &MRI,
    const WasmExecPressureResult &StackifiedPressure) {
  if (!Profile.HasRegisterRing || !Profile.FPRingCapacity)
    return false;
  if (!isReassocFPProductBankEdge(Reg, DefI, Insert, MRI))
    return false;
  // A plus-accumulator/product-bank shape normally uses one lane for the
  // incoming accumulator and the rest for product producers. Avoid treating
  // smaller trees as bank-width boundaries just because the estimator's peak
  // includes transient add operands.
  if (countReassocFPProductBankProducts(DefI, MRI) + 1 <
      Profile.PreferredFPBank)
    return false;
  return StackifiedPressure.PeakFP >= Profile.PreferredFPBank;
}

static int64_t
productBankBoundaryPenalty(const WasmExecutionProfile &Profile, Register Reg,
                           MachineInstr *DefI, MachineInstr *Insert,
                           const MachineRegisterInfo &MRI,
                           const WasmExecPressureResult &StackifiedPressure) {
  if (!shouldPreferProductBankBoundary(Profile, Reg, DefI, Insert, MRI,
                                       StackifiedPressure))
    return 0;

  unsigned Saturation = StackifiedPressure.PeakFP - Profile.PreferredFPBank + 1;
  return int64_t(Saturation) * (Profile.SpillCost + Profile.FillCost +
                                Profile.LocalGetCost + Profile.LocalSetCost);
}

static ProfileGateResult profileGateU2Delta(const WebAssemblySubtarget &ST,
                                            Register Reg, MachineInstr *DefI,
                                            MachineInstr *Insert,
                                            const MachineRegisterInfo &MRI,
                                            const WebAssemblyFunctionInfo &MFI,
                                            StringRef Action) {
  const WasmExecutionProfile &Profile = ST.getExecutionProfile();
  WasmExecPressureResult Before = estimateStackifiedTreePressure(
      *Insert, MRI, MFI, WasmTuneStackifyNodeLimit);
  finishPressureForProfile(Profile, Before);

  WasmExecPressureResult After = estimateStackifiedTreePressure(
      *Insert, Reg, *DefI, MRI, MFI, WasmTuneStackifyNodeLimit);
  finishPressureForProfile(Profile, After);

  unsigned BeforeOverflow = totalOverflow(Profile, Before);
  unsigned AfterOverflow = totalOverflow(Profile, After);
  bool Allow = true;
  StringRef Reason;
  if (AfterOverflow > BeforeOverflow) {
    Allow = false;
    Reason = "ring-overflow";
  } else if (!(After.HitLimit && !Before.HitLimit) && AfterOverflow != 0 &&
             After.Score > Before.Score + WasmTuneStackifyScoreHysteresis) {
    Allow = false;
    Reason = "score-delta";
  }

  LLVM_DEBUG({
    if (!Allow) {
      dbgs() << "wasm-tune-stackify: veto"
             << " tune=" << ST.getTuneCPUName() << " action=" << Action
             << " reason=" << Reason << " reg=" << Reg
             << " before-peak-fp=" << Before.PeakFP
             << " before-peak-int=" << Before.PeakInt
             << " after-peak-fp=" << After.PeakFP
             << " after-peak-int=" << After.PeakInt
             << " before-overflow=" << BeforeOverflow
             << " after-overflow=" << AfterOverflow
             << " before-score=" << Before.Score
             << " after-score=" << After.Score
             << " before-hit-limit=" << (Before.HitLimit ? "true" : "false")
             << " after-hit-limit=" << (After.HitLimit ? "true" : "false")
             << " hysteresis=" << WasmTuneStackifyScoreHysteresis << " def=";
      DefI->print(dbgs());
    }
  });

  return {Allow, !Allow};
}

static ProfileGateResult profileGateMove(const WebAssemblySubtarget &ST,
                                         Register Reg, MachineInstr *DefI,
                                         MachineInstr *Insert,
                                         const MachineRegisterInfo &MRI,
                                         const WebAssemblyFunctionInfo &MFI) {
  const WasmExecutionProfile &Profile = ST.getExecutionProfile();
  if (!shouldUseProfileGate(Profile, Insert))
    return {};

  WasmValueClass VC = classifyWasmReg(Reg, MRI);
  if (Profile.HasRegisterRing)
    return profileGateU2Delta(ST, Reg, DefI, Insert, MRI, MFI, "move");

  if (!Profile.HasM3SlotProviderModel)
    return {};

  WasmExecPressureResult R = estimateStackifiedTreePressure(
      *Insert, Reg, *DefI, MRI, MFI, WasmTuneStackifyNodeLimit);
  finishPressureForProfile(Profile, R);

  bool Allow = true;
  StringRef Reason;
  unsigned Dist = nonDebugDistance(DefI, Insert, /*Limit=*/16);
  if (VC == WasmValueClass::FP && Dist > 4) {
    Allow = false;
    Reason = "m3-fp-distance";
  } else if (VC == WasmValueClass::FP && R.PeakFP > 2) {
    Allow = false;
    Reason = "m3-fp-bank";
  }

  LLVM_DEBUG({
    if (!Allow) {
      dbgs() << "wasm-tune-stackify: veto"
             << " tune=" << ST.getTuneCPUName() << " action=move"
             << " reason=" << Reason << " reg=" << Reg
             << " peak-fp=" << R.PeakFP << " peak-int=" << R.PeakInt
             << " hit-limit=" << (R.HitLimit ? "true" : "false")
             << " cap-fp=" << Profile.FPRingCapacity << " distance=" << Dist
             << " cap-int=" << Profile.IntRingCapacity << " def=";
      DefI->print(dbgs());
    }
  });

  return {Allow, !Allow};
}

static ProfileGateResult
profileGateRematerialize(const WebAssemblySubtarget &ST, Register Reg,
                         MachineInstr *DefI, MachineInstr *Insert,
                         const MachineRegisterInfo &MRI,
                         const WebAssemblyFunctionInfo &MFI) {
  const WasmExecutionProfile &Profile = ST.getExecutionProfile();
  if (!shouldUseProfileGate(Profile, Insert))
    return {};

  // Rematerialization clones a cheap producer next to the consumer, so the
  // original DefI distance is not relevant to m3's provider-friendly shape.
  if (Profile.HasM3SlotProviderModel)
    return {};

  if (Profile.HasRegisterRing)
    return profileGateU2Delta(ST, Reg, DefI, Insert, MRI, MFI, "rematerialize");

  return {};
}

static ProfileGateResult profileGateTee(const WebAssemblySubtarget &ST,
                                        Register Reg, MachineInstr *DefI,
                                        MachineInstr *Insert,
                                        const MachineRegisterInfo &MRI,
                                        const WebAssemblyFunctionInfo &MFI) {
  const WasmExecutionProfile &Profile = ST.getExecutionProfile();
  if (!shouldUseProfileGate(Profile, Insert))
    return {};

  if (Profile.HasRegisterRing)
    return profileGateU2Delta(ST, Reg, DefI, Insert, MRI, MFI, "tee");

  if (!Profile.HasM3SlotProviderModel)
    return {};

  WasmValueClass VC = classifyWasmReg(Reg, MRI);
  unsigned Dist = nonDebugDistance(DefI, Insert, /*Limit=*/8);
  WasmExecPressureResult R = estimateStackifiedTreePressure(
      *Insert, Reg, *DefI, MRI, MFI, WasmTuneStackifyNodeLimit);
  finishPressureForProfile(Profile, R);

  bool Allow = true;
  StringRef Reason;
  if (VC == WasmValueClass::FP && Dist > 3) {
    Allow = false;
    Reason = "m3-tee-distance";
  } else if (VC == WasmValueClass::FP && R.PeakFP > 2) {
    Allow = false;
    Reason = "m3-fp-bank";
  }

  LLVM_DEBUG({
    if (!Allow) {
      dbgs() << "wasm-tune-stackify: veto"
             << " tune=" << ST.getTuneCPUName() << " action=tee"
             << " reason=" << Reason << " reg=" << Reg
             << " peak-fp=" << R.PeakFP << " peak-int=" << R.PeakInt
             << " hit-limit=" << (R.HitLimit ? "true" : "false")
             << " distance=" << Dist << " def=";
      DefI->print(dbgs());
    }
  });

  return {Allow, !Allow};
}

static WasmExecPressureResult estimatePressureForProfileCandidate(
    MachineInstr *Insert, Register CandidateReg, MachineInstr *CandidateDef,
    const MachineRegisterInfo &MRI, const WebAssemblyFunctionInfo &MFI,
    const WasmExecutionProfile &Profile) {
  WasmExecPressureResult R =
      CandidateDef
          ? estimateStackifiedTreePressure(*Insert, CandidateReg, *CandidateDef,
                                           MRI, MFI, WasmTuneStackifyNodeLimit)
          : estimateStackifiedTreePressure(*Insert, MRI, MFI,
                                           WasmTuneStackifyNodeLimit);
  finishPressureForProfile(Profile, R);
  return R;
}

static unsigned estimateRemainingUseDistance(Register Reg, MachineInstr *Insert,
                                             const MachineRegisterInfo &MRI,
                                             unsigned Limit) {
  unsigned MaxDistance = 0;
  for (const MachineOperand &MO : MRI.use_nodbg_operands(Reg)) {
    const MachineInstr *UseMI = MO.getParent();
    if (UseMI == Insert || UseMI->getParent() != Insert->getParent())
      continue;
    unsigned Dist = nonDebugDistance(Insert, UseMI, Limit);
    MaxDistance = std::max(MaxDistance, Dist);
  }
  return MaxDistance;
}

static StackifyActionCandidate scoreStackifyAction(
    StackifyActionKind Kind, bool Legal, Register Reg, MachineInstr *DefI,
    MachineInstr *Insert, const MachineRegisterInfo &MRI,
    const WebAssemblyFunctionInfo &MFI, const WasmExecutionProfile &Profile) {
  StackifyActionCandidate Candidate;
  Candidate.Kind = Kind;
  Candidate.Legal = Legal;
  if (!Legal)
    return Candidate;

  Candidate.Pressure = Kind == StackifyActionKind::KeepLocal
                           ? estimatePressureForProfileCandidate(
                                 Insert, Register(), nullptr, MRI, MFI, Profile)
                           : estimatePressureForProfileCandidate(
                                 Insert, Reg, DefI, MRI, MFI, Profile);
  Candidate.Score = Candidate.Pressure.Score;

  if (Kind == StackifyActionKind::Rematerialize) {
    Candidate.Score += Profile.CodeSizeCost;
    if (Profile.HasRegisterRing)
      Candidate.Score += Profile.DispatchCost;
  } else if (Kind == StackifyActionKind::Tee) {
    Candidate.Score += Profile.TeeCost;
    unsigned RemainingUseDistance =
        estimateRemainingUseDistance(Reg, Insert, MRI, /*Limit=*/16);
    if (Profile.HasM3SlotProviderModel && RemainingUseDistance > 3)
      Candidate.Score += (RemainingUseDistance - 3) * Profile.LocalGetCost;
    else if (Profile.HasRegisterRing && RemainingUseDistance > 8)
      Candidate.Score += RemainingUseDistance - 8;
  }

  if (Kind != StackifyActionKind::KeepLocal)
    Candidate.Score += productBankBoundaryPenalty(Profile, Reg, DefI, Insert,
                                                  MRI, Candidate.Pressure);

  return Candidate;
}

static bool isBetterAction(const StackifyActionCandidate &Candidate,
                           const StackifyActionCandidate &Best) {
  if (!Candidate.Legal)
    return false;
  if (!Best.Legal)
    return true;
  if (Candidate.Score != Best.Score)
    return Candidate.Score < Best.Score;
  return actionPriority(Candidate.Kind) < actionPriority(Best.Kind);
}

static StackifyActionCandidate
chooseProfileActionCandidate(const WebAssemblySubtarget &ST, Register Reg,
                             MachineInstr *DefI, MachineInstr *Insert,
                             const MachineRegisterInfo &MRI,
                             const WebAssemblyFunctionInfo &MFI, bool MoveLegal,
                             const ProfileGateResult &MoveGate, bool RematLegal,
                             const ProfileGateResult &RematGate, bool TeeLegal,
                             const ProfileGateResult &TeeGate) {
  const WasmExecutionProfile &Profile = ST.getExecutionProfile();
  StackifyActionCandidate Keep =
      scoreStackifyAction(StackifyActionKind::KeepLocal, /*Legal=*/true, Reg,
                          DefI, Insert, MRI, MFI, Profile);
  StackifyActionCandidate Move =
      scoreStackifyAction(StackifyActionKind::Move, MoveLegal && MoveGate.Allow,
                          Reg, DefI, Insert, MRI, MFI, Profile);
  StackifyActionCandidate Remat = scoreStackifyAction(
      StackifyActionKind::Rematerialize, RematLegal && RematGate.Allow, Reg,
      DefI, Insert, MRI, MFI, Profile);
  StackifyActionCandidate Tee =
      scoreStackifyAction(StackifyActionKind::Tee, TeeLegal && TeeGate.Allow,
                          Reg, DefI, Insert, MRI, MFI, Profile);

  StackifyActionCandidate Historical = Keep;
  if (Move.Legal)
    Historical = Move;
  else if (Remat.Legal)
    Historical = Remat;
  else if (Tee.Legal)
    Historical = Tee;

  StackifyActionCandidate Best = Historical;
  if (shouldPreferProductBankBoundary(Profile, Reg, DefI, Insert, MRI,
                                      Historical.Pressure) &&
      isBetterAction(Keep, Best))
    Best = Keep;
  if (isBetterAction(Move, Best))
    Best = Move;
  if (isBetterAction(Remat, Best))
    Best = Remat;
  if (isBetterAction(Tee, Best))
    Best = Tee;

  // The bounded estimator can undercount a newly inspected tree. Do not choose
  // a non-historical action only because a partial estimate looks better.
  if (Best.Pressure.HitLimit && !Historical.Pressure.HitLimit)
    Best = Historical;

  StackifyActionCandidate Chosen = Historical;
  if (Best.Kind != Historical.Kind &&
      Best.Score + WasmTuneStackifyScoreHysteresis < Historical.Score)
    Chosen = Best;

  LLVM_DEBUG({
    if (Chosen.Kind != Historical.Kind) {
      dbgs() << "wasm-tune-stackify: choose-action"
             << " tune=" << ST.getTuneCPUName()
             << " historical=" << actionName(Historical.Kind)
             << " chosen=" << actionName(Chosen.Kind)
             << " historical-score=" << Historical.Score
             << " chosen-score=" << Chosen.Score << " historical-overflow="
             << totalOverflow(Profile, Historical.Pressure)
             << " chosen-overflow=" << totalOverflow(Profile, Chosen.Pressure)
             << "\n";
    }
  });

  return Chosen;
}

static bool isProfileCommuteBetter(const WasmExecutionProfile &Profile,
                                   const WasmExecPressureResult &Before,
                                   const WasmExecPressureResult &After) {
  if (After.HitLimit && !Before.HitLimit)
    return false;

  unsigned BeforeOverflow = totalOverflow(Profile, Before);
  unsigned AfterOverflow = totalOverflow(Profile, After);
  if (AfterOverflow != BeforeOverflow)
    return AfterOverflow < BeforeOverflow;

  if (Profile.HasM3SlotProviderModel) {
    if (After.PeakFP != Before.PeakFP)
      return After.PeakFP < Before.PeakFP;
    if (After.PeakInt != Before.PeakInt)
      return After.PeakInt < Before.PeakInt;
  }

  return After.Score + WasmTuneStackifyScoreHysteresis < Before.Score;
}

static bool maybeProfileCommuteAfterVeto(
    MachineInstr *Insert, TreeWalkerState &TreeWalker,
    const WebAssemblySubtarget &ST, const MachineRegisterInfo &MRI,
    const WebAssemblyFunctionInfo &MFI, const WebAssemblyInstrInfo *TII,
    Register CandidateReg, MachineInstr *CandidateDef) {
  const WasmExecutionProfile &Profile = ST.getExecutionProfile();
  if (!shouldUseProfileGate(Profile, Insert))
    return false;
  if (Insert->isInlineAsm())
    return false;

  unsigned Operand0 = TargetInstrInfo::CommuteAnyOperandIndex;
  unsigned Operand1 = TargetInstrInfo::CommuteAnyOperandIndex;
  if (!TII->findCommutedOpIndices(*Insert, Operand0, Operand1))
    return false;

  WasmExecPressureResult Before = estimatePressureForProfileCandidate(
      Insert, CandidateReg, CandidateDef, MRI, MFI, Profile);

  MachineInstr *Commuted =
      TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
  if (!Commuted)
    return false;

  WasmExecPressureResult After = estimatePressureForProfileCandidate(
      Insert, CandidateReg, CandidateDef, MRI, MFI, Profile);

  if (isProfileCommuteBetter(Profile, Before, After)) {
    TreeWalker.restartOperands(Insert);
    LLVM_DEBUG(dbgs() << "wasm-tune-stackify: profile-commute"
                      << " tune=" << ST.getTuneCPUName() << " before-peak-fp="
                      << Before.PeakFP << " before-peak-int=" << Before.PeakInt
                      << " after-peak-fp=" << After.PeakFP
                      << " after-peak-int=" << After.PeakInt
                      << " before-overflow=" << totalOverflow(Profile, Before)
                      << " after-overflow=" << totalOverflow(Profile, After)
                      << " before-score=" << Before.Score
                      << " after-score=" << After.Score << " before-hit-limit="
                      << (Before.HitLimit ? "true" : "false")
                      << " after-hit-limit="
                      << (After.HitLimit ? "true" : "false") << "\n");
    return true;
  }

  [[maybe_unused]] MachineInstr *Restored =
      TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
  assert(Restored == Insert && "Expected in-place commute restoration");
  return false;
}

bool WebAssemblyRegStackify::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Register Stackifying **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  const auto &WasmST = MF.getSubtarget<WebAssemblySubtarget>();
  const auto *TII = WasmST.getInstrInfo();
  MachineDominatorTree *MDT = nullptr;
  LiveIntervals *LIS = nullptr;
  if (Optimize) {
    MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
    LIS = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  }

  // Walk the instructions from the bottom up. Currently we don't look past
  // block boundaries, and the blocks aren't ordered so the block visitation
  // order isn't significant, but we may want to change this in the future.
  for (MachineBasicBlock &MBB : MF) {
    // Don't use a range-based for loop, because we modify the list as we're
    // iterating over it and the end iterator may change.
    for (auto MII = MBB.rbegin(); MII != MBB.rend(); ++MII) {
      MachineInstr *Insert = &*MII;
      // Don't nest anything inside an inline asm, because we don't have
      // constraints for $push inputs.
      if (Insert->isInlineAsm())
        continue;

      // Ignore debugging intrinsics.
      if (Insert->isDebugValue())
        continue;

      // Ignore FAKE_USEs, which are no-ops and will be deleted later.
      if (Insert->isFakeUse())
        continue;

      // Iterate through the inputs in reverse order, since we'll be pulling
      // operands off the stack in LIFO order.
      CommutingState Commuting;
      SmallPtrSet<MachineInstr *, 4> ProfileCommuteTried;
      TreeWalkerState TreeWalker(Insert);
      while (!TreeWalker.done()) {
        MachineOperand &Use = TreeWalker.pop();

        // We're only interested in explicit virtual register operands.
        if (!Use.isReg())
          continue;

        Register Reg = Use.getReg();
        assert(Use.isUse() && "explicit_uses() should only iterate over uses");
        assert(!Use.isImplicit() &&
               "explicit_uses() should only iterate over explicit operands");
        if (Reg.isPhysical())
          continue;

        // Identify the definition for this register at this point.
        MachineInstr *DefI = getVRegDef(Reg, Insert, MRI, LIS);
        if (!DefI)
          continue;

        // Don't nest an INLINE_ASM def into anything, because we don't have
        // constraints for $pop outputs.
        if (DefI->isInlineAsm())
          continue;

        // Argument instructions represent live-in registers and not real
        // instructions.
        if (WebAssembly::isArgument(DefI->getOpcode()))
          continue;

        MachineOperand *Def =
            DefI->findRegisterDefOperand(Reg, /*TRI=*/nullptr);
        assert(Def != nullptr);

        // Decide which strategy to take. Prefer to move a single-use value
        // over cloning it, and prefer cloning over introducing a tee.
        // For moving, we require the def to be in the same block as the use;
        // this makes things simpler (LiveIntervals' handleMove function only
        // supports intra-block moves) and it's MachineSink's job to catch all
        // the sinking opportunities anyway.
        bool SameBlock = DefI->getParent() == &MBB;
        bool CanMove = SameBlock &&
                       isSafeToMove(Def, &Use, Insert, MFI, MRI, Optimize) &&
                       !TreeWalker.isOnStack(Reg);
        bool MoveLegal =
            CanMove && hasSingleUse(Reg, MRI, MF, Optimize, DefI, LIS);
        bool RematLegal = Optimize && shouldRematerialize(*DefI, TII);
        bool TeeLegal =
            Optimize && CanMove &&
            oneUseDominatesOtherUses(Reg, Use, MBB, MRI, *MDT, *LIS, MFI);

        bool Stackified = false;
        bool ProfileVetoed = false;
        Register ProfileVetoReg;
        MachineInstr *ProfileVetoDef = nullptr;
        auto RecordProfileVeto = [&](const ProfileGateResult &Gate) {
          if (!Gate.ProfileVeto)
            return;
          ProfileVetoed = true;
          if (!ProfileVetoDef) {
            ProfileVetoReg = Reg;
            ProfileVetoDef = DefI;
          }
        };

        ProfileGateResult MoveGate;
        ProfileGateResult RematGate;
        ProfileGateResult TeeGate;
        if (MoveLegal) {
          MoveGate = profileGateMove(WasmST, Reg, DefI, Insert, MRI, MFI);
          RecordProfileVeto(MoveGate);
        }
        if (RematLegal) {
          RematGate =
              profileGateRematerialize(WasmST, Reg, DefI, Insert, MRI, MFI);
          RecordProfileVeto(RematGate);
        }
        if (TeeLegal) {
          TeeGate = profileGateTee(WasmST, Reg, DefI, Insert, MRI, MFI);
          RecordProfileVeto(TeeGate);
        }

        bool HasAllowedStackify = (MoveLegal && MoveGate.Allow) ||
                                  (RematLegal && RematGate.Allow) ||
                                  (TeeLegal && TeeGate.Allow);

        StackifyActionKind SelectedAction = StackifyActionKind::KeepLocal;
        if (shouldUseProfileGate(WasmST.getExecutionProfile(), Insert)) {
          SelectedAction =
              chooseProfileActionCandidate(WasmST, Reg, DefI, Insert, MRI, MFI,
                                           MoveLegal, MoveGate, RematLegal,
                                           RematGate, TeeLegal, TeeGate)
                  .Kind;
        } else if (MoveLegal && MoveGate.Allow) {
          SelectedAction = StackifyActionKind::Move;
        } else if (RematLegal && RematGate.Allow) {
          SelectedAction = StackifyActionKind::Rematerialize;
        } else if (TeeLegal && TeeGate.Allow) {
          SelectedAction = StackifyActionKind::Tee;
        }

        switch (SelectedAction) {
        case StackifyActionKind::Move:
          if (MoveLegal && MoveGate.Allow) {
            Insert =
                moveForSingleUse(Reg, Use, DefI, MBB, Insert, LIS, MFI, MRI);

            // If we are removing the frame base reg completely, remove the
            // debug info as well.
            // TODO: Encode this properly as a stackified value.
            if (MFI.isFrameBaseVirtual() && MFI.getFrameBaseVreg() == Reg) {
              assert(Optimize && "Stackifying away frame base in unoptimized "
                                 "code not expected");
              MFI.clearFrameBaseVreg();
            }
            Stackified = true;
          }
          break;
        case StackifyActionKind::Rematerialize:
          if (RematLegal && RematGate.Allow) {
            Insert = rematerializeCheapDef(
                Reg, Use, *DefI, Insert->getIterator(), *LIS, MFI, MRI, TII);
            Stackified = true;
          }
          break;
        case StackifyActionKind::Tee:
          if (TeeLegal && TeeGate.Allow) {
            Insert = moveAndTeeForMultiUse(Reg, Use, DefI, MBB, Insert, *LIS,
                                           MFI, MRI, TII);
            Stackified = true;
          }
          break;
        case StackifyActionKind::KeepLocal:
          break;
        }

        if (!Stackified) {
          // We failed to stackify the operand. If the problem was ordering
          // constraints, Commuting may be able to help.
          if (SameBlock && ProfileVetoed && !HasAllowedStackify &&
              !ProfileCommuteTried.contains(Insert) &&
              maybeProfileCommuteAfterVeto(Insert, TreeWalker, WasmST, MRI, MFI,
                                           TII, ProfileVetoReg,
                                           ProfileVetoDef)) {
            ProfileCommuteTried.insert(Insert);
            Commuting.reset();
            Changed = true;
            continue;
          }
          if (SameBlock && ProfileVetoed)
            ProfileCommuteTried.insert(Insert);
          if (!CanMove && SameBlock)
            Commuting.maybeCommute(Insert, TreeWalker, TII);
          // Proceed to the next operand.
          continue;
        }

        // Stackifying a multivalue def may unlock in-place stackification of
        // subsequent defs. TODO: Handle the case where the consecutive uses are
        // not all in the same instruction.
        auto *SubsequentDef = Insert->defs().begin();
        auto *SubsequentUse = &Use;
        while (SubsequentDef != Insert->defs().end() &&
               SubsequentUse != Use.getParent()->uses().end()) {
          if (!SubsequentDef->isReg() || !SubsequentUse->isReg())
            break;
          Register DefReg = SubsequentDef->getReg();
          Register UseReg = SubsequentUse->getReg();
          // TODO: This single-use restriction could be relaxed by using tees
          if (DefReg != UseReg ||
              !hasSingleUse(DefReg, MRI, MF, Optimize, nullptr, nullptr))
            break;
          MFI.stackifyVReg(MRI, DefReg);
          ++SubsequentDef;
          ++SubsequentUse;
        }

        // If the instruction we just stackified is an IMPLICIT_DEF, convert it
        // to a constant 0 so that the def is explicit, and the push/pop
        // correspondence is maintained.
        if (Insert->getOpcode() == TargetOpcode::IMPLICIT_DEF)
          convertImplicitDefToConstZero(Insert, MRI, TII, MF);

        // We stackified an operand. Add the defining instruction's operands to
        // the worklist stack now to continue to build an ever deeper tree.
        Commuting.reset();
        TreeWalker.pushOperands(Insert);
      }

      // If we stackified any operands, skip over the tree to start looking for
      // the next instruction we can build a tree on.
      if (Insert != &*MII) {
        imposeStackOrdering(&*MII);
        MII = MachineBasicBlock::iterator(Insert).getReverse();
        Changed = true;
      }
    }
  }

  // If we used VALUE_STACK anywhere, add it to the live-in sets everywhere so
  // that it never looks like a use-before-def.
  if (Changed) {
    MF.getRegInfo().addLiveIn(WebAssembly::VALUE_STACK);
    for (MachineBasicBlock &MBB : MF)
      MBB.addLiveIn(WebAssembly::VALUE_STACK);
  }

  if (WasmExecPressureDump)
    dumpExecutionPressure(MF, WasmST, MFI, MRI);

#ifndef NDEBUG
  // Verify that pushes and pops are performed in LIFO order.
  SmallVector<unsigned, 0> Stack;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      for (MachineOperand &MO : reverse(MI.explicit_uses())) {
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        if (MFI.isVRegStackified(Reg))
          assert(Stack.pop_back_val() == Reg &&
                 "Register stack pop should be paired with a push");
      }
      for (MachineOperand &MO : MI.defs()) {
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        if (MFI.isVRegStackified(Reg))
          Stack.push_back(MO.getReg());
      }
    }
    // TODO: Generalize this code to support keeping values on the stack across
    // basic block boundaries.
    assert(Stack.empty() &&
           "Register stack pushes and pops should be balanced");
  }
#endif

  return Changed;
}
