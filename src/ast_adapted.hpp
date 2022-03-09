//
//  ast_adapted.hpp
//
//  Copyright (c) 2022 The Miko Authors.
//  Apache License v2.0
//

#ifndef _4a13c82a_9536_11ec_b909_0242ac120002
#define _4a13c82a_9536_11ec_b909_0242ac120002

#include "pch.hpp"
#include "ast.hpp"

namespace ast = miko::ast;

// clang-format off

////////////////
// Expression //
////////////////
BOOST_FUSION_ADAPT_STRUCT(
  ast::unary_op_expr,
  (std::string, op)
  (ast::expression, rhs)
)

BOOST_FUSION_ADAPT_STRUCT(
  ast::binary_op_expr,
  (ast::expression, lhs)
  (std::string, op)
  (ast::expression, rhs)
)

BOOST_FUSION_ADAPT_STRUCT(
  ast::variable_expr,
  (std::string, name)
)

BOOST_FUSION_ADAPT_STRUCT(
  ast::function_call_expr,
  (std::string, callee)
  (std::vector<ast::expression>, args)
)

///////////////
// Statement //
///////////////
BOOST_FUSION_ADAPT_STRUCT(
  ast::variable_def_statement,
  (std::string, name)
  (std::optional<ast::expression>, initializer)
)

BOOST_FUSION_ADAPT_STRUCT(
  ast::return_statement,
  (ast::expression, rhs)
)

BOOST_FUSION_ADAPT_STRUCT(
  ast::if_statement,
  (ast::expression, condition)
  (ast::compound_statement, then_statement)
  (std::optional<ast::compound_statement>, else_statement)
)

/////////////
// Program //
/////////////
BOOST_FUSION_ADAPT_STRUCT(
  ast::function_declare,
  (std::string, name)
  (std::vector<std::string>, args)
)

BOOST_FUSION_ADAPT_STRUCT(
  ast::function_define,
	(ast::function_declare, decl)
  (ast::compound_statement, body)
)

// clang-format on

#endif
