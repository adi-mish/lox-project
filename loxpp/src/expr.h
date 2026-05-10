#pragma once

#include <memory>
#include <string>
#include <vector>

#include "token.h"
#include "value.h"

namespace loxpp {

class AssignExpr;
class BinaryExpr;
class CallExpr;
class GetExpr;
class GroupingExpr;
class LiteralExpr;
class LogicalExpr;
class SetExpr;
class SuperExpr;
class ThisExpr;
class UnaryExpr;
class VariableExpr;

class ExprValueVisitor {
 public:
  virtual ~ExprValueVisitor() = default;
  virtual Value visitAssignExpr(AssignExpr& expr) = 0;
  virtual Value visitBinaryExpr(BinaryExpr& expr) = 0;
  virtual Value visitCallExpr(CallExpr& expr) = 0;
  virtual Value visitGetExpr(GetExpr& expr) = 0;
  virtual Value visitGroupingExpr(GroupingExpr& expr) = 0;
  virtual Value visitLiteralExpr(LiteralExpr& expr) = 0;
  virtual Value visitLogicalExpr(LogicalExpr& expr) = 0;
  virtual Value visitSetExpr(SetExpr& expr) = 0;
  virtual Value visitSuperExpr(SuperExpr& expr) = 0;
  virtual Value visitThisExpr(ThisExpr& expr) = 0;
  virtual Value visitUnaryExpr(UnaryExpr& expr) = 0;
  virtual Value visitVariableExpr(VariableExpr& expr) = 0;
};

class ExprVoidVisitor {
 public:
  virtual ~ExprVoidVisitor() = default;
  virtual void visitAssignExpr(AssignExpr& expr) = 0;
  virtual void visitBinaryExpr(BinaryExpr& expr) = 0;
  virtual void visitCallExpr(CallExpr& expr) = 0;
  virtual void visitGetExpr(GetExpr& expr) = 0;
  virtual void visitGroupingExpr(GroupingExpr& expr) = 0;
  virtual void visitLiteralExpr(LiteralExpr& expr) = 0;
  virtual void visitLogicalExpr(LogicalExpr& expr) = 0;
  virtual void visitSetExpr(SetExpr& expr) = 0;
  virtual void visitSuperExpr(SuperExpr& expr) = 0;
  virtual void visitThisExpr(ThisExpr& expr) = 0;
  virtual void visitUnaryExpr(UnaryExpr& expr) = 0;
  virtual void visitVariableExpr(VariableExpr& expr) = 0;
};

class ExprStringVisitor {
 public:
  virtual ~ExprStringVisitor() = default;
  virtual std::string visitAssignExpr(AssignExpr& expr) = 0;
  virtual std::string visitBinaryExpr(BinaryExpr& expr) = 0;
  virtual std::string visitCallExpr(CallExpr& expr) = 0;
  virtual std::string visitGetExpr(GetExpr& expr) = 0;
  virtual std::string visitGroupingExpr(GroupingExpr& expr) = 0;
  virtual std::string visitLiteralExpr(LiteralExpr& expr) = 0;
  virtual std::string visitLogicalExpr(LogicalExpr& expr) = 0;
  virtual std::string visitSetExpr(SetExpr& expr) = 0;
  virtual std::string visitSuperExpr(SuperExpr& expr) = 0;
  virtual std::string visitThisExpr(ThisExpr& expr) = 0;
  virtual std::string visitUnaryExpr(UnaryExpr& expr) = 0;
  virtual std::string visitVariableExpr(VariableExpr& expr) = 0;
};

class Expr {
 public:
  virtual ~Expr() = default;
  virtual Value accept(ExprValueVisitor& visitor) = 0;
  virtual void accept(ExprVoidVisitor& visitor) = 0;
  virtual std::string accept(ExprStringVisitor& visitor) = 0;
};

using ExprPtr = std::shared_ptr<Expr>;

class AssignExpr final : public Expr {
 public:
  AssignExpr(Token name, ExprPtr value)
      : name(std::move(name)), value(std::move(value)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  Token name;
  ExprPtr value;
};

class BinaryExpr final : public Expr {
 public:
  BinaryExpr(ExprPtr left, Token op, ExprPtr right)
      : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  ExprPtr left;
  Token op;
  ExprPtr right;
};

class CallExpr final : public Expr {
 public:
  CallExpr(ExprPtr callee, Token paren, std::vector<ExprPtr> arguments)
      : callee(std::move(callee)),
        paren(std::move(paren)),
        arguments(std::move(arguments)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  ExprPtr callee;
  Token paren;
  std::vector<ExprPtr> arguments;
};

class GetExpr final : public Expr {
 public:
  GetExpr(ExprPtr object, Token name)
      : object(std::move(object)), name(std::move(name)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  ExprPtr object;
  Token name;
};

class GroupingExpr final : public Expr {
 public:
  explicit GroupingExpr(ExprPtr expression)
      : expression(std::move(expression)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  ExprPtr expression;
};

class LiteralExpr final : public Expr {
 public:
  explicit LiteralExpr(Value value) : value(std::move(value)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  Value value;
};

class LogicalExpr final : public Expr {
 public:
  LogicalExpr(ExprPtr left, Token op, ExprPtr right)
      : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  ExprPtr left;
  Token op;
  ExprPtr right;
};

class SetExpr final : public Expr {
 public:
  SetExpr(ExprPtr object, Token name, ExprPtr value)
      : object(std::move(object)),
        name(std::move(name)),
        value(std::move(value)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  ExprPtr object;
  Token name;
  ExprPtr value;
};

class SuperExpr final : public Expr {
 public:
  SuperExpr(Token keyword, Token method)
      : keyword(std::move(keyword)), method(std::move(method)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  Token keyword;
  Token method;
};

class ThisExpr final : public Expr {
 public:
  explicit ThisExpr(Token keyword) : keyword(std::move(keyword)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  Token keyword;
};

class UnaryExpr final : public Expr {
 public:
  UnaryExpr(Token op, ExprPtr right)
      : op(std::move(op)), right(std::move(right)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  Token op;
  ExprPtr right;
};

class VariableExpr final : public Expr {
 public:
  explicit VariableExpr(Token name) : name(std::move(name)) {}

  Value accept(ExprValueVisitor& visitor) override;
  void accept(ExprVoidVisitor& visitor) override;
  std::string accept(ExprStringVisitor& visitor) override;

  Token name;
};

}  // namespace loxpp
