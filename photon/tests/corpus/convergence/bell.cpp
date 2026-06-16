[[photon::kernel]]
int bell() {
  QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure_int();
}
