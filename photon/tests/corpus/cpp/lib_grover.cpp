// Library-routine driven kernel.
[[photon::kernel]]
int grover_demo() {
  QReg q(3);
  q.grover(2);
  return q.measure_int();
}
