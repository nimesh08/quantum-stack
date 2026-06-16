OPENQASM 3.0;
qubit[1] q;
bit[1] c;
c[0] = measure q[0];
if (c[0] == 1) {
  x q[0];
}
