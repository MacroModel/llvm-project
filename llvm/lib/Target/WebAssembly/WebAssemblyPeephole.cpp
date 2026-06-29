//===-- WebAssemblyPeephole.cpp - WebAssembly Peephole Optimiztions -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Late peephole optimizations for WebAssembly.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-peephole"

static cl::opt<bool> DisableWebAssemblyFallthroughReturnOpt(
    "disable-wasm-fallthrough-return-opt", cl::Hidden,
    cl::desc("WebAssembly: Disable fallthrough-return optimizations."),
    cl::init(false));

static cl::opt<bool> WasmTuneU2LoopLTCompare(
    "wasm-tune-u2-loop-lt-compare", cl::Hidden, cl::init(true),
    cl::desc("Enable u2-aapcs64 loop-tail i32.ne to i32.lt_u peephole"));

namespace {
class WebAssemblyPeephole final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly late peephole optimizer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<LibcallLoweringInfoWrapper>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID;
  WebAssemblyPeephole() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyPeephole::ID = 0;
INITIALIZE_PASS(WebAssemblyPeephole, DEBUG_TYPE,
                "WebAssembly peephole optimizations", false, false)

FunctionPass *llvm::createWebAssemblyPeephole() {
  return new WebAssemblyPeephole();
}

/// If desirable, rewrite NewReg to a drop register.
static bool maybeRewriteToDrop(unsigned OldReg, unsigned NewReg,
                               MachineOperand &MO, WebAssemblyFunctionInfo &MFI,
                               MachineRegisterInfo &MRI) {
  bool Changed = false;
  if (OldReg == NewReg) {
    Changed = true;
    Register NewReg = MRI.createVirtualRegister(MRI.getRegClass(OldReg));
    MO.setReg(NewReg);
    MO.setIsDead();
    MFI.stackifyVReg(MRI, NewReg);
  }
  return Changed;
}

static bool maybeRewriteToFallthrough(MachineInstr &MI, MachineBasicBlock &MBB,
                                      const MachineFunction &MF,
                                      WebAssemblyFunctionInfo &MFI,
                                      MachineRegisterInfo &MRI,
                                      const WebAssemblyInstrInfo &TII) {
  if (DisableWebAssemblyFallthroughReturnOpt)
    return false;
  if (&MBB != &MF.back())
    return false;

  MachineBasicBlock::iterator End = MBB.end();
  --End;
  assert(End->getOpcode() == WebAssembly::END_FUNCTION);
  --End;
  if (&MI != &*End)
    return false;

  for (auto &MO : MI.explicit_operands()) {
    // If the operand isn't stackified, insert a COPY to read the operands and
    // stackify them.
    Register Reg = MO.getReg();
    if (!MFI.isVRegStackified(Reg)) {
      unsigned CopyLocalOpc;
      const TargetRegisterClass *RegClass = MRI.getRegClass(Reg);
      CopyLocalOpc = WebAssembly::getCopyOpcodeForRegClass(RegClass);
      Register NewReg = MRI.createVirtualRegister(RegClass);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(CopyLocalOpc), NewReg)
          .addReg(Reg);
      MO.setReg(NewReg);
      MFI.stackifyVReg(MRI, NewReg);
    }
  }

  MI.setDesc(TII.get(WebAssembly::FALLTHROUGH_RETURN));
  return true;
}

static bool isConstI32(Register Reg, int64_t &Imm,
                       const MachineRegisterInfo &MRI) {
  if (!Reg.isVirtual() || !MRI.hasOneDef(Reg))
    return false;

  MachineInstr *Def = MRI.getVRegDef(Reg);
  if (!Def || Def->getOpcode() != WebAssembly::CONST_I32 ||
      Def->getNumOperands() < 2 || !Def->getOperand(1).isImm())
    return false;

  Imm = Def->getOperand(1).getImm();
  return true;
}

static bool getLocalImmediate(const MachineInstr &MI, unsigned OperandNo,
                              int64_t &Local) {
  if (MI.getNumOperands() <= OperandNo || !MI.getOperand(OperandNo).isImm())
    return false;
  Local = MI.getOperand(OperandNo).getImm();
  return true;
}

static bool isLocalGetI32Of(Register Reg, int64_t Local,
                            const MachineRegisterInfo &MRI) {
  if (!Reg.isVirtual() || !MRI.hasOneDef(Reg))
    return false;

  MachineInstr *Def = MRI.getVRegDef(Reg);
  int64_t GetLocal = -1;
  return Def && Def->getOpcode() == WebAssembly::LOCAL_GET_I32 &&
         getLocalImmediate(*Def, 1, GetLocal) && GetLocal == Local;
}

static bool isLocalWriteI32(const MachineInstr &MI, int64_t Local) {
  int64_t WrittenLocal = -1;
  switch (MI.getOpcode()) {
  case WebAssembly::LOCAL_SET_I32:
    return getLocalImmediate(MI, 0, WrittenLocal) && WrittenLocal == Local;
  case WebAssembly::LOCAL_TEE_I32:
    return getLocalImmediate(MI, 1, WrittenLocal) && WrittenLocal == Local;
  default:
    return false;
  }
}

static bool localWriteIsSetZero(const MachineInstr &MI, int64_t Local,
                                const MachineRegisterInfo &MRI) {
  if (MI.getOpcode() != WebAssembly::LOCAL_SET_I32)
    return false;

  int64_t WrittenLocal = -1;
  if (!getLocalImmediate(MI, 0, WrittenLocal) || WrittenLocal != Local)
    return false;

  if (MI.getNumOperands() < 2 || !MI.getOperand(1).isReg())
    return false;

  int64_t Imm = 0;
  return isConstI32(MI.getOperand(1).getReg(), Imm, MRI) && Imm == 0;
}

static bool
outsidePredecessorInitializesLocalToZero(const MachineBasicBlock &LoopMBB,
                                         int64_t Local,
                                         const MachineRegisterInfo &MRI) {
  bool HasSelfPred = false;
  const MachineBasicBlock *OutsidePred = nullptr;
  for (const MachineBasicBlock *Pred : LoopMBB.predecessors()) {
    if (Pred == &LoopMBB) {
      HasSelfPred = true;
      continue;
    }
    if (OutsidePred)
      return false;
    OutsidePred = Pred;
  }
  if (!HasSelfPred || !OutsidePred)
    return false;

  const MachineInstr *LastWrite = nullptr;
  for (const MachineInstr &PredMI : *OutsidePred)
    if (isLocalWriteI32(PredMI, Local))
      LastWrite = &PredMI;

  return LastWrite && localWriteIsSetZero(*LastWrite, Local, MRI);
}

static bool loopHasOnlyCandidateLocalWrite(MachineBasicBlock::iterator Loop,
                                           MachineBasicBlock::iterator Br,
                                           int64_t Local,
                                           const MachineInstr &CandidateTee) {
  for (auto I = std::next(Loop); I != Br; ++I) {
    if (&*I == &CandidateTee)
      continue;
    if (isLocalWriteI32(*I, Local))
      return false;
  }
  return true;
}

static bool maybeRewriteUWVM2ForLoopCompare(MachineInstr &Br,
                                            MachineBasicBlock &MBB,
                                            const MachineRegisterInfo &MRI,
                                            const WebAssemblyInstrInfo &TII) {
  if (Br.getOpcode() != WebAssembly::BR_IF || Br.getNumOperands() < 2 ||
      !Br.getOperand(1).isReg())
    return false;
  if (!Br.getOperand(0).isImm() || Br.getOperand(0).getImm() != 0)
    return false;

  Register CondReg = Br.getOperand(1).getReg();
  if (!CondReg.isVirtual() || !MRI.hasOneDef(CondReg))
    return false;

  MachineInstr *Cmp = MRI.getVRegDef(CondReg);
  if (!Cmp || Cmp->getOpcode() != WebAssembly::NE_I32 ||
      Cmp->getNumOperands() < 3 || !Cmp->getOperand(1).isReg() ||
      !Cmp->getOperand(2).isReg())
    return false;

  Register TeeReg = Cmp->getOperand(1).getReg();
  Register EndReg = Cmp->getOperand(2).getReg();
  int64_t EndImm = 0;
  if (!isConstI32(EndReg, EndImm, MRI) || EndImm <= 0)
    return false;

  if (!TeeReg.isVirtual() || !MRI.hasOneDef(TeeReg))
    return false;
  MachineInstr *Tee = MRI.getVRegDef(TeeReg);
  int64_t Local = -1;
  if (!Tee || Tee->getOpcode() != WebAssembly::LOCAL_TEE_I32 ||
      Tee->getNumOperands() < 3 || !getLocalImmediate(*Tee, 1, Local) ||
      !Tee->getOperand(2).isReg())
    return false;

  Register AddReg = Tee->getOperand(2).getReg();
  if (!AddReg.isVirtual() || !MRI.hasOneDef(AddReg))
    return false;
  MachineInstr *Add = MRI.getVRegDef(AddReg);
  if (!Add || Add->getOpcode() != WebAssembly::ADD_I32 ||
      Add->getNumOperands() < 3 || !Add->getOperand(1).isReg() ||
      !Add->getOperand(2).isReg())
    return false;

  int64_t StepImm = 0;
  if (!isLocalGetI32Of(Add->getOperand(1).getReg(), Local, MRI) ||
      !isConstI32(Add->getOperand(2).getReg(), StepImm, MRI) || StepImm != 1)
    return false;

  auto BrIt = Br.getIterator();
  auto LoopIt = MBB.end();
  for (auto I = BrIt; I != MBB.begin();) {
    --I;
    if (I->getOpcode() == WebAssembly::LOOP) {
      LoopIt = I;
      break;
    }
  }
  if (LoopIt == MBB.end())
    return false;

  for (auto I = MBB.begin(); I != LoopIt; ++I)
    if (isLocalWriteI32(*I, Local))
      return false;

  if (!loopHasOnlyCandidateLocalWrite(LoopIt, BrIt, Local, *Tee))
    return false;

  if (!outsidePredecessorInitializesLocalToZero(MBB, Local, MRI))
    return false;

  Cmp->setDesc(TII.get(WebAssembly::LT_U_I32));
  LLVM_DEBUG(dbgs() << "wasm-peephole: u2-aapcs64 for-loop cmp"
                    << " local=" << Local << " end=" << EndImm << "\n");
  return true;
}

bool WebAssemblyPeephole::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Peephole **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  const WebAssemblySubtarget &Subtarget =
      MF.getSubtarget<WebAssemblySubtarget>();
  const auto &TII = *Subtarget.getInstrInfo();
  auto &LibInfo =
      getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(MF.getFunction());

  const LibcallLoweringInfo &LibcallLowering =
      getAnalysis<LibcallLoweringInfoWrapper>().getLibcallLowering(
          *MF.getFunction().getParent(), Subtarget);

  RTLIB::LibcallImpl MemcpyImpl = LibcallLowering.getLibcallImpl(RTLIB::MEMCPY);
  RTLIB::LibcallImpl MemmoveImpl =
      LibcallLowering.getLibcallImpl(RTLIB::MEMMOVE);
  RTLIB::LibcallImpl MemsetImpl = LibcallLowering.getLibcallImpl(RTLIB::MEMSET);

  StringRef MemcpyName =
      RTLIB::RuntimeLibcallsInfo::getLibcallImplName(MemcpyImpl);
  StringRef MemmoveName =
      RTLIB::RuntimeLibcallsInfo::getLibcallImplName(MemmoveImpl);
  StringRef MemsetName =
      RTLIB::RuntimeLibcallsInfo::getLibcallImplName(MemsetImpl);

  bool Changed = false;

  for (auto &MBB : MF)
    for (auto &MI : MBB)
      switch (MI.getOpcode()) {
      default:
        break;
      case WebAssembly::CALL: {
        MachineOperand &Op1 = MI.getOperand(1);
        if (Op1.isSymbol()) {
          StringRef Name(Op1.getSymbolName());
          if (Name == MemcpyName || Name == MemmoveName || Name == MemsetName) {
            LibFunc Func;
            if (LibInfo.getLibFunc(Name, Func)) {
              const auto &Op2 = MI.getOperand(2);
              if (!Op2.isReg())
                report_fatal_error("Peephole: call to builtin function with "
                                   "wrong signature, not consuming reg");
              MachineOperand &MO = MI.getOperand(0);
              Register OldReg = MO.getReg();
              Register NewReg = Op2.getReg();

              if (MRI.getRegClass(NewReg) != MRI.getRegClass(OldReg))
                report_fatal_error("Peephole: call to builtin function with "
                                   "wrong signature, from/to mismatch");
              Changed |= maybeRewriteToDrop(OldReg, NewReg, MO, MFI, MRI);
            }
          }
        }
        break;
      }
      // Optimize away an explicit void return at the end of the function.
      case WebAssembly::RETURN:
        Changed |= maybeRewriteToFallthrough(MI, MBB, MF, MFI, MRI, TII);
        break;
      case WebAssembly::BR_IF:
        if (WasmTuneU2LoopLTCompare &&
            Subtarget.getExecutionProfile().Kind == WasmTuneKind::U2AAPCS64)
          Changed |= maybeRewriteUWVM2ForLoopCompare(MI, MBB, MRI, TII);
        break;
      }

  return Changed;
}
