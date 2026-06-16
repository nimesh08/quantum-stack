// spinor/dialect/mlir/SpinorMLIR.cpp
//
// MLIR-backed bridge for the Spinor dialect. Built only when
// QSTACK_HAVE_LLVM is ON. The generated TableGen includes
// (SpinorDialect.h.inc, SpinorOps.h.inc, SpinorTypes.h.inc) live
// in the build directory.
//
// Only stubs in M1; full lowering of in-tree Module ↔ MLIR
// ModuleOp lands as M5/M6 need it. The point of this file in M1
// is to register the dialect with an MLIRContext and to satisfy
// the M1-T17 test (dialect registers, empty module verifies).

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Support/LogicalResult.h"

#include "spinor/dialect/Spinor.h"

#include "SpinorTypes.h.inc"
#include "SpinorOps.h.inc"
#include "SpinorDialect.h.inc"

namespace mlir::spinor {

void SpinorDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "SpinorOps.cpp.inc"
      >();
  addTypes<
#define GET_TYPEDEF_LIST
#include "SpinorTypes.cpp.inc"
      >();
}

}  // namespace mlir::spinor

#define GET_TYPEDEF_CLASSES
#include "SpinorTypes.cpp.inc"

#define GET_OP_CLASSES
#include "SpinorOps.cpp.inc"

namespace spinor::dialect {

// Minimal smoke check used by M1-T17.
bool registerSpinorDialect() {
  ::mlir::MLIRContext ctx;
  ctx.loadDialect<::mlir::spinor::SpinorDialect>();
  ::mlir::OpBuilder b(&ctx);
  auto module = ::mlir::ModuleOp::create(b.getUnknownLoc());
  return ::mlir::succeeded(::mlir::verify(module));
}

}  // namespace spinor::dialect
