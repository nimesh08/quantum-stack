"""Independent mpmath checks of huge angles, SU(2) phase and tiny interactions.

Ported from the prior independent reviewer fixtures. Expected operators use
Pauli/projector definitions at 350 decimal digits, never compiler helpers.
Each mp.mpf(float) is the exact represented binary64 input, not an ideal decimal.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time

import mpmath as mp

from evidence import REPO, binary, environment, snapshot

# Exercise the checked-out Python API. Distribution isolation has a separate CI
# job; this mathematical audit must use the same source revision as the binary.
sys.path.insert(0, str(REPO / 'spinor/submit/python'))
from qstack import __version__
from qstack.registry import cache_targets
from qstack.service import compile_file

mp.mp.dps = 350
I = mp.eye(2)
X = mp.matrix([[0, 1], [1, 0]])
Y = mp.matrix([[0, -mp.j], [mp.j, 0]])
Z = mp.matrix([[1, 0], [0, -1]])


def one(name, params):
    if name in ('x', 'y', 'z'):
        return {'x': X, 'y': Y, 'z': Z}[name]
    if name == 'sx':
        return (I + X) / 2 + mp.j * (I - X) / 2
    if name in ('rx', 'ry', 'rz'):
        angle = mp.mpf(params[0]); axis = {'rx': X, 'ry': Y, 'rz': Z}[name]
        return mp.cos(angle / 2) * I - mp.j * mp.sin(angle / 2) * axis
    if name == 'u1q':
        angle, phase = map(mp.mpf, params)
        return mp.cos(angle / 2) * I - mp.j * mp.sin(angle / 2) * (mp.cos(phase) * X + mp.sin(phase) * Y)
    if name == 'phased_xz':
        x, z, a = map(mp.mpf, params)
        xp = (I + X) / 2 + mp.exp(mp.j * x) * (I - X) / 2
        axis = mp.matrix([[1, 0], [0, mp.exp(mp.j * a)]])
        zp = mp.matrix([[1, 0], [0, mp.exp(mp.j * z)]])
        return zp * axis * xp * axis.H
    raise ValueError(f'Unsupported oracle gate: {name}')


def operator(name, params, wires, n):
    size = 1 << n
    if name == 'gphase':
        return mp.exp(mp.j * mp.mpf(params[0])) * mp.eye(size)
    if len(wires) == 1:
        local = one(name, params); out = mp.zeros(size); mask = 1 << (n - 1 - wires[0])
        for col in range(size):
            for bit in (0, 1):
                row = (col & ~mask) | (mask if bit else 0)
                out[row, col] = local[bit, bool(col & mask)]
        return out
    out = mp.zeros(size)
    if name == 'rxx':
        angle = mp.mpf(params[0]); out = mp.cos(angle / 2) * mp.eye(size)
        mask = sum(1 << (n - 1 - q) for q in wires)
        for col in range(size):
            out[col ^ mask, col] = -mp.j * mp.sin(angle / 2)
        return out
    if name == 'rzz':
        angle = mp.mpf(params[0])
        for col in range(size):
            parity = sum(bool(col & (1 << (n - 1 - q))) for q in wires) % 2
            out[col, col] = mp.exp(mp.j * (1 if parity else -1) * angle / 2)
        return out
    if name == 'cz':
        out = mp.eye(size)
        for col in range(size):
            if all(col & (1 << (n - 1 - q)) for q in wires):
                out[col, col] = -1
        return out
    if name == 'cx':
        for col in range(size):
            row = col ^ ((1 << (n - 1 - wires[1])) if col & (1 << (n - 1 - wires[0])) else 0)
            out[row, col] = 1
        return out
    raise ValueError(f'Unsupported oracle operation: {name}')


def compose(ops, n, phase=0):
    out = mp.eye(1 << n)
    for name, params, wires in ops:
        out = operator(name, params, wires, n) * out
    return mp.exp(mp.j * mp.mpf(phase)) * out


def cases():
    for size in (1e12, 1e16, 1e100, 1e300):
        for sign in (-1, 1):
            a = size * sign
            for sequence in ([('rx', [a]), ('rx', [1.])], [('rz', [a]), ('rz', [1.])],
                             [('u1q', [a, a])], [('phased_xz', [a, .3, -.7])], [('phased_xz', [.3, a, a])]):
                yield 'large', [(name, params, [0]) for name, params in sequence], 1, 2e-10
    for a in (-4 * math.pi, -2 * math.pi, -math.pi, 0., math.pi, 2 * math.pi, 4 * math.pi):
        yield 'large', [('rx', [a], [0])], 1, 2e-10
    for delta in (1e-8, 1e-10, 1e-12):
        for case in ([('rx', [delta], [0])], [('rxx', [delta], [0, 1])],
                     [('rxx', [math.pi - delta], [0, 1]), ('rzz', [math.pi - delta], [0, 1])],
                     [('rx', [1.], [0]), ('rx', [-1. + delta], [0])],
                     [('rz', [math.pi + delta], [0]), ('cz', [], [0, 1]), ('rz', [math.pi], [0])]):
            yield 'tiny', case, 2, 2e-13


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spinorc', type=Path, default=os.environ.get('QSTACK_SPINORC'))
    parser.add_argument('--runtime-dir', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        executable = binary('spinorc', args.spinorc)
    except FileNotFoundError as exc:
        parser.error(str(exc))
    if args.runtime_dir:
        os.environ['PATH'] = str(args.runtime_dir.resolve()) + os.pathsep + os.environ.get('PATH', '')
    executables = {'spinorc': executable}; start_snapshot = snapshot(executables)
    records = []; started = time.monotonic()
    old_state = os.environ.get('QSTACK_STATE_DIR')
    try:
        with tempfile.TemporaryDirectory(prefix='qstack-high-precision-') as temporary:
            directory = Path(temporary); os.environ['QSTACK_STATE_DIR'] = str(directory / 'state')
            targets = []
            for name, width, gates in [('large', 1, ['rz', 'sx', 'x', 'cz']),
                                       ('tiny', 2, ['rx', 'ry', 'rz', 'sx', 'x', 'rxx', 'rzz', 'cx', 'cz'])]:
                targets.append({'route': 'ibm', 'vendor': 'ibm', 'device': 'audit-' + name,
                    'qubits': width, 'native_gates': gates, 'all_to_all': True, 'coupling': [],
                    'formats': ['qasm3'], 'capability_verified': True, 'parameter_units': 'radians',
                    'supports': {'reset': False, 'feedforward': False, 'mid_circuit_measure': False}})
            cache_targets('ibm', targets)
            source = directory / 'input.spn'
            for family, case, n, tolerance in cases():
                expected = compose(case, n)
                source.write_text(f'target generic\nqubit q[{n}]\n' + ''.join(
                    name + ('(' + ','.join(map(repr, params)) + ')' if params else '') + ' ' +
                    ', '.join(f'q[{wire}]' for wire in wires) + '\n' for name, params, wires in case), encoding='utf-8')
                for level in range(4):
                    record = {'family': family, 'source': case, 'optimization': level, 'tolerance': tolerance}
                    try:
                        physical = compile_file(source, target='audit-' + family,
                            config={'provider': 'ibm', 'spinorc': str(executable)}, optimization_level=level).physical_ir
                        if physical['initial_logical_to_physical'] != list(range(n)) or physical['logical_to_physical'] != list(range(n)):
                            raise ValueError('This high-precision fixture requires identity layouts')
                        actual = compose([(op['op'], op.get('params', []), op.get('qubits', [])) for op in physical['instructions']], n, physical['global_phase'])
                        error = max(abs(actual[row, col] - expected[row, col]) for row in range(1 << n) for col in range(1 << n))
                        record.update(max_entry_error=float(error), passed=bool(error < mp.mpf(tolerance)))
                    except Exception as exc:
                        record.update(error=str(exc), passed=False)
                    records.append(record)
    finally:
        if old_state is None:
            os.environ.pop('QSTACK_STATE_DIR', None)
        else:
            os.environ['QSTACK_STATE_DIR'] = old_state
    end_snapshot = snapshot(executables)
    failures = [record for record in records if not record['passed']]
    summary = {**environment(('mpmath',)), 'qstack_version': __version__, 'precision_decimal_digits': 350,
               'cases': len(records), 'failures': len(failures),
               'max_entry_error': max((r.get('max_entry_error', 0) for r in records), default=0),
               'tolerances': {'large': 2e-10, 'tiny': 2e-13}, 'elapsed_s': time.monotonic() - started,
               'source_and_binaries_at_start': start_snapshot, 'source_and_binaries_at_end': end_snapshot,
               'snapshot_changed_during_run': start_snapshot != end_snapshot, 'certified': False}
    if args.output:
        args.output.write_text(json.dumps({'summary': summary, 'records': records, 'failures': failures}, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2)); print(json.dumps(failures[:10], indent=2))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
