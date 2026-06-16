// spinor/passes/lib/Complex2x2.h
//
// Tiny 2×2 / 4×4 complex matrix helpers for the Phase A
// decomposition recipes. We avoid pulling in Eigen for Phase A
// because the matrix sizes are small and our recipes are
// closed-form. Decision D6.

#pragma once

#include <array>
#include <cmath>
#include <complex>

namespace spinor::passes::la {

using cdbl = std::complex<double>;

struct Mat2 {
  std::array<cdbl, 4> e{};  // row-major
  cdbl& operator()(int r, int c) { return e[2 * r + c]; }
  cdbl  operator()(int r, int c) const { return e[2 * r + c]; }
};

inline Mat2 identity2() {
  Mat2 m;
  m(0,0) = 1; m(1,1) = 1;
  return m;
}
inline Mat2 mul2(const Mat2& a, const Mat2& b) {
  Mat2 c;
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 2; ++j) {
      cdbl s = 0;
      for (int k = 0; k < 2; ++k) s += a(i, k) * b(k, j);
      c(i, j) = s;
    }
  return c;
}

// Standard 1q unitaries.
inline Mat2 H()   { Mat2 m; double s = 1.0/std::sqrt(2.0);
                    m(0,0)=s; m(0,1)=s; m(1,0)=s; m(1,1)=-s; return m; }
inline Mat2 X()   { Mat2 m; m(0,1)=1; m(1,0)=1; return m; }
inline Mat2 Y()   { Mat2 m; m(0,1) = cdbl(0, -1); m(1,0) = cdbl(0, 1); return m; }
inline Mat2 Z()   { Mat2 m; m(0,0)=1; m(1,1)=-1; return m; }
inline Mat2 S()   { Mat2 m; m(0,0)=1; m(1,1) = cdbl(0, 1); return m; }
inline Mat2 Sdg() { Mat2 m; m(0,0)=1; m(1,1) = cdbl(0, -1); return m; }
inline Mat2 T()   { Mat2 m; m(0,0)=1;
                    m(1,1) = std::polar(1.0, M_PI/4); return m; }
inline Mat2 Tdg() { Mat2 m; m(0,0)=1;
                    m(1,1) = std::polar(1.0, -M_PI/4); return m; }
inline Mat2 SX()  { Mat2 m; cdbl one(1,0), i(0,1);
                    m(0,0) = (one + i)/2.0; m(0,1) = (one - i)/2.0;
                    m(1,0) = (one - i)/2.0; m(1,1) = (one + i)/2.0;
                    return m; }
inline Mat2 SXdg(){ Mat2 m; cdbl one(1,0), i(0,1);
                    m(0,0) = (one - i)/2.0; m(0,1) = (one + i)/2.0;
                    m(1,0) = (one + i)/2.0; m(1,1) = (one - i)/2.0;
                    return m; }

inline Mat2 Rx(double t) {
  Mat2 m;
  cdbl c(std::cos(t/2), 0), s(0, -std::sin(t/2));
  m(0,0) = c; m(0,1) = s; m(1,0) = s; m(1,1) = c; return m;
}
inline Mat2 Ry(double t) {
  Mat2 m;
  double c = std::cos(t/2), s = std::sin(t/2);
  m(0,0) = c; m(0,1) = -s; m(1,0) = s; m(1,1) = c; return m;
}
inline Mat2 Rz(double t) {
  Mat2 m;
  m(0,0) = std::polar(1.0, -t/2);
  m(1,1) = std::polar(1.0,  t/2);
  return m;
}

// Equality up to a complex unit-modulus factor (global phase).
inline bool equalUpToPhase(const Mat2& a, const Mat2& b, double tol = 1e-9) {
  // Find a non-zero entry and divide.
  for (int i = 0; i < 4; ++i) {
    if (std::abs(b.e[i]) > 1e-7) {
      cdbl phase = a.e[i] / b.e[i];
      if (std::abs(std::abs(phase) - 1.0) > tol) return false;
      for (int j = 0; j < 4; ++j) {
        cdbl diff = a.e[j] - phase * b.e[j];
        if (std::abs(diff) > tol) return false;
      }
      return true;
    }
  }
  // b is zero matrix — accept only if a is too.
  for (int i = 0; i < 4; ++i)
    if (std::abs(a.e[i]) > tol) return false;
  return true;
}

// 4×4 helpers for two-qubit gates.
struct Mat4 {
  std::array<cdbl, 16> e{};
  cdbl& operator()(int r, int c) { return e[4 * r + c]; }
  cdbl  operator()(int r, int c) const { return e[4 * r + c]; }
};

inline Mat4 identity4() {
  Mat4 m;
  for (int i = 0; i < 4; ++i) m(i, i) = 1;
  return m;
}
inline Mat4 mul4(const Mat4& a, const Mat4& b) {
  Mat4 c;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      cdbl s = 0;
      for (int k = 0; k < 4; ++k) s += a(i, k) * b(k, j);
      c(i, j) = s;
    }
  return c;
}

// Tensor product (Kronecker) of 2×2 blocks. Convention: top
// qubit is the leftmost factor (operand 0 = control).
inline Mat4 kron(const Mat2& a, const Mat2& b) {
  Mat4 c;
  for (int ar = 0; ar < 2; ++ar)
    for (int ac = 0; ac < 2; ++ac)
      for (int br = 0; br < 2; ++br)
        for (int bc = 0; bc < 2; ++bc) {
          c(ar * 2 + br, ac * 2 + bc) = a(ar, ac) * b(br, bc);
        }
  return c;
}

// Common 4×4 gates.
inline Mat4 CX() {
  // |0><0| ⊗ I + |1><1| ⊗ X
  Mat4 c;
  // |00>->|00>; |01>->|01>; |10>->|11>; |11>->|10>
  c(0,0) = 1; c(1,1) = 1; c(2,3) = 1; c(3,2) = 1;
  return c;
}
inline Mat4 CZ() {
  Mat4 c;
  c(0,0) = 1; c(1,1) = 1; c(2,2) = 1; c(3,3) = -1;
  return c;
}
inline Mat4 SWAP() {
  Mat4 c;
  c(0,0) = 1; c(1,2) = 1; c(2,1) = 1; c(3,3) = 1;
  return c;
}

// ECR: standard form. ECR = (1/√2)(IX − XY).
inline Mat4 ECR() {
  Mat4 m;
  cdbl one(1, 0), i(0, 1);
  // ECR = (1/√2) [ [0,1,0,i], [1,0,-i,0], [0,i,0,1], [-i,0,1,0] ]
  // (one canonical form; may differ by phase between vendors.
  // We test up to global phase, so any consistent ECR form works.)
  cdbl s = cdbl(1.0/std::sqrt(2.0), 0);
  m(0,0)=0; m(0,1)=s*one; m(0,2)=0; m(0,3)=s*i;
  m(1,0)=s*one; m(1,1)=0; m(1,2)=s*(-i); m(1,3)=0;
  m(2,0)=0; m(2,1)=s*i; m(2,2)=0; m(2,3)=s*one;
  m(3,0)=s*(-i); m(3,1)=0; m(3,2)=s*one; m(3,3)=0;
  return m;
}

// MS(θ): exp(-i θ XX/2). Default θ=π/2 is the IonQ "MS" gate.
inline Mat4 MS(double t = M_PI/2) {
  // exp(-iθ XX/2) acts on basis as a block-rotation of the
  // (00, 11) subspace and the (01, 10) subspace.
  double c = std::cos(t/2), s = std::sin(t/2);
  Mat4 m;
  cdbl I(0, -1);
  m(0,0) = c;        m(0,3) = I * s;
  m(1,1) = c;        m(1,2) = I * s;
  m(2,1) = I * s;    m(2,2) = c;
  m(3,0) = I * s;    m(3,3) = c;
  return m;
}

// RZZ(θ): exp(-i θ ZZ/2).
inline Mat4 RZZ(double t) {
  Mat4 m;
  m(0,0) = std::polar(1.0, -t/2);
  m(1,1) = std::polar(1.0,  t/2);
  m(2,2) = std::polar(1.0,  t/2);
  m(3,3) = std::polar(1.0, -t/2);
  return m;
}

inline bool equalUpToPhase4(const Mat4& a, const Mat4& b, double tol = 1e-8) {
  for (int i = 0; i < 16; ++i) {
    if (std::abs(b.e[i]) > 1e-6) {
      cdbl phase = a.e[i] / b.e[i];
      if (std::abs(std::abs(phase) - 1.0) > tol) return false;
      for (int j = 0; j < 16; ++j) {
        cdbl diff = a.e[j] - phase * b.e[j];
        if (std::abs(diff) > tol) return false;
      }
      return true;
    }
  }
  for (int i = 0; i < 16; ++i)
    if (std::abs(a.e[i]) > tol) return false;
  return true;
}

}  // namespace spinor::passes::la
