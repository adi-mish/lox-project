#include "CodeGenVisitor.h"

#include <llvm/IR/Instructions.h>

namespace eloxir {

bool CodeGenVisitor::emitPlannedClassCall(Call *e) {
  if (!codeGenPlan) {
    return false;
  }

  auto *calleeVar = dynamic_cast<Variable *>(e->callee.get());
  if (!calleeVar) {
    return false;
  }

  const std::string &className = calleeVar->name.getLexeme();
  const PlannedClass *planned = codeGenPlan->findStableClass(className);
  if (!planned) {
    return false;
  }

  if (planned->hasSuperclass ||
      planned->initializerArity != static_cast<int>(e->arguments.size())) {
    return false;
  }

  auto slotIt = stableClassSlots.find(className);
  if (slotIt == stableClassSlots.end()) {
    return false;
  }

  auto *slot = slotIt->second;
  auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(slot);
  auto *activeFn = builder.GetInsertBlock()->getParent();
  if (!alloca || alloca->getFunction() != activeFn) {
    return false;
  }

  llvm::Function *instantiateFn = mod.getFunction("elx_instantiate_known_class");
  if (!instantiateFn) {
    return false;
  }

  llvm::Function *initializer = nullptr;
  if (!planned->trivialInitializer) {
    auto initIt = stableInitializers.find(className);
    if (initIt == stableInitializers.end()) {
      return false;
    }
    initializer = initIt->second;
    if (!initializer ||
        initializer->arg_size() !=
            static_cast<unsigned>(e->arguments.size() + 1)) {
      return false;
    }
  }

  llvm::Value *classValue =
      builder.CreateLoad(llvmValueTy(), slot, className + "_class");

  std::vector<llvm::Value *> args;
  args.reserve(e->arguments.size());
  for (auto &arg : e->arguments) {
    arg->accept(this);
    args.push_back(value);
  }

  llvm::Value *instance =
      builder.CreateCall(instantiateFn, {classValue}, "direct_class");
  if (!initializer) {
    value = instance;
    return true;
  }

  std::vector<llvm::Value *> callArgs;
  callArgs.reserve(args.size() + 1);
  callArgs.push_back(instance);
  callArgs.insert(callArgs.end(), args.begin(), args.end());
  llvm::Value *initResult =
      builder.CreateCall(initializer, callArgs, "direct_init");
  checkRuntimeError(initResult);
  return true;
}

bool CodeGenVisitor::isDiscardablePlannedClassCall(const Expr *expr) const {
  if (!codeGenPlan) {
    return false;
  }

  const auto *call = dynamic_cast<const Call *>(expr);
  if (!call || !call->arguments.empty()) {
    return false;
  }

  const auto *calleeVar = dynamic_cast<const Variable *>(call->callee.get());
  if (!calleeVar) {
    return false;
  }

  const std::string &className = calleeVar->name.getLexeme();
  const PlannedClass *planned =
      codeGenPlan->findStableTrivialClass(className);
  if (!planned || planned->initializerArity != 0) {
    return false;
  }

  return stableClassSlots.find(className) != stableClassSlots.end();
}

bool CodeGenVisitor::isDiscardablePlannedReceiverCall(const Expr *expr) const {
  if (!codeGenPlan) {
    return false;
  }

  const auto *call = dynamic_cast<const Call *>(expr);
  if (!call || !call->arguments.empty()) {
    return false;
  }

  const auto *get = dynamic_cast<const Get *>(call->callee.get());
  if (!get) {
    return false;
  }

  const auto *receiver = dynamic_cast<const Variable *>(get->object.get());
  if (!receiver) {
    return false;
  }

  const std::string &receiverName = receiver->name.getLexeme();
  auto slotIt = stableReceiverSlots.find(receiverName);
  if (slotIt == stableReceiverSlots.end()) {
    return false;
  }

  auto stackIt = variableStacks.find(receiverName);
  if (stackIt == variableStacks.end() || stackIt->second.empty() ||
      stackIt->second.back() != slotIt->second) {
    return false;
  }

  const PlannedMethod *method =
      codeGenPlan->findStableReceiverMethod(receiverName, get->name.getLexeme());
  return method && method->arity == 0;
}

bool CodeGenVisitor::emitPlannedReceiverCall(Call *e) {
  if (!codeGenPlan || !e->arguments.empty()) {
    return false;
  }

  auto *get = dynamic_cast<Get *>(e->callee.get());
  if (!get) {
    return false;
  }

  auto *receiverVar = dynamic_cast<Variable *>(get->object.get());
  if (!receiverVar) {
    return false;
  }

  const std::string &receiverName = receiverVar->name.getLexeme();
  const std::string &methodName = get->name.getLexeme();
  const PlannedMethod *method =
      codeGenPlan->findStableReceiverMethod(receiverName, methodName);
  if (!method || method->arity != 0) {
    return false;
  }

  auto slotIt = stableReceiverSlots.find(receiverName);
  if (slotIt == stableReceiverSlots.end()) {
    return false;
  }

  auto stackIt = variableStacks.find(receiverName);
  if (stackIt == variableStacks.end() || stackIt->second.empty() ||
      stackIt->second.back() != slotIt->second) {
    return false;
  }

  llvm::Value *slot = slotIt->second;
  auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(slot);
  llvm::Function *fn = builder.GetInsertBlock()->getParent();
  if (!alloca || alloca->getFunction() != fn) {
    return false;
  }

  llvm::Function *shapeFn = mod.getFunction("elx_instance_shape_ptr");
  llvm::Function *fieldsFn = mod.getFunction("elx_instance_field_values_ptr");
  llvm::Function *presenceFn =
      mod.getFunction("elx_instance_field_presence_ptr");
  llvm::Function *callPropertyFn = mod.getFunction("elx_call_property");
  if (!shapeFn || !callPropertyFn) {
    return false;
  }
  if (method->kind == PlannedMethodKind::FieldGetter &&
      (!fieldsFn || !presenceFn)) {
    return false;
  }

  llvm::Value *receiver =
      builder.CreateLoad(llvmValueTy(), slot, receiverName + "_receiver");
  llvm::Value *shape = builder.CreateCall(shapeFn, {receiver}, "direct_shape");
  llvm::Value *shapeValid = builder.CreateIsNotNull(shape, "direct_shape_ok");

  auto directBB = llvm::BasicBlock::Create(ctx, "direct.method", fn);
  auto fallbackBB = llvm::BasicBlock::Create(ctx, "direct.method.fallback", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "direct.method.cont", fn);
  builder.CreateCondBr(shapeValid, directBB, fallbackBB);

  std::vector<std::pair<llvm::BasicBlock *, llvm::Value *>> results;

  builder.SetInsertPoint(directBB);
  if (method->kind == PlannedMethodKind::Empty) {
    llvm::Value *directValue = nilConst();
    builder.CreateBr(contBB);
    results.emplace_back(builder.GetInsertBlock(), directValue);
  } else if (method->kind == PlannedMethodKind::FieldGetter) {
    llvm::Value *fields = builder.CreateCall(fieldsFn, {receiver}, "fields");
    llvm::Value *presence =
        builder.CreateCall(presenceFn, {receiver}, "field_presence");
    llvm::Value *fieldsOk = builder.CreateIsNotNull(fields, "fields_ok");
    llvm::Value *presenceOk =
        builder.CreateIsNotNull(presence, "field_presence_ok");
    llvm::Value *storageOk =
        builder.CreateAnd(fieldsOk, presenceOk, "field_storage_ok");

    auto loadBB = llvm::BasicBlock::Create(ctx, "direct.method.load", fn);
    builder.CreateCondBr(storageOk, loadBB, fallbackBB);

    builder.SetInsertPoint(loadBB);
    auto slotIndex = llvm::ConstantInt::get(builder.getInt64Ty(),
                                            method->fieldSlot);
    auto presencePtr = builder.CreateInBoundsGEP(
        builder.getInt8Ty(), presence, slotIndex, "field_presence_ptr");
    auto initializedByte =
        builder.CreateLoad(builder.getInt8Ty(), presencePtr, "field_present");
    auto initialized =
        builder.CreateICmpNE(initializedByte, builder.getInt8(0),
                             "field_initialized");
    auto initializedBB =
        llvm::BasicBlock::Create(ctx, "direct.method.initialized", fn);
    builder.CreateCondBr(initialized, initializedBB, fallbackBB);

    builder.SetInsertPoint(initializedBB);
    auto fieldPtr = builder.CreateInBoundsGEP(llvmValueTy(), fields, slotIndex,
                                              "field_ptr");
    llvm::Value *directValue =
        builder.CreateLoad(llvmValueTy(), fieldPtr, "field_value");
    builder.CreateBr(contBB);
    results.emplace_back(builder.GetInsertBlock(), directValue);
  } else {
    builder.CreateBr(fallbackBB);
  }

  builder.SetInsertPoint(fallbackBB);
  llvm::Value *nameValue = stringConst(methodName, true);
  llvm::Value *argArray = llvm::ConstantPointerNull::get(
      llvm::PointerType::get(llvmValueTy(), 0));
  llvm::Value *argCount = builder.getInt32(0);
  llvm::Value *fallbackValue =
      builder.CreateCall(callPropertyFn, {receiver, nameValue, argArray,
                                          argCount},
                         "direct_property_fallback");
  checkRuntimeError(fallbackValue);
  llvm::Value *safeFallbackValue = value;
  builder.CreateBr(contBB);
  results.emplace_back(builder.GetInsertBlock(), safeFallbackValue);

  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), results.size(),
                               "direct_method_result");
  for (auto &entry : results) {
    phi->addIncoming(entry.second, entry.first);
  }
  value = phi;
  return true;
}

bool CodeGenVisitor::emitPlannedInitializerFieldSet(Set *e) {
  if (!codeGenPlan || plannedInitializerClass.empty()) {
    return false;
  }

  if (!dynamic_cast<This *>(e->object.get())) {
    return false;
  }

  const PlannedClass *planned =
      codeGenPlan->findStableClass(plannedInitializerClass);
  if (!planned || !planned->linearInitializer) {
    return false;
  }

  auto slotIt = planned->fieldSlots.find(e->name.getLexeme());
  if (slotIt == planned->fieldSlots.end()) {
    return false;
  }

  llvm::Function *hasErrorFn = mod.getFunction("elx_has_runtime_error");
  llvm::Function *setSlotFn = mod.getFunction("elx_set_instance_field_slot");
  if (!hasErrorFn || !setSlotFn) {
    return false;
  }

  e->object->accept(this);
  llvm::Value *receiver = value;
  auto errorFlag = builder.CreateCall(hasErrorFn, {}, "direct_set_object_error");
  auto hasError = builder.CreateICmpNE(errorFlag, builder.getInt32(0),
                                       "direct_set_object_failed");

  llvm::Function *fn = builder.GetInsertBlock()->getParent();
  auto skipValueBB = llvm::BasicBlock::Create(ctx, "direct.set.skip", fn);
  auto evalValueBB = llvm::BasicBlock::Create(ctx, "direct.set.eval", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "direct.set.cont", fn);
  builder.CreateCondBr(hasError, skipValueBB, evalValueBB);

  builder.SetInsertPoint(evalValueBB);
  e->value->accept(this);
  llvm::Value *assignedValue = value;
  llvm::Value *nameValue = stringConst(e->name.getLexeme(), true);
  llvm::Value *slot =
      builder.getInt32(static_cast<uint32_t>(slotIt->second));
  llvm::Value *setResult =
      builder.CreateCall(setSlotFn, {receiver, nameValue, slot, assignedValue},
                         "direct_field_set");
  checkRuntimeError(setResult);
  llvm::Value *successValue = value;
  builder.CreateBr(contBB);
  llvm::BasicBlock *successBB = builder.GetInsertBlock();

  builder.SetInsertPoint(skipValueBB);
  llvm::Value *skipValue = nilConst();
  builder.CreateBr(contBB);
  llvm::BasicBlock *skipEndBB = builder.GetInsertBlock();

  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 2, "direct_set_result");
  phi->addIncoming(successValue, successBB);
  phi->addIncoming(skipValue, skipEndBB);
  value = phi;
  return true;
}

} // namespace eloxir
