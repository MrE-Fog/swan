//===--- InstructionVisitor.cpp - SIL to CAst Translator -----------------===//
//
// This source file is part of the SWAN open source project
//
// Copyright (c) 2019 Maple @ University of Alberta
// All rights reserved. This program and the accompanying materials (unless
// otherwise specified by a license inside of the accompanying material)
// are made available under the terms of the Eclipse Public License v2.0
// which accompanies this distribution, and is available at
// http://www.eclipse.org/legal/epl-v20.html
//
//===---------------------------------------------------------------------===//
///
/// This file implements the InstructionVisitor class, which inherits
/// the SILInstructionVisitor template class (part of the Swift compiler).
/// The SILInstructionVisitor translates a given SIL
/// (Swift Intermediate Language) Module to CAst (WALA IR).
///
//===---------------------------------------------------------------------===//

#include "InstructionVisitor.h"
#include "swift/AST/Module.h"
#include "swift/AST/Types.h"
#include "swift/Demangling/Demangle.h"
#include "swift/SIL/SILModule.h"
#include <fstream>
#include <memory>

// TODO: Figure out how to use variadic macros instead.
#define MAKE_NODE(x) Instance->CAst->makeNode(x)
#define MAKE_NODE2(x, y) Instance->CAst->makeNode(x, y)
#define MAKE_NODE3(x, y, z) Instance->CAst->makeNode(x, y, z)
#define MAKE_NODE4(x, y, z, z2) Instance->CAst->makeNode(x, y, z, z2)
#define MAKE_CONST(x) Instance->CAst->makeConstant(x)
#define MAKE_ARRAY(x) Instance->CAst->makeArray(x)
#define ADD_PROP(x) currentInstruction->addProperty(x)

using namespace swan;

//===------------------- MODULE/FUNCTION/BLOCK VISITORS ------------------===//

void InstructionVisitor::visitSILModule(SILModule *M) {
  moduleInfo = std::make_unique<SILModuleInfo>(M->getSwiftModule()->getModuleFilename());
  currentModule = std::make_unique<RootModuleInfo>(Instance->CAst);

  for (SILFunction &F: *M) {
    if (F.empty()) { // Most likely a builtin, so we ignore it.
      llvm::outs() << "Skipping " << Demangle::demangleSymbolAsString(F.getName()) << "\n";
      continue;
    }

    visitSILFunction(&F);
  }

  Instance->Root = currentModule->make();
}

void InstructionVisitor::visitSILFunction(SILFunction *F) {
  std::string const &demangledFunctionName = Demangle::demangleSymbolAsString(F->getName());
  functionInfo = std::make_unique<SILFunctionInfo>(F->getName(), demangledFunctionName);
  currentFunction = std::make_unique<RootFunctionInfo>(Instance->CAst);
  currentFunction->functionName = demangledFunctionName;

  // Set function source information.
  unsigned fl = 0, fc = 0, ll = 0, lc = 0;
  // Swift compiler doesn't seem to have a way of getting the specific location of a param.
  if (!F->getLocation().isNull()) {
    SourceManager const &srcMgr = F->getModule().getSourceManager();
    SourceRange const &srcRange = F->getLocation().getSourceRange();
    SourceLoc const &srcStart = srcRange.Start;
    SourceLoc const &srcEnd = srcRange.End;
    if (srcStart.isInvalid() || srcEnd.isInvalid()) {
      llvm::outs() << "WARNING: Source information is invalid for function: " << demangledFunctionName;
    } else {
      auto startLineCol = srcMgr.getLineAndColumn(srcStart);
      fl = startLineCol.first;
      fc = startLineCol.second;
      auto endLineCol = srcMgr.getLineAndColumn(srcEnd);
      ll = endLineCol.first;
      lc = endLineCol.second;
    }
  } else {
    // "main" does not have source information for obvious reasons.
    llvm::outs() << "WARNING: Source information is null for function: " << demangledFunctionName << "\n";
  }
  currentFunction->setFunctionSourceInfo(fl, fc, ll, lc);

  // Handle function arguments.
  for (SILArgument *arg: F->getArguments()) {
    if (arg->getDecl() && arg->getDecl()->hasName()) {
      // Currently the arguments do not have a specific position.
      currentFunction->addArgument(addressToString(static_cast<ValueBase*>(arg)), arg->getType().getAsString(),
        fl, fc, fl, fc);
    }
  }

  // Set function result type.
  if (F->getLoweredFunctionType()->getNumResults() == 1) {
    currentFunction->returnType = F->getLoweredFunctionType()->getSingleResult().getSILStorageType().getAsString();
  } else if (F->getLoweredFunctionType()->getNumResults() == 0) {
    currentFunction->returnType = "void";
  } else {
    currentFunction->returnType = "MultiResultType"; // TODO: Replace with array of types or something?
  }

  if (SWAN_PRINT) {
    llvm::outs() << "SILFunction: " << "ADDR: " << F << " , NAME: " << demangledFunctionName << "\n";
    llvm::outs() << "<RAW SIL BEGIN> \n\n";
    F->print(llvm::outs(), true);
    llvm::outs() << "\n</RAW SIL END> \n\n";
  }

  // Finally, visit every basic block of the function.
  for (auto &BB: *F) {
    visitSILBasicBlock(&BB);
  }

  currentModule->addFunction(currentFunction.get());
}

void InstructionVisitor::visitSILBasicBlock(SILBasicBlock *BB) {
  if (SWAN_PRINT) {
    llvm::outs() << "Basic Block: " << BB << "\n";
    llvm::outs() << "Parent SILFunction: " << BB->getParent() << "\n";
  }

  // Clear information from previous basic block.
  InstructionCounter = 0;
  currentBasicBlock = std::make_unique<RootBasicBlockInfo>(Instance->CAst);

  // Visit every instruction of the basic block.
  for (auto &I: *BB) {
    currentInstruction = std::make_unique<RootInstructionInfo>(Instance->CAst);
    visit(&I);
    currentInstruction->instructionName = getSILInstructionName(I.getKind());
    currentInstruction->setInstructionSourceInfo(instrInfo->startLine, instrInfo->startCol,
      instrInfo->endLine, instrInfo->endCol);
    currentBasicBlock->addInstruction(currentInstruction.get());
  }

  currentFunction->addBlock(currentBasicBlock.get());
}

//===----------- INSTRUCTION SOURCE INFORMATION PROCESSING ---------------===//

void InstructionVisitor::beforeVisit(SILInstruction *I) {
  // Set instruction source information.
  instrInfo = std::make_unique<SILInstructionInfo>();
  SourceManager &srcMgr = I->getModule().getSourceManager();
  SILLocation const &debugLoc = I->getDebugLocation().getLocation();
  SILLocation::DebugLoc const &debugInfo = debugLoc.decodeDebugLoc(srcMgr);
  // Set filename.
  instrInfo->Filename = debugInfo.Filename;
  // Set position.
  if (!I->getLoc().isNull()) {
    SourceRange const &srcRange = I->getLoc().getSourceRange();
    SourceLoc const &srcStart = srcRange.Start;
    SourceLoc const &srcEnd = srcRange.End;

    if (srcStart.isInvalid() && srcEnd.isInvalid()) {
      // This can happen if the instruction doesn't have corresponding Swift
      // code, most likely because it is a low level (usually memory related)
      // instruction. e.g. begin_access
      if (SWAN_PRINT) {
        llvm::outs() << "\t NOTE: Source information is invalid\n";
      }
    } else {
      if (srcStart.isValid()) {
        instrInfo->srcType = SILSourceType::STARTONLY;
        auto startLineCol = srcMgr.getLineAndColumn(srcStart);
        instrInfo->startLine = startLineCol.first;
        instrInfo->startCol = startLineCol.second;
      }
      if (srcEnd.isValid()) {
        auto endLineCol = srcMgr.getLineAndColumn(srcEnd);
        instrInfo->endLine = endLineCol.first;
        instrInfo->endCol = endLineCol.second;
        instrInfo->srcType = SILSourceType::FULL;
      }
    }
  }
  // Set memory behaviour.
  instrInfo->memBehavior = I->getMemoryBehavior();
  instrInfo->relBehavior = I->getReleasingBehavior();

  // Set other properties.
  instrInfo->num = InstructionCounter++;
  instrInfo->modInfo = moduleInfo.get();
  instrInfo->funcInfo = functionInfo.get();
  instrInfo->instrKind = I->getKind();

  // Set instruction operands.
  std::vector<void *> vals;
  for (const auto &op: I->getAllOperands()) {
    vals.push_back(op.get().getOpaqueValue());
  }
  instrInfo->ops = llvm::ArrayRef<void *>(vals);

  if (SWAN_PRINT) {
    if (SWAN_PRINT_SOURCE) {
      printSILInstructionInfo();
    }
    llvm::outs() << "<< " << getSILInstructionName(I->getKind()) << " >>\n";
  }
}

void InstructionVisitor::printSILInstructionInfo() {
  // llvm::outs() << "\t\t [INSTR] #" << instrInfo->num;
  // llvm::outs() << ", [OPNUM] " << instrInfo->id << "\n";
  if (SWAN_PRINT_FILE_AND_MEMORY) {
    llvm::outs() << "\t\t --> File: " << instrInfo->Filename << "\n";
    if (instrInfo->srcType == SILSourceType::INVALID) {
      llvm::outs() << "\t\t **** No source information. \n";
    } else { // Has at least start information.
      llvm::outs() << "\t\t\t ++++ Start - Line " << instrInfo->startLine << ":"
                   << instrInfo->startCol << "\n";
    }
    // Has end information.
    if (instrInfo->srcType == SILSourceType::FULL) {
      llvm::outs() << "\t\t\t ---- End - Line " << instrInfo->endLine;
      llvm::outs() << ":" << instrInfo->endCol << "\n";
    }
    // Memory Behavior.
    switch (instrInfo->memBehavior) {
      case SILInstruction::MemoryBehavior::None: {
        break;
      }
      case SILInstruction::MemoryBehavior::MayRead: {
        llvm::outs() << "\t\t +++ [MEM-R]: May read from memory. \n";
        break;
      }
      case SILInstruction::MemoryBehavior::MayWrite: {
        llvm::outs() << "\t\t +++ [MEM-W]: May write to memory. \n";
        break;
      }
      case SILInstruction::MemoryBehavior::MayReadWrite: {
        llvm::outs() << "\t\t +++ [MEM-RW]: May read or write memory. \n";
        break;
      }
      case SILInstruction::MemoryBehavior::MayHaveSideEffects: {
        llvm::outs() << "\t\t +++ [MEM-F]: May have side effects. \n";
      }
    }
    // Releasing Behavior.
    switch (instrInfo->relBehavior) {
      case SILInstruction::ReleasingBehavior::DoesNotRelease: {
        llvm::outs() << "\t\t [REL]: Does not release memory. \n";
        break;
      }
      case SILInstruction::ReleasingBehavior::MayRelease: {
        llvm::outs() << "\t\t [REL]: May release memory. \n";
        break;
      }
    }
  }
}

//===------------------------- UTLITY FUNCTIONS ----------------------------===//

jobject InstructionVisitor::getOperatorCAstType(const Identifier &Name) {
  if (Name.is("==")) {
    return CAstWrapper::OP_EQ;
  } else if (Name.is("!=")) {
    return CAstWrapper::OP_NE;
  } else if (Name.is("+")) {
    return CAstWrapper::OP_ADD;
  } else if (Name.is("/")) {
    return CAstWrapper::OP_DIV;
  } else if (Name.is("<<")) {
    return CAstWrapper::OP_LSH;
  } else if (Name.is("*")) {
    return CAstWrapper::OP_MUL;
  } else if (Name.is(">>")) {
    return CAstWrapper::OP_RSH;
  } else if (Name.is("-")) {
    return CAstWrapper::OP_SUB;
  } else if (Name.is(">=")) {
    return CAstWrapper::OP_GE;
  } else if (Name.is(">")) {
    return CAstWrapper::OP_GT;
  } else if (Name.is("<=")) {
    return CAstWrapper::OP_LE;
  } else if (Name.is("<")) {
    return CAstWrapper::OP_LT;
  } else if (Name.is("!")) {
    return CAstWrapper::OP_NOT;
  } else if (Name.is("~")) {
    return CAstWrapper::OP_BITNOT;
  } else if (Name.is("&")) {
    return CAstWrapper::OP_BIT_AND;
  } else if (Name.is("&&")) {
    // TODO: Why is this not handled?
    // OLD: return CAstWrapper::OP_REL_AND;
    return nullptr; // OLD: && and || are handled separately because they involve short circuits
  } else if (Name.is("|")) {
    return CAstWrapper::OP_BIT_OR;
  } else if (Name.is("||")) {
    // TODO: Why is this not handled?
    // OLD: return CAstWrapper::OP_REL_OR;
    return nullptr; // OLD: && and || are handled separatedly because they involve short circuits
  } else if (Name.is("^")) {
    return CAstWrapper::OP_BIT_XOR;
  } else if (Name.is("~=")) { // Pattern matching operator.
    return CAstWrapper::OP_EQ;
  } else {
    llvm::outs() << "WARNING: Unhandled operator: " << Name << " detected! \n";
    return nullptr;
  }
}

//===-------------------SPECIFIC INSTRUCTION VISITORS ----------------------===//

/*******************************************************************************/
/*                         ALLOCATION AND DEALLOCATION                         */
/*******************************************************************************/

void InstructionVisitor::visitAllocStackInst(AllocStackInst *ASI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(ASI));
  std::string ResultType = ASI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitAllocRefInst(AllocRefInst *ARI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(ARI));
  std::string ResultType = ARI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitAllocRefDynamicInst(AllocRefDynamicInst *ARDI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitAllocBoxInst(AllocBoxInst *ABI){
  std::string ResultName = addressToString(static_cast<ValueBase*>(ABI));
  std::string ResultType = ABI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitAllocValueBufferInst(AllocValueBufferInst *AVBI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitAllocGlobalInst(AllocGlobalInst *AGI) {
  SILGlobalVariable *Var = AGI->getReferencedGlobal();
  std::string VarName = Demangle::demangleSymbolAsString(Var->getName());
  std::string VarType = Var->getLoweredType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [ALLOC NAME]:" << VarName << "\n";
    llvm::outs() << "\t [ALLOC TYPE]:" << VarType << "\n";
  }
  ADD_PROP(MAKE_CONST(VarName.c_str()));
  ADD_PROP(MAKE_CONST(VarType.c_str()));
}

void InstructionVisitor::visitDeallocStackInst(DeallocStackInst *DSI) {
  std::string OperandName = addressToString(DSI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitDeallocBoxInst(DeallocBoxInst *DBI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitProjectBoxInst(ProjectBoxInst *PBI) {
  std::string OperandName = addressToString(PBI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(PBI));
  std::string ResultType = PBI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitDeallocRefInst(DeallocRefInst *DRI) {
  std::string OperandName = addressToString(DRI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitDeallocPartialRefInst(DeallocPartialRefInst *DPRI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitDeallocValueBufferInst(DeallocValueBufferInst *DVBI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitProjectValueBufferInst(ProjectValueBufferInst *PVBI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                        DEBUG INFROMATION                                    */
/*******************************************************************************/

void InstructionVisitor::visitDebugValueInst(DebugValueInst *DBI) {
  SILValue const &operand = DBI->getOperand();
  std::string OperandName = addressToString(operand.getOpaqueValue());
  std::string OperandType = operand->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [OPER TYPE]: " << OperandType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(OperandType.c_str()));
}

void InstructionVisitor::visitDebugValueAddrInst(DebugValueAddrInst *DVAI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                        Accessing Memory                                     */
/*******************************************************************************/

void InstructionVisitor::visitLoadInst(LoadInst *LI) {
  std::string OperandName = addressToString(LI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(LI));
  std::string ResultType = LI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitStoreInst(StoreInst *SI) {
  std::string SourceName = addressToString(SI->getSrc().getOpaqueValue());
  std::string DestName = addressToString(SI->getDest().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [SRC ADDR]: " << SourceName << "\n";
    llvm::outs() << "\t [DEST ADDR]: " << DestName << "\n";
  }
  ADD_PROP(MAKE_CONST(SourceName.c_str()));
  ADD_PROP(MAKE_CONST(DestName.c_str()));
}

void InstructionVisitor::visitStoreBorrowInst(StoreBorrowInst *SBI)
{
  // It seems the result of this instruction is never used, but it does
  // have one, unlike store. There is nothing in SIL.rst on this inst.
  std::string SourceName = addressToString(SBI->getSrc().getOpaqueValue());
  std::string DestName = addressToString(SBI->getDest().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [SRC ADDR]: " << SourceName << "\n";
    llvm::outs() << "\t [DEST ADDR]: " << DestName << "\n";
  }
  ADD_PROP(MAKE_CONST(SourceName.c_str()));
  ADD_PROP(MAKE_CONST(DestName.c_str()));
}

void InstructionVisitor::visitLoadBorrowInst(LoadBorrowInst *LBI) {
  std::string OperandName = addressToString(LBI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(LBI));
  std::string ResultType = LBI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitBeginBorrowInst(BeginBorrowInst *BBI) {
  std::string OperandName = addressToString(BBI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(BBI));
  std::string ResultType = BBI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitEndBorrowInst(EndBorrowInst *EBI) {
  std::string OperandName = addressToString(EBI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitAssignInst(AssignInst *AI) {
  std::string SourceName = addressToString(AI->getSrc().getOpaqueValue());
  std::string DestName = addressToString(AI->getDest().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [SRC ADDR]: " << SourceName << "\n";
    llvm::outs() << "\t [DEST ADDR]: " << DestName << "\n";
  }
  ADD_PROP(MAKE_CONST(SourceName.c_str()));
  ADD_PROP(MAKE_CONST(DestName.c_str()));
}

void InstructionVisitor::visitAssignByWrapperInst(AssignByWrapperInst *ABWI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitMarkUninitializedInst(MarkUninitializedInst *MUI) {
  std::string OperandName = addressToString(MUI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(MUI));
  std::string ResultType = MUI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitMarkFunctionEscapeInst(MarkFunctionEscapeInst *MFEI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitCopyAddrInst(CopyAddrInst *CAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitDestroyAddrInst(DestroyAddrInst *DAI) {
  std::string OperandName = addressToString(DAI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitIndexAddrInst(IndexAddrInst *IAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitTailAddrInst(TailAddrInst *TAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitIndexRawPointerInst(IndexRawPointerInst *IRPI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitBindMemoryInst(BindMemoryInst *BMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitBeginAccessInst(BeginAccessInst *BAI) {
  std::string OperandName = addressToString(BAI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(BAI));
  std::string ResultType = BAI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitEndAccessInst(EndAccessInst *EAI) {
  std::string OperandName = addressToString(EAI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitBeginUnpairedAccessInst(BeginUnpairedAccessInst *BUI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitEndUnpairedAccessInst(EndUnpairedAccessInst *EUAI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                        Reference Counting                                   */
/*******************************************************************************/

void InstructionVisitor::visitStrongRetainInst(StrongRetainInst *SRTI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitStrongReleaseInst(StrongReleaseInst *SRLI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitSetDeallocatingInst(SetDeallocatingInst *SDI)  {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitStrongRetainUnownedInst(StrongRetainUnownedInst *SRUI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUnownedRetainInst(UnownedRetainInst *URTI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUnownedReleaseInst(UnownedReleaseInst *URLI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitLoadWeakInst(LoadWeakInst *LWI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitStoreWeakInst(StoreWeakInst *SWI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitLoadUnownedInst(LoadUnownedInst *LUI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitStoreUnownedInst(StoreUnownedInst *SUI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitFixLifetimeInst(FixLifetimeInst *FLI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitEndLifetimeInst(EndLifetimeInst *ELI) {
  std::string OperandName = addressToString(ELI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitMarkDependenceInst(MarkDependenceInst *MDI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitIsUniqueInst(IsUniqueInst *IUI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitIsEscapingClosureInst(IsEscapingClosureInst *IECI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitCopyBlockInst(CopyBlockInst *CBI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitCopyBlockWithoutEscapingInst(CopyBlockWithoutEscapingInst *CBWEI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                         Literals                                            */
/*******************************************************************************/

void InstructionVisitor::visitFunctionRefInst(FunctionRefInst *FRI) {
  SILFunction *referencedFunction = FRI->getReferencedFunctionOrNull();
  std::string FuncName = Demangle::demangleSymbolAsString(referencedFunction->getName());
  std::string ResultName = addressToString(static_cast<ValueBase*>(FRI));
  std::string ResultType = FRI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [FUNC NAME]:" << FuncName << "\n";
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(FuncName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitDynamicFunctionRefInst(DynamicFunctionRefInst *DFRI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitPreviousDynamicFunctionRefInst(PreviousDynamicFunctionRefInst *PDFRI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitGlobalAddrInst(GlobalAddrInst *GAI) {
  SILGlobalVariable *Var = GAI->getReferencedGlobal();
  std::string VarName = Demangle::demangleSymbolAsString(Var->getName());
  std::string ResultName = addressToString(static_cast<ValueBase*>(GAI));
  std::string ResultType = GAI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [GLOBAL NAME]:" << VarName << "\n";
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(VarName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitGlobalValueInst(GlobalValueInst *GVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitIntegerLiteralInst(IntegerLiteralInst *ILI) {
  APInt Value = ILI->getValue();
  jobject Node = nullptr;
  if (Value.isNegative()) {
    if (Value.getMinSignedBits() <= 32) {
      Node = MAKE_CONST(static_cast<int>(Value.getSExtValue()));
    } else if (Value.getMinSignedBits() <= 64) {
      Node = MAKE_CONST(static_cast<long>(Value.getSExtValue()));
    }
  } else {
    if (Value.getActiveBits() <= 32) {
      Node = MAKE_CONST(static_cast<int>(Value.getZExtValue()));
    } else if (Value.getActiveBits() <= 64) {
      Node = MAKE_CONST(static_cast<long>(Value.getZExtValue()));
    }
  }
  if (Node != nullptr) {
    if (SWAN_PRINT) {
       llvm::outs() << "\t [VALUE]:" << Value.getZExtValue() << "\n";
    }
  } else {
    llvm::outs() << "\t ERROR: Undefined integer_literal behaviour\n";
  }
  std::string ResultName = addressToString(static_cast<ValueBase*>(ILI));
  std::string ResultType = ILI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(Node);
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitFloatLiteralInst(FloatLiteralInst *FLI) {
  APFloat Value = FLI->getValue();
  jobject Node = nullptr;
  if (&Value.getSemantics() == &APFloat::IEEEsingle()) {
    Node = Instance->CAst->makeConstant(Value.convertToFloat());
    if (SWAN_PRINT) {
      llvm::outs() << "\t [VALUE]:" << static_cast<double>(Value.convertToFloat()) << "\n";
    }
  }
  else if (&Value.getSemantics() == &APFloat::IEEEdouble()) {
    Node = Instance->CAst->makeConstant(Value.convertToDouble());
    if (SWAN_PRINT) {
      llvm::outs() << "\t [VALUE]:" << Value.convertToDouble() << "\n";
    }
  }
  else if (Value.isFinite()) {
    SmallVector<char, 128> buf;
    Value.toString(buf);
    jobject BigDecimal = Instance->makeBigDecimal(buf.data(), static_cast<int>(buf.size()));
    Node = Instance->CAst->makeConstant(BigDecimal);
    if (SWAN_PRINT) {
      llvm::outs() << "\t [VALUE]:" << buf << "\n";
    }
  }
  else {
    bool APFLosesInfo;
    Value.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &APFLosesInfo);
    Node = Instance->CAst->makeConstant(Value.convertToDouble());
    if (SWAN_PRINT) {
      llvm::outs() << "\t [VALUE]:" << Value.convertToDouble() << "\n";
    }
  }
  std::string ResultName = addressToString(static_cast<ValueBase*>(FLI));
  std::string ResultType = FLI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(Node);
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitStringLiteralInst(StringLiteralInst *SLI) {
  std::string Value = SLI->getValue();
  std::string ResultName = addressToString(static_cast<ValueBase*>(SLI));
  std::string ResultType = SLI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [VALUE]: " << Value << "\n";
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(Value.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

/*******************************************************************************/
/*                         Dynamic Dispatch                                    */
/*******************************************************************************/

void InstructionVisitor::visitClassMethodInst(ClassMethodInst *CMI) {
  std::string OperandName = addressToString(CMI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(CMI));
  std::string ResultType = CMI->getType().getAsString();
  std::string FunctionName = Demangle::demangleSymbolAsString(CMI->getMember().mangle());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
    llvm::outs() << "\t [CLASS METHOD]: " << FunctionName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  ADD_PROP(MAKE_CONST(FunctionName.c_str()));
}

void InstructionVisitor::visitObjCMethodInst(ObjCMethodInst *AMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitSuperMethodInst(SuperMethodInst *SMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitObjCSuperMethodInst(ObjCSuperMethodInst *ASMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitWitnessMethodInst(WitnessMethodInst *WMI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(WMI));
  std::string ResultType = WMI->getType().getAsString();
  std::string FunctionName = Demangle::demangleSymbolAsString(WMI->getMember().mangle());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
    llvm::outs() << "\t [CLASS METHOD]: " << FunctionName << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  ADD_PROP(MAKE_CONST(FunctionName.c_str()));
}

/*******************************************************************************/
/*                         Function Application                                */
/*******************************************************************************/

void InstructionVisitor::visitApplyInst(ApplyInst *AI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(AI));
  std::string ResultType = AI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  auto *Callee = AI->getReferencedFunctionOrNull();
  std::list<jobject> arguments;
  for (auto arg : AI->getArguments()) {
    arguments.push_back(MAKE_CONST(addressToString(arg.getOpaqueValue()).c_str()));
  }
  ADD_PROP(MAKE_CONST(addressToString(AI->getOperand(0).getOpaqueValue()).c_str()));
  if (SWAN_PRINT) {
    llvm::outs() << "\t [FUNC REF ADDR]: " << AI->getOperand(0).getOpaqueValue() << "\n";
  }
  if (!Callee) {
    llvm::outs() << "\t WARNING: Apply site's Callee is empty!\n";
    arguments.push_front(MAKE_CONST("N/A"));
    ADD_PROP(Instance->CAst->makeNode(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&arguments)));
    return;
  }
  auto *FD = Callee->getLocation().getAsASTNode<FuncDecl>();
  if (FD && (FD->isUnaryOperator() || FD->isBinaryOperator())) {
    jobject OperatorNode = getOperatorCAstType(FD->getName());
    arguments.push_front(OperatorNode);
    if (OperatorNode) {
      if (SWAN_PRINT) {
        llvm::outs() << "\t [OPERATOR NAME]:" << Instance->CAst->getConstantValue(OperatorNode) << "\n";
      }
      if (FD->isUnaryOperator()) {
        ADD_PROP(Instance->CAst->makeNode(CAstWrapper::UNARY_EXPR, MAKE_ARRAY(&arguments)));
      } else if (FD->isBinaryOperator()) {
        ADD_PROP(Instance->CAst->makeNode(CAstWrapper::BINARY_EXPR, MAKE_ARRAY(&arguments)));
      }
      if (SWAN_PRINT) {
        for (auto arg : arguments) {
          llvm::outs() << "\t\t [ARG]: " << Instance->CAst->getConstantValue(arg) << "\n";
        }
      }
    } else {
      llvm::outs() << "ERROR: Could not make operator \n";
    }
  } else {
    std::string CalleeName = Demangle::demangleSymbolAsString(Callee->getName());
    if (SWAN_PRINT) {
      llvm::outs() << "\t [CALLEE NAME]:" << CalleeName << "\n";
    }
    arguments.push_front(MAKE_CONST(CalleeName.c_str()));
    ADD_PROP(Instance->CAst->makeNode(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&arguments)));
  }
}

void InstructionVisitor::visitBeginApplyInst(BeginApplyInst *BAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitAbortApplyInst(AbortApplyInst *AAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitEndApplyInst(EndApplyInst *EAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitPartialApplyInst(PartialApplyInst *PAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitBuiltinInst(BuiltinInst *BI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                          Metatypes                                          */
/*******************************************************************************/

void InstructionVisitor::visitMetatypeInst(MetatypeInst *MI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(MI));
  std::string ResultType = MI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitValueMetatypeInst(ValueMetatypeInst *VMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitExistentialMetatypeInst(ExistentialMetatypeInst *EMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitObjCProtocolInst(ObjCProtocolInst *OPI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                          Aggregate Types                                    */
/*******************************************************************************/

void InstructionVisitor::visitRetainValueInst(RetainValueInst *RVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitRetainValueAddrInst(RetainValueAddrInst *RVAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUnmanagedRetainValueInst(UnmanagedRetainValueInst *URVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitCopyValueInst(CopyValueInst *CVI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(CVI));
  std::string ResultType = CVI->getType().getAsString();
  std::string OperandName = addressToString(CVI->getOperand().getOpaqueValue());
    if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitReleaseValueInst(ReleaseValueInst *REVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitReleaseValueAddrInst(ReleaseValueAddrInst *REVAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUnmanagedReleaseValueInst(UnmanagedReleaseValueInst *UREVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitDestroyValueInst(DestroyValueInst *DVI) {
  std::string OperandName = addressToString(DVI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitAutoreleaseValueInst(AutoreleaseValueInst *AREVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitTupleInst(TupleInst *TI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(TI));
  std::string ResultType = TI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  std::list<jobject> Fields;
  for (Operand &op : TI->getElementOperands()) {
    SILValue opValue = op.get();
    if (SWAN_PRINT) {
      llvm::outs() << "\t\t\t [FIELD VALUE]: " << opValue.getOpaqueValue() << "\n";
      llvm::outs() << "\t\t\t [FIELD TYPE]: " << opValue->getType().getAsString() << "\n";
    }
    Fields.push_back(MAKE_NODE3(CAstWrapper::PRIMITIVE,
      MAKE_CONST(addressToString(opValue.getOpaqueValue()).c_str()),
      MAKE_CONST(opValue->getType().getAsString().c_str())));
  }
  ADD_PROP(MAKE_NODE2(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&Fields)));
}

void InstructionVisitor::visitTupleExtractInst(TupleExtractInst *TEI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitTupleElementAddrInst(TupleElementAddrInst *TEAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitDestructureTupleInst(DestructureTupleInst *DTI) {
  SILValue const &result1 = DTI->getResult(0);
  SILValue const &result2 = DTI->getResult(1);
  std::string Result1Name = addressToString(result1.getOpaqueValue());
  std::string Result1Type = result1->getType().getAsString();
  std::string Result2Name = addressToString(result2.getOpaqueValue());
  std::string Result2Type = result2->getType().getAsString();
  std::string OperandName = addressToString(DTI->getOperand().getOpaqueValue());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT1 NAME]:" << Result1Name << "\n";
    llvm::outs() << "\t [RESULT1 TYPE]:" << Result1Type << "\n";
    llvm::outs() << "\t [RESULT2 NAME]:" << Result2Name << "\n";
    llvm::outs() << "\t [RESULT2 TYPE]:" << Result2Type << "\n";
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(Result1Name.c_str()));
  ADD_PROP(MAKE_CONST(Result1Type.c_str()));
  ADD_PROP(MAKE_CONST(Result2Name.c_str()));
  ADD_PROP(MAKE_CONST(Result2Type.c_str()));
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitStructInst(StructInst *SI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(SI));
  std::string ResultType = SI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  std::list<jobject> Fields;
  ArrayRef<VarDecl*>::iterator property = SI->getStructDecl()->getStoredProperties().begin();
  for (Operand &op : SI->getElementOperands()) {
    assert(property != SI->getStructDecl()->getStoredProperties().end());
    VarDecl *field = *property;
    SILValue opValue = op.get();
    if (SWAN_PRINT) {
      llvm::outs() << "\t\t\t [FIELD]: " << field->getNameStr() << "\n";
      llvm::outs() << "\t\t\t [VALUE]: " << opValue.getOpaqueValue() << "\n";
    }
    Fields.push_back(MAKE_NODE3(CAstWrapper::PRIMITIVE,
      MAKE_CONST(field->getNameStr().str().c_str()),
      MAKE_CONST(addressToString(opValue.getOpaqueValue()).c_str())));
    ++property;
  }
  ADD_PROP(MAKE_NODE2(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&Fields)));
}

void InstructionVisitor::visitStructExtractInst(StructExtractInst *SEI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitStructElementAddrInst(StructElementAddrInst *SEAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitDestructureStructInst(DestructureStructInst *DSI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitObjectInst(ObjectInst *OI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitRefElementAddrInst(RefElementAddrInst *REAI) {
  std::string OperandName = addressToString(REAI->getOperand().getOpaqueValue());
  std::string FieldName = REAI->getField()->getNameStr().str();
  std::string ResultName = addressToString(static_cast<ValueBase*>(REAI));
  std::string ResultType = REAI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [FIELD NAME]: " << FieldName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(FieldName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitRefTailAddrInst(RefTailAddrInst *RTAI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                          Enums                                              */
/*******************************************************************************/

void InstructionVisitor::visitEnumInst(EnumInst *EI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(EI));
  std::string ResultType = EI->getType().getAsString();
  std::string OperandName = "NO OPERAND";
  if (EI->hasOperand()) {
    OperandName = addressToString(EI->getOperand().getOpaqueValue());
  }
  std::string EnumName = EI->getElement()->getParentEnum()->getName().str();
  std::string CaseName = EI->getElement()->getNameStr();
   if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [ENUM NAME]: " << EnumName << "\n";
    llvm::outs() << "\t [CASE NAME]: " << CaseName << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(EnumName.c_str()));
  ADD_PROP(MAKE_CONST(CaseName.c_str()));
}

void InstructionVisitor::visitUncheckedEnumDataInst(UncheckedEnumDataInst *UED) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitInjectEnumAddrInst(InjectEnumAddrInst *IUAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitInitEnumDataAddrInst(InitEnumDataAddrInst *UDAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUncheckedTakeEnumDataAddrInst(UncheckedTakeEnumDataAddrInst *UDAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitSelectEnumInst(SelectEnumInst *SEI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                          Protocol and Protocol Composition Types            */
/*******************************************************************************/

void InstructionVisitor::visitInitExistentialAddrInst(InitExistentialAddrInst *IEAI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(IEAI));
  std::string ResultType = IEAI->getType().getAsString();
  std::string OperandName = addressToString(IEAI->getOperand().getOpaqueValue());
    if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitDeinitExistentialAddrInst(DeinitExistentialAddrInst *DEAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitInitExistentialValueInst(InitExistentialValueInst *IEVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitDeinitExistentialValueInst(DeinitExistentialValueInst *DEVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitOpenExistentialAddrInst(OpenExistentialAddrInst *OEAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitOpenExistentialValueInst(OpenExistentialValueInst *OEVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitInitExistentialMetatypeInst(InitExistentialMetatypeInst *IEMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitOpenExistentialMetatypeInst(OpenExistentialMetatypeInst *OEMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitInitExistentialRefInst(InitExistentialRefInst *IERI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitOpenExistentialRefInst(OpenExistentialRefInst *OERI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitAllocExistentialBoxInst(AllocExistentialBoxInst *AEBI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitProjectExistentialBoxInst(ProjectExistentialBoxInst *PEBI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitOpenExistentialBoxInst(OpenExistentialBoxInst *OEBI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitOpenExistentialBoxValueInst(OpenExistentialBoxValueInst *OEBVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitDeallocExistentialBoxInst(DeallocExistentialBoxInst *DEBI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                          Blocks                                             */
/*******************************************************************************/

/*******************************************************************************/
/*                          Unchecked Conversions                              */
/*******************************************************************************/

void InstructionVisitor::visitUpcastInst(UpcastInst *UI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitAddressToPointerInst(AddressToPointerInst *ATPI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitPointerToAddressInst(PointerToAddressInst *PTAI) {
  std::string ResultName = addressToString(static_cast<ValueBase*>(PTAI));
  std::string ResultType = PTAI->getType().getAsString();
  std::string OperandName = addressToString(PTAI->getOperand().getOpaqueValue());
    if (SWAN_PRINT) {
    llvm::outs() << "\t [RESULT NAME]:" << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]:" << ResultType << "\n";
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitUncheckedRefCastInst(UncheckedRefCastInst *URCI) {
  std::string OperandName = addressToString(URCI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(URCI));
  std::string ResultType = URCI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitUncheckedAddrCastInst(UncheckedAddrCastInst *UACI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUncheckedTrivialBitCastInst(UncheckedTrivialBitCastInst *BI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUncheckedOwnershipConversionInst(UncheckedOwnershipConversionInst *UOCI) {
  std::string OperandName = addressToString(UOCI->getOperand().getOpaqueValue());
  std::string ResultName = addressToString(static_cast<ValueBase*>(UOCI));
  std::string ResultType = UOCI->getType().getAsString();
  if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]: " << OperandName << "\n";
    llvm::outs() << "\t [RESULT NAME]: " << ResultName << "\n";
    llvm::outs() << "\t [RESULT TYPE]: " << ResultType << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
  ADD_PROP(MAKE_CONST(ResultName.c_str()));
  ADD_PROP(MAKE_CONST(ResultType.c_str()));
}

void InstructionVisitor::visitRefToRawPointerInst(RefToRawPointerInst *CI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitRawPointerToRefInst(RawPointerToRefInst *CI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitUnmanagedToRefInst(UnmanagedToRefInst *CI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitConvertFunctionInst(ConvertFunctionInst *CFI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitThinFunctionToPointerInst(ThinFunctionToPointerInst *TFPI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitPointerToThinFunctionInst(PointerToThinFunctionInst *CI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitThinToThickFunctionInst(ThinToThickFunctionInst *TTFI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitThickToObjCMetatypeInst(ThickToObjCMetatypeInst *TTOMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitObjCToThickMetatypeInst(ObjCToThickMetatypeInst *OTTMI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitConvertEscapeToNoEscapeInst(ConvertEscapeToNoEscapeInst *CVT) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                          Checked Conversions                                */
/*******************************************************************************/

void InstructionVisitor::visitUnconditionalCheckedCastAddrInst(UnconditionalCheckedCastAddrInst *CI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                          Runtime Failures                                   */
/*******************************************************************************/

void InstructionVisitor::visitCondFailInst(CondFailInst *FI) {
  // TODO: UNIMPLEMENTED
  
}

/*******************************************************************************/
/*                           Terminators                                       */
/*******************************************************************************/

void InstructionVisitor::visitUnreachableInst(UnreachableInst *UI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitReturnInst(ReturnInst *RI) {
  std::string OperandName = addressToString(RI->getOperand().getOpaqueValue());
    if (SWAN_PRINT) {
    llvm::outs() << "\t [OPER NAME]:" << OperandName << "\n";
  }
  ADD_PROP(MAKE_CONST(OperandName.c_str()));
}

void InstructionVisitor::visitThrowInst(ThrowInst *TI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitYieldInst(YieldInst *YI) {
  SILBasicBlock *ResumeBB = YI->getResumeBB();
  SILBasicBlock *UnwindBB = YI->getUnwindBB();
  std::string ResumeLabel = label(ResumeBB);
  std::string UnwindLabel = label(UnwindBB);
  if (SWAN_PRINT) {
    llvm::outs() << "\t [RESUME BB]: " << ResumeLabel << "\n";
    llvm::outs() << "\t [UNWIND BB]: " << UnwindLabel << "\n";
  }
  ADD_PROP(MAKE_CONST(ResumeLabel.c_str()));
  ADD_PROP(MAKE_CONST(UnwindLabel.c_str()));
  list<jobject> yieldValues;
  for (const auto &value : YI->getYieldedValues()) {
    if (SWAN_PRINT) {
      llvm::outs() << "\t [YIELD VALUE]: " << value << "\n";
      yieldValues.push_back(MAKE_CONST(addressToString(value.getOpaqueValue()).c_str()));
    }
  }
  ADD_PROP(MAKE_NODE2(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&yieldValues)));
}

void InstructionVisitor::visitUnwindInst(__attribute__((unused)) UnwindInst *UI) { }

void InstructionVisitor::visitBranchInst(BranchInst *BI) {
  std::string DestBranch = label(BI->getDestBB());
  ADD_PROP(MAKE_CONST(DestBranch.c_str()));
  if (SWAN_PRINT) {
    llvm::outs() << "\t [DEST BB]: " << DestBranch << "\n";
  }
  std::list<jobject> Arguments;
  for (unsigned int opIndex = 0; opIndex < BI->getNumArgs(); ++opIndex) {
    std::string OperandName = addressToString(BI->getOperand(opIndex).getOpaqueValue());
    std::string DestArgName = addressToString(BI->getDestBB()->getArgument(opIndex));
    std::string DestArgType = BI->getDestBB()->getArgument(opIndex)->getType().getAsString();
    if (SWAN_PRINT) {
      llvm::outs() << "\t\t [OPER NAME]: " << OperandName << "\n";
      llvm::outs() << "\t\t [DEST ARG NAME]: " << DestArgName << "\n";
      llvm::outs() << "\t\t [DEST ARG TYPE]: " << DestArgType << "\n";
      llvm::outs() << "\t\t -------\n";
    }
    Arguments.push_back(MAKE_NODE4(CAstWrapper::PRIMITIVE,
      MAKE_CONST(OperandName.c_str()),
      MAKE_CONST(DestArgName.c_str()),
      MAKE_CONST(DestArgType.c_str())));
  }
  ADD_PROP(MAKE_NODE2(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&Arguments)));
}

void InstructionVisitor::visitCondBranchInst(CondBranchInst *CBI) {
  std::string CondOperandName = addressToString(CBI->getCondition().getOpaqueValue());
  std::string TrueDestName = label(CBI->getTrueBB());
  std::string FalseDestName = label(CBI->getFalseBB());
  if (SWAN_PRINT) {
    llvm::outs() << "\t [COND NAME]: " << CondOperandName << "\n";
    llvm::outs() << "\t [TRUE DEST BB]: " << TrueDestName << "\n";
    llvm::outs() << "\t [FALSE DEST BB]: " << FalseDestName << "\n";
  }
  ADD_PROP(MAKE_CONST(CondOperandName.c_str()));
  ADD_PROP(MAKE_CONST(TrueDestName.c_str()));
  ADD_PROP(MAKE_CONST(FalseDestName.c_str()));
  std::list<jobject> TrueArguments;
  llvm::outs() << "\t True Args \n";
  for (unsigned int opIndex = 0; opIndex < CBI->getTrueOperands().size(); ++opIndex) {
    std::string OperandName = addressToString(CBI->getTrueOperands()[opIndex].get().getOpaqueValue());
    std::string DestArgName = addressToString(CBI->getTrueArgs()[opIndex]);
    std::string DestArgType = CBI->getTrueArgs()[opIndex]->getType().getAsString();
    if (SWAN_PRINT) {
      llvm::outs() << "\t\t [OPER NAME]: " << OperandName << "\n";
      llvm::outs() << "\t\t [DEST ARG NAME]: " << DestArgName << "\n";
      llvm::outs() << "\t\t [DEST ARG TYPE]: " << DestArgType << "\n";
      llvm::outs() << "\t\t -------\n";
    }
    TrueArguments.push_back(MAKE_NODE4(CAstWrapper::PRIMITIVE,
      MAKE_CONST(OperandName.c_str()),
      MAKE_CONST(DestArgName.c_str()),
      MAKE_CONST(DestArgType.c_str())));
  }
  ADD_PROP(MAKE_NODE2(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&TrueArguments)));
  std::list<jobject> FalseArguments;
  llvm::outs() << "\t False Args \n";
  for (unsigned int opIndex = 0; opIndex < CBI->getFalseOperands().size(); ++opIndex) {
    std::string OperandName = addressToString(CBI->getFalseOperands()[opIndex].get().getOpaqueValue());
    std::string DestArgName = addressToString(CBI->getFalseArgs()[opIndex]);
    std::string DestArgType = CBI->getFalseArgs()[opIndex]->getType().getAsString();
    if (SWAN_PRINT) {
      llvm::outs() << "\t\t [OPER NAME]: " << OperandName << "\n";
      llvm::outs() << "\t\t [DEST ARG NAME]: " << DestArgName << "\n";
      llvm::outs() << "\t\t [DEST ARG TYPE]: " << DestArgType << "\n";
      llvm::outs() << "\t\t -------\n";
    }
    FalseArguments.push_back(MAKE_NODE4(CAstWrapper::PRIMITIVE,
      MAKE_CONST(OperandName.c_str()),
      MAKE_CONST(DestArgName.c_str()),
      MAKE_CONST(DestArgType.c_str())));
  }
  ADD_PROP(MAKE_NODE2(CAstWrapper::PRIMITIVE, MAKE_ARRAY(&FalseArguments)));
}

void InstructionVisitor::visitSwitchValueInst(SwitchValueInst *SVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitSelectValueInst(SelectValueInst *SVI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitSwitchEnumInst(SwitchEnumInst *SWI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitSwitchEnumAddrInst(SwitchEnumAddrInst *SEAI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitCheckedCastBranchInst(CheckedCastBranchInst *CI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitCheckedCastAddrBranchInst(CheckedCastAddrBranchInst *CI) {
  // TODO: UNIMPLEMENTED
  
}

void InstructionVisitor::visitTryApplyInst(TryApplyInst *TAI) {
  // TODO: UNIMPLEMENTED
  
}
