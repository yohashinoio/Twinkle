//
//  codegen.cpp
//
//  Copyright (c) 2022 The Miko Authors.
//  MIT License
//

#include "codegen.hpp"
#include "utility.hpp"
#include <boost/spirit/include/support_line_pos_iterator.hpp>

namespace miko::codegen
{

struct symbol_table {
  llvm::AllocaInst* operator[](const std::string& name) const noexcept
  try {
    return named_values.at(name);
  }
  catch (const std::out_of_range&) {
    return nullptr;
  }

  void insert(const std::string& name, llvm::AllocaInst* value)
  {
    named_values.insert({name, value});
  }

private:
  std::unordered_map<std::string, llvm::AllocaInst*> named_values;
};

// Create an alloca instruction in the entry block of
// the function.  This is used for mutable variables etc.
llvm::AllocaInst* create_entry_block_alloca(llvm::Function*    func,
                                            llvm::LLVMContext& context,
                                            const std::string& var_name)
{
  llvm::IRBuilder<> tmp(&func->getEntryBlock(), func->getEntryBlock().begin());

  return tmp.CreateAlloca(llvm::Type::getInt32Ty(context), nullptr, var_name);
}

// It seems that the member function has to be const.
struct expression_visitor : public boost::static_visitor<llvm::Value*> {
  expression_visitor(std::shared_ptr<llvm::Module> module,
                     llvm::IRBuilder<>&            builder,
                     symbol_table&                 named_values,
                     const std::filesystem::path&  source)
    : module{module}
    , builder{builder}
    , named_values{named_values}
    , source{source}
  {
  }

  llvm::Value* operator()(ast::nil) const
  {
    BOOST_ASSERT(0);
  }

  llvm::Value* operator()(const int value) const
  {
    return llvm::ConstantInt::get(builder.getInt32Ty(), value);
  }

  llvm::Value* operator()(const ast::unaryop& node) const
  {
    auto rhs = boost::apply_visitor(*this, node.rhs);

    if (node.op == "+")
      return rhs;
    if (node.op == "-") {
      // -x to 0-x
      return apply_sub_op(llvm::ConstantInt::get(builder.getInt32Ty(), 0), rhs);
    }

    BOOST_ASSERT_MSG(
      0,
      "Unsupported unary operators may have been converted to ASTs.");
  }

  llvm::Value* operator()(const ast::binop& node) const
  {
    auto lhs = boost::apply_visitor(*this, node.lhs);
    auto rhs = boost::apply_visitor(*this, node.rhs);

    // addition
    if (node.op == "+")
      return apply_add_op(lhs, rhs);
    if (node.op == "-")
      return apply_sub_op(lhs, rhs);

    // multiplication
    if (node.op == "*")
      return apply_mul_op(lhs, rhs);
    if (node.op == "/")
      return apply_div_op(lhs, rhs);

    // equality
    if (node.op == "==")
      return apply_equal(lhs, rhs);
    if (node.op == "!=")
      return apply_not_equal(lhs, rhs);

    // relational
    if (node.op == "<")
      return apply_signed_lt(lhs, rhs);
    if (node.op == ">")
      return apply_signed_gt(lhs, rhs);
    if (node.op == "<=")
      return apply_signed_lte(lhs, rhs);
    if (node.op == ">=")
      return apply_signed_gte(lhs, rhs);

    BOOST_ASSERT_MSG(
      0,
      "Unsupported binary operators may have been converted to ASTs.");
  }

  llvm::Value* operator()(const ast::variable& node) const
  {
    auto* ainst = named_values[node.name];
    if (!ainst) {
      throw std::runtime_error{format_error_message(
        source.string(),
        format("Unknown variable '%s' referenced", node.name))};
    }

    return builder.CreateLoad(ainst->getAllocatedType(),
                              ainst,
                              node.name.c_str());
  }

  llvm::Value* operator()(const ast::function_call& node) const
  {
    auto* callee_f = module->getFunction(node.callee);

    if (!callee_f) {
      throw std::runtime_error{format_error_message(
        source.string(),
        format("Unknown function '%s' referenced", node.callee))};
    }

    if (callee_f->arg_size() != node.args.size()) {
      throw std::runtime_error{
        format_error_message(source.string(),
                             format("Incorrect arguments passed"))};
    }

    std::vector<llvm::Value*> args_value;
    for (std::size_t i = 0, size = node.args.size(); i != size; ++i) {
      args_value.push_back(boost::apply_visitor(*this, node.args[i]));

      if (!args_value.back())
        return nullptr;
    }

    return builder.CreateCall(callee_f, args_value);
  }

private:
  std::shared_ptr<llvm::Module> module;
  llvm::IRBuilder<>&            builder;

  symbol_table& named_values;

  const std::filesystem::path& source;

  llvm::Value* apply_add_op(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateAdd(lhs, rhs);
  }
  llvm::Value* apply_sub_op(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateSub(lhs, rhs);
  }
  llvm::Value* apply_mul_op(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateMul(lhs, rhs);
  }
  llvm::Value* apply_div_op(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateSDiv(lhs, rhs);
  }

  llvm::Value* apply_equal(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateICmp(llvm::ICmpInst::ICMP_EQ, lhs, rhs);
  }
  llvm::Value* apply_not_equal(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateICmp(llvm::ICmpInst::ICMP_NE, lhs, rhs);
  }

  // Less than
  llvm::Value* apply_signed_lt(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateICmp(llvm::ICmpInst::ICMP_SLT, lhs, rhs);
  }
  // Greater than
  llvm::Value* apply_signed_gt(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateICmp(llvm::ICmpInst::ICMP_SGT, lhs, rhs);
  }
  // Less than or equal to
  llvm::Value* apply_signed_lte(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateICmp(llvm::ICmpInst::ICMP_SLE, lhs, rhs);
  }
  // Greater than or equal to
  llvm::Value* apply_signed_gte(llvm::Value* lhs, llvm::Value* rhs) const
  {
    return builder.CreateICmp(llvm::ICmpInst::ICMP_SGE, lhs, rhs);
  }
};

struct statement_visitor : public boost::static_visitor<void> {
  statement_visitor(std::shared_ptr<llvm::Module> module,
                    llvm::IRBuilder<>&            builder,
                    symbol_table&                 named_values,
                    const std::filesystem::path&  source)
    : module{module}
    , builder{builder}
    , named_values{named_values}
    , source{source}
  {
  }

  void operator()(ast::nil) const
  {
    BOOST_ASSERT(0);
  }

  void operator()(const ast::expression& node) const
  {
    if (!boost::apply_visitor(
          expression_visitor{module, builder, named_values, source},
          node)) {
      throw std::runtime_error{
        format_error_message(source.string(),
                             "Failed to generate expression code")};
    }
  }

  void operator()(const ast::return_statement& node) const
  {
    auto* retval = boost::apply_visitor(
      expression_visitor{module, builder, named_values, source},
      node.rhs);

    if (!retval) {
      throw std::runtime_error{
        format_error_message(source.string(),
                             "Failure to generate return value.")};
    }

    builder.CreateRet(retval);
  }

private:
  std::shared_ptr<llvm::Module> module;
  llvm::IRBuilder<>&            builder;

  symbol_table& named_values;

  const std::filesystem::path& source;
};

struct top_visitor : public boost::static_visitor<llvm::Function*> {
  top_visitor(llvm::LLVMContext&                 context,
              std::shared_ptr<llvm::Module>      module,
              llvm::IRBuilder<>&                 builder,
              llvm::legacy::FunctionPassManager& fpm,
              const std::filesystem::path&       source)
    : context{context}
    , module{module}
    , builder{builder}
    , fpm{fpm}
    , source{source}
  {
  }

  llvm::Function* operator()(ast::nil) const
  {
    BOOST_ASSERT(0);
  }

  // Function declaration
  llvm::Function* operator()(const ast::function_decl& node) const
  {
    std::vector<llvm::Type*> param_types(node.args.size(),
                                         builder.getInt32Ty());

    auto* func_type
      = llvm::FunctionType::get(builder.getInt32Ty(), param_types, false);

    auto* func = llvm::Function::Create(func_type,
                                        llvm::Function::ExternalLinkage,
                                        node.name,
                                        module.get());

    // Set names for all arguments.
    {
      std::size_t idx = 0;
      for (auto&& arg : func->args())
        arg.setName(node.args[idx++]);
    }

    return func;
  }

  // Function definition
  llvm::Function* operator()(const ast::function_def& node) const
  {
    auto* func = module->getFunction(node.decl.name);

    if (!func)
      func = this->operator()(node.decl);

    if (!func) {
      throw std::runtime_error{format_error_message(
        source.string(),
        format("Failed to create function %s", node.decl.name),
        true)};
    }

    auto* basic_block = llvm::BasicBlock::Create(context, "entry", func);
    builder.SetInsertPoint(basic_block);

    symbol_table named_values;
    for (auto& arg : func->args()) {
      // Create an alloca for this variable.
      auto* alloca
        = create_entry_block_alloca(func, context, arg.getName().str());

      // Store the initial value into the alloca.
      builder.CreateStore(&arg, alloca);

      // Add arguments to variable symbol table.
      named_values.insert(arg.getName().str(), alloca);
    }

    for (auto&& statement : node.body) {
      boost::apply_visitor(
        statement_visitor{module, builder, named_values, source},
        statement);
    }

    std::string              em;
    llvm::raw_string_ostream os{em};
    if (llvm::verifyFunction(*func, &os)) {
      func->eraseFromParent();

      throw std::runtime_error{format_error_message(source.string(), os.str())};
    }

    fpm.run(*func);

    return func;
  }

private:
  llvm::LLVMContext&            context;
  std::shared_ptr<llvm::Module> module;
  llvm::IRBuilder<>&            builder;

  llvm::legacy::FunctionPassManager& fpm;

  const std::filesystem::path& source;
};

code_generator::code_generator(const ast::program&          ast,
                               const position_cache&        positions,
                               const std::filesystem::path& source,
                               const bool                   optimize)
  : module{std::make_shared<llvm::Module>(source.filename().string(), context)}
  , builder{context}
  , fpm{module.get()}
  , source{source}
  , ast{ast}
  , positions{positions}
{
  if (optimize) {
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    fpm.add(llvm::createInstructionCombiningPass());
    // Reassociate expressions.
    fpm.add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    fpm.add(llvm::createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    fpm.add(llvm::createCFGSimplificationPass());
    // Promote allocas to registers.
    fpm.add(llvm::createPromoteMemoryToRegisterPass());
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    fpm.add(llvm::createInstructionCombiningPass());
    // Reassociate expressions.
    fpm.add(llvm::createReassociatePass());
  }

  fpm.doInitialization();

  codegen();
}

void code_generator::write_llvm_ir_to_file(
  const std::filesystem::path& out) const
{
  std::error_code      ostream_ec;
  llvm::raw_fd_ostream os{out.string(),
                          ostream_ec,
                          llvm::sys::fs::OpenFlags::OF_None};

  if (ostream_ec) {
    throw std::runtime_error{format_error_message(
      "mikoc",
      format("%s: %s", out.string(), ostream_ec.message()))};
  }

  module->print(os, nullptr);
}

void code_generator::write_object_code_to_file(
  const std::filesystem::path& out) const
{
  const auto target_triple = llvm::sys::getDefaultTargetTriple();

  // target triple error string.
  std::string target_triple_es;
  auto        target
    = llvm::TargetRegistry::lookupTarget(target_triple, target_triple_es);

  if (!target) {
    throw std::runtime_error{format_error_message(
      "mikoc",
      format("Failed to lookup target %s: %s", target_triple, target_triple_es),
      true)};
  }

  llvm::TargetOptions opt;
  auto*               the_target_machine
    = target->createTargetMachine(target_triple,
                                  "generic",
                                  "",
                                  opt,
                                  llvm::Optional<llvm::Reloc::Model>());

  module->setTargetTriple(target_triple);
  module->setDataLayout(the_target_machine->createDataLayout());

  std::error_code      ostream_ec;
  llvm::raw_fd_ostream os{out.string(),
                          ostream_ec,
                          llvm::sys::fs::OpenFlags::OF_None};
  if (ostream_ec) {
    throw std::runtime_error{format_error_message(
      "mikoc",
      format("%s: %s\n", out.string(), ostream_ec.message()))};
  }

  llvm::legacy::PassManager pm;
  if (the_target_machine->addPassesToEmitFile(pm,
                                              os,
                                              nullptr,
                                              llvm::CGFT_ObjectFile)) {
    throw std::runtime_error{
      format_error_message("mikoc",
                           "TargetMachine can't emit a file of this types",
                           true)};
  }

  pm.run(*module);
  os.flush();
}

void code_generator::codegen()
{
  for (auto&& node : ast) {
    boost::apply_visitor(top_visitor{context, module, builder, fpm, source},
                         node);
  }
}

} // namespace miko::codegen
