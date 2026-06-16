[[photon::kernel]]
int ghz3() {
  QReg q(3);
  q.h(0);
  q.cx(0, 1);
  q.cx(1, 2);
  return q.measure_int();
}
