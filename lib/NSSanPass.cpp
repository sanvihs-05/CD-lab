#include "nssan/NSSanRuntime.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include <string>

using namespace llvm;

namespace {

using ShadowMap = DenseMap<Value *, Value *>;

bool isTrackedFloatType(Type *Ty) {
  return Ty != nullptr && Ty->isFloatTy();
}

Instruction *getInsertionPointAfter(Instruction &I) {
  auto It = I.getIterator();
  ++It;
  return &*It;
}

std::string locationPath(const DILocation *Loc) {
  if (Loc == nullptr) {
    return "<unknown>";
  }

  const DIScope *Scope = Loc->getScope();
  if (Scope == nullptr || Scope->getFile() == nullptr) {
    return "<unknown>";
  }

  const DIFile *File = Scope->getFile();
  std::string Name = File->getFilename().str();
  std::string Directory = File->getDirectory().str();

  if (!Directory.empty()) {
    return Directory + "/" + Name;
  }

  return Name.empty() ? "<unknown>" : Name;
}

FunctionCallee getShadowStoreDecl(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *VoidTy = Type::getVoidTy(Ctx);
  Type *PtrTy = PointerType::getUnqual(Ctx);
  Type *DoubleTy = Type::getDoubleTy(Ctx);
  return M.getOrInsertFunction("__nssan_shadow_store", VoidTy, PtrTy, DoubleTy);
}

FunctionCallee getShadowLoadDecl(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::getUnqual(Ctx);
  Type *FloatTy = Type::getFloatTy(Ctx);
  Type *DoubleTy = Type::getDoubleTy(Ctx);
  return M.getOrInsertFunction("__nssan_shadow_load", DoubleTy, PtrTy, FloatTy);
}

FunctionCallee getCheckDecl(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *VoidTy = Type::getVoidTy(Ctx);
  Type *FloatTy = Type::getFloatTy(Ctx);
  Type *DoubleTy = Type::getDoubleTy(Ctx);
  Type *I8PtrTy = PointerType::getUnqual(Ctx);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  return M.getOrInsertFunction("__nssan_check_float_op",
                               VoidTy,
                               FloatTy,
                               DoubleTy,
                               I8PtrTy,
                               I32Ty,
                               I32Ty,
                               I8PtrTy);
}

Value *castPointerToBytePtr(IRBuilder<> &Builder, Value *Ptr) {
  return Builder.CreatePointerCast(Ptr, Builder.getPtrTy());
}

Value *getOrCreateShadow(Value *ValueOperand,
                         Instruction *InsertBefore,
                         ShadowMap &Shadows) {
  if (!isTrackedFloatType(ValueOperand->getType())) {
    return nullptr;
  }

  if (auto It = Shadows.find(ValueOperand); It != Shadows.end()) {
    return It->second;
  }

  Type *DoubleTy = Type::getDoubleTy(ValueOperand->getContext());

  if (auto *Constant = dyn_cast<ConstantFP>(ValueOperand)) {
    return ConstantFP::get(DoubleTy, Constant->getValueAPF().convertToDouble());
  }

  if (isa<UndefValue>(ValueOperand)) {
    return UndefValue::get(DoubleTy);
  }

  if (auto *Arg = dyn_cast<Argument>(ValueOperand)) {
    IRBuilder<> EntryBuilder(&*Arg->getParent()->getEntryBlock().getFirstInsertionPt());
    Value *Shadow = EntryBuilder.CreateFPExt(Arg, DoubleTy, Arg->getName() + ".nssan.shadow");
    Shadows[Arg] = Shadow;
    return Shadow;
  }

  IRBuilder<> Builder(InsertBefore);
  return Builder.CreateFPExt(ValueOperand, DoubleTy, "nssan.cast");
}

Function *getUnaryMathShadowCallee(Module &M, StringRef Name) {
  LLVMContext &Ctx = M.getContext();
  Type *DoubleTy = Type::getDoubleTy(Ctx);
  FunctionType *FnTy = FunctionType::get(DoubleTy, {DoubleTy}, false);

  if (Name == "sqrtf") {
    return cast<Function>(M.getOrInsertFunction("sqrt", FnTy).getCallee());
  }
  if (Name == "sinf") {
    return cast<Function>(M.getOrInsertFunction("sin", FnTy).getCallee());
  }
  if (Name == "cosf") {
    return cast<Function>(M.getOrInsertFunction("cos", FnTy).getCallee());
  }
  if (Name == "expf") {
    return cast<Function>(M.getOrInsertFunction("exp", FnTy).getCallee());
  }
  if (Name == "logf") {
    return cast<Function>(M.getOrInsertFunction("log", FnTy).getCallee());
  }

  return nullptr;
}

Function *getTernaryMathShadowCallee(Module &M, StringRef Name) {
  LLVMContext &Ctx = M.getContext();
  Type *DoubleTy = Type::getDoubleTy(Ctx);
  FunctionType *FnTy = FunctionType::get(DoubleTy, {DoubleTy, DoubleTy, DoubleTy}, false);

  if (Name == "fmaf") {
    return cast<Function>(M.getOrInsertFunction("fma", FnTy).getCallee());
  }

  return nullptr;
}

Value *buildIntrinsicShadow(Module &M,
                            IntrinsicInst &II,
                            Value *ShadowArg,
                            Instruction *InsertBefore) {
  LLVMContext &Ctx = M.getContext();
  Type *DoubleTy = Type::getDoubleTy(Ctx);
  IRBuilder<> Builder(InsertBefore);

  Intrinsic::ID Id = II.getIntrinsicID();
  if (Id == Intrinsic::sqrt || Id == Intrinsic::sin || Id == Intrinsic::cos ||
      Id == Intrinsic::exp || Id == Intrinsic::log || Id == Intrinsic::fabs) {
    Function *ShadowFn = Intrinsic::getDeclaration(&M, Id, {DoubleTy});
    return Builder.CreateCall(ShadowFn, {ShadowArg}, "nssan.math.shadow");
  }

  return nullptr;
}

Value *buildTernaryIntrinsicShadow(Module &M,
                                   IntrinsicInst &II,
                                   Value *ShadowArg0,
                                   Value *ShadowArg1,
                                   Value *ShadowArg2,
                                   Instruction *InsertBefore) {
  LLVMContext &Ctx = M.getContext();
  Type *DoubleTy = Type::getDoubleTy(Ctx);
  IRBuilder<> Builder(InsertBefore);

  Intrinsic::ID Id = II.getIntrinsicID();
  if (Id == Intrinsic::fma || Id == Intrinsic::fmuladd) {
    Function *ShadowFn = Intrinsic::getDeclaration(&M, Intrinsic::fma, {DoubleTy});
    return Builder.CreateCall(ShadowFn,
                              {ShadowArg0, ShadowArg1, ShadowArg2},
                              "nssan.math.shadow");
  }

  return nullptr;
}

void emitCheck(BinaryOperator &BO, Value *ShadowResult, Instruction *InsertBefore) {
  Module &M = *BO.getModule();
  IRBuilder<> Builder(InsertBefore);
  DILocation *Loc = BO.getDebugLoc().get();

  Value *File = Builder.CreateGlobalStringPtr(locationPath(Loc));
  Value *Line = Builder.getInt32(Loc == nullptr ? 0U : Loc->getLine());
  Value *Column = Builder.getInt32(Loc == nullptr ? 0U : Loc->getColumn());
  Value *OpName = Builder.CreateGlobalStringPtr(BO.getOpcodeName());

  Builder.CreateCall(getCheckDecl(M),
                     {&BO, ShadowResult, File, Line, Column, OpName});
}

void emitCheck(CallInst &CI,
               Value *ShadowResult,
               Instruction *InsertBefore,
               StringRef OpName) {
  Module &M = *CI.getModule();
  IRBuilder<> Builder(InsertBefore);
  DILocation *Loc = CI.getDebugLoc().get();

  Value *File = Builder.CreateGlobalStringPtr(locationPath(Loc));
  Value *Line = Builder.getInt32(Loc == nullptr ? 0U : Loc->getLine());
  Value *Column = Builder.getInt32(Loc == nullptr ? 0U : Loc->getColumn());
  Value *OpNameValue = Builder.CreateGlobalStringPtr(OpName);

  Builder.CreateCall(getCheckDecl(M),
                     {&CI, ShadowResult, File, Line, Column, OpNameValue});
}

bool instrumentLoad(LoadInst &LI, ShadowMap &Shadows) {
  if (!isTrackedFloatType(LI.getType())) {
    return false;
  }

  Module &M = *LI.getModule();
  IRBuilder<> Builder(getInsertionPointAfter(LI));
  Value *Ptr = castPointerToBytePtr(Builder, LI.getPointerOperand());
  Value *Shadow =
      Builder.CreateCall(getShadowLoadDecl(M), {Ptr, &LI}, LI.getName() + ".nssan.shadow");
  Shadows[&LI] = Shadow;
  return true;
}

bool instrumentStore(StoreInst &SI, ShadowMap &Shadows) {
  if (!isTrackedFloatType(SI.getValueOperand()->getType())) {
    return false;
  }

  Module &M = *SI.getModule();
  IRBuilder<> Builder(&SI);
  Value *Shadow = getOrCreateShadow(SI.getValueOperand(), &SI, Shadows);
  Value *Ptr = castPointerToBytePtr(Builder, SI.getPointerOperand());
  Builder.CreateCall(getShadowStoreDecl(M), {Ptr, Shadow});
  return true;
}

bool instrumentBinaryOp(BinaryOperator &BO, ShadowMap &Shadows) {
  if (!isTrackedFloatType(BO.getType())) {
    return false;
  }

  Instruction *InsertBefore = getInsertionPointAfter(BO);
  IRBuilder<> Builder(InsertBefore);
  Value *LhsShadow = getOrCreateShadow(BO.getOperand(0), InsertBefore, Shadows);
  Value *RhsShadow = getOrCreateShadow(BO.getOperand(1), InsertBefore, Shadows);

  if (LhsShadow == nullptr || RhsShadow == nullptr) {
    return false;
  }

  Value *ShadowResult = nullptr;
  switch (BO.getOpcode()) {
  case Instruction::FAdd:
    ShadowResult = Builder.CreateFAdd(LhsShadow, RhsShadow, "nssan.shadow");
    break;
  case Instruction::FSub:
    ShadowResult = Builder.CreateFSub(LhsShadow, RhsShadow, "nssan.shadow");
    break;
  case Instruction::FMul:
    ShadowResult = Builder.CreateFMul(LhsShadow, RhsShadow, "nssan.shadow");
    break;
  case Instruction::FDiv:
    ShadowResult = Builder.CreateFDiv(LhsShadow, RhsShadow, "nssan.shadow");
    break;
  default:
    return false;
  }

  if (auto *ShadowInst = dyn_cast<Instruction>(ShadowResult)) {
    ShadowInst->copyFastMathFlags(&BO);
  }

  Shadows[&BO] = ShadowResult;
  Instruction *CheckInsertBefore = InsertBefore;
  if (auto *ShadowInst = dyn_cast<Instruction>(ShadowResult)) {
    CheckInsertBefore = getInsertionPointAfter(*ShadowInst);
  }

  emitCheck(BO, ShadowResult, CheckInsertBefore);
  return true;
}

bool instrumentMathCall(CallInst &CI, ShadowMap &Shadows) {
  if (!isTrackedFloatType(CI.getType())) {
    return false;
  }

  Module &M = *CI.getModule();
  Instruction *InsertBefore = getInsertionPointAfter(CI);
  Value *Shadow = nullptr;
  StringRef ShadowName = "math";

  if (CI.arg_size() == 1 && isTrackedFloatType(CI.getArgOperand(0)->getType())) {
    Value *ArgShadow = getOrCreateShadow(CI.getArgOperand(0), InsertBefore, Shadows);
    if (ArgShadow == nullptr) {
      return false;
    }

    IRBuilder<> Builder(InsertBefore);
    if (auto *II = dyn_cast<IntrinsicInst>(&CI)) {
      Shadow = buildIntrinsicShadow(M, *II, ArgShadow, InsertBefore);
      switch (II->getIntrinsicID()) {
      case Intrinsic::sqrt:
        ShadowName = "sqrtf";
        break;
      case Intrinsic::sin:
        ShadowName = "sinf";
        break;
      case Intrinsic::cos:
        ShadowName = "cosf";
        break;
      case Intrinsic::exp:
        ShadowName = "expf";
        break;
      case Intrinsic::log:
        ShadowName = "logf";
        break;
      case Intrinsic::fabs:
        ShadowName = "fabsf";
        break;
      default:
        break;
      }
    } else if (Function *Callee = CI.getCalledFunction()) {
      if (Function *ShadowCallee = getUnaryMathShadowCallee(M, Callee->getName())) {
        Shadow = Builder.CreateCall(ShadowCallee, {ArgShadow}, "nssan.math.shadow");
        ShadowName = Callee->getName();
      }
    }
  } else if (CI.arg_size() == 3 && isTrackedFloatType(CI.getArgOperand(0)->getType()) &&
             isTrackedFloatType(CI.getArgOperand(1)->getType()) &&
             isTrackedFloatType(CI.getArgOperand(2)->getType())) {
    Value *Arg0Shadow = getOrCreateShadow(CI.getArgOperand(0), InsertBefore, Shadows);
    Value *Arg1Shadow = getOrCreateShadow(CI.getArgOperand(1), InsertBefore, Shadows);
    Value *Arg2Shadow = getOrCreateShadow(CI.getArgOperand(2), InsertBefore, Shadows);
    if (Arg0Shadow == nullptr || Arg1Shadow == nullptr || Arg2Shadow == nullptr) {
      return false;
    }

    IRBuilder<> Builder(InsertBefore);
    if (auto *II = dyn_cast<IntrinsicInst>(&CI)) {
      Shadow =
          buildTernaryIntrinsicShadow(M, *II, Arg0Shadow, Arg1Shadow, Arg2Shadow, InsertBefore);
      switch (II->getIntrinsicID()) {
      case Intrinsic::fma:
      case Intrinsic::fmuladd:
        ShadowName = "fmaf";
        break;
      default:
        break;
      }
    } else if (Function *Callee = CI.getCalledFunction()) {
      if (Function *ShadowCallee = getTernaryMathShadowCallee(M, Callee->getName())) {
        Shadow = Builder.CreateCall(ShadowCallee,
                                    {Arg0Shadow, Arg1Shadow, Arg2Shadow},
                                    "nssan.math.shadow");
        ShadowName = Callee->getName();
      }
    }
  }

  if (Shadow == nullptr) {
    return false;
  }

  Shadows[&CI] = Shadow;
  Instruction *CheckInsertBefore = InsertBefore;
  if (auto *ShadowInst = dyn_cast<Instruction>(Shadow)) {
    CheckInsertBefore = getInsertionPointAfter(*ShadowInst);
  }

  emitCheck(CI, Shadow, CheckInsertBefore, ShadowName);
  return true;
}

class NSSanFunctionPass : public PassInfoMixin<NSSanFunctionPass> {
public:
  static bool isRequired() { return true; }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    if (F.isDeclaration()) {
      return PreservedAnalyses::all();
    }

    bool Changed = false;
    ShadowMap Shadows;

    for (Instruction &I : make_early_inc_range(instructions(F))) {
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        Changed |= instrumentLoad(*LI, Shadows);
        continue;
      }

      if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
        Changed |= instrumentBinaryOp(*BO, Shadows);
        continue;
      }

      if (auto *CI = dyn_cast<CallInst>(&I)) {
        Changed |= instrumentMathCall(*CI, Shadows);
        continue;
      }

      if (auto *SI = dyn_cast<StoreInst>(&I)) {
        Changed |= instrumentStore(*SI, Shadows);
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "NSSanPass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "nssan") {
                    return false;
                  }
                  FPM.addPass(NSSanFunctionPass());
                  return true;
                });

            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                  MPM.addPass(createModuleToFunctionPassAdaptor(NSSanFunctionPass()));
                });
          }};
}
