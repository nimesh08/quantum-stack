// photon/lang/cpp/lib/Module.cpp
//
// Photon AST helpers.

#include "photon/lang/Module.h"

namespace photon::lang {

const Function* Module::findFunction(std::string_view name) const {
  for (const auto& f : functions) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

ExprPtr mkInt(std::int64_t v, Location loc) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::IntLit;
  e->int_value = v;
  e->loc = std::move(loc);
  return e;
}

ExprPtr mkReal(double v, Location loc) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::RealLit;
  e->real_value = v;
  e->loc = std::move(loc);
  return e;
}

ExprPtr mkIdent(std::string n, Location loc) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Ident;
  e->text = std::move(n);
  e->loc = std::move(loc);
  return e;
}

ExprPtr mkBinOp(std::string op, ExprPtr l, ExprPtr r, Location loc) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::BinOp;
  e->text = std::move(op);
  e->children = {std::move(l), std::move(r)};
  e->loc = std::move(loc);
  return e;
}

ExprPtr mkCall(std::string name, std::vector<ExprPtr> args, Location loc) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Call;
  e->text = std::move(name);
  e->children = std::move(args);
  e->loc = std::move(loc);
  return e;
}

ExprPtr mkMember(std::string recv, std::string member, Location loc) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Member;
  e->text = std::move(member);
  e->children = {mkIdent(std::move(recv), loc)};
  e->loc = std::move(loc);
  return e;
}

}  // namespace photon::lang
