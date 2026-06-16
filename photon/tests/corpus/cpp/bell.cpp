// bell.cpp — a marked C++ kernel for the Phase C M5 ingester.
//
// The C++ frontend recognises this exact subset:
//   - `[[photon::kernel]]` attribute on the function.
//   - `QReg q(N);` declaration.
//   - `q.<gate>(args);` calls.
//   - `return q.measure_int();`.

[[photon::kernel]]
int bell_kernel() {
  QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure_int();
}
