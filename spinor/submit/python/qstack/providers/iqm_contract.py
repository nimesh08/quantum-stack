"""IQM Star MOVE validation without invoking IQM's circuit transpiler.

MOVE is only defined on {|00>, |01>, |10>}; its unknown phase cancels in
a matching pair. We require closed sandwiches and reserve resonator slots.
"""
from qstack.models import QStackError


def validate_moves(ir, snapshot):
    operations = ir.get("instructions", [])
    if not snapshot.get("resonator_qubits") and not any(i.get("op") == "move" for i in operations):
        return
    size = snapshot.get("qubits", ir.get("num_qubits", 0))
    computational = snapshot.get("computational_qubits", [])
    resonators = snapshot.get("resonator_qubits", [])
    if (not computational or not resonators or
        any(type(q) is not int or not 0 <= q < size for q in computational + resonators) or
        len(set(computational + resonators)) != len(computational + resonators) or
        set(computational + resonators) != set(range(size))):
        raise QStackError("IQM MOVE requires distinct computational and resonator component slots from discovery", "TARGET_INCOMPATIBLE")
    for field in ("computational_qubits", "resonator_qubits"):
        if field in ir:
            components = ir[field]
            # Component lists identify a partition of physical slots; their
            # order does not define the logical layout or component names.
            if (not isinstance(components, list) or
                any(type(q) is not int for q in components) or
                len(set(components)) != len(components) or
                set(components) != set(snapshot[field])):
                raise QStackError("Physical IQM component metadata differs from the target snapshot", "TARGET_INCOMPATIBLE")
    for key in ("logical_to_physical", "initial_logical_to_physical"):
        if any(q not in computational for q in ir.get(key, [])):
            raise QStackError("Logical qubits cannot be placed on reserved IQM resonator slots", "TARGET_INCOMPATIBLE")
    move_loci = snapshot.get("gate_loci", {}).get("move", [])
    occupied, moved = {}, set()
    for inst in operations:
        op, qubits = inst.get("op"), inst.get("qubits", [])
        if occupied and op in {"barrier", "if", "else", "endif"}:
            raise QStackError("Close IQM MOVE sandwiches before a barrier or branch boundary", "TARGET_INCOMPATIBLE")
        if op == "move":
            if (len(qubits) != 2 or qubits[0] not in computational or qubits[1] not in resonators or
                qubits not in move_loci or inst.get("params", []) or inst.get("clbits", [])):
                raise QStackError("MOVE requires an available ordered (qubit, resonator) locus and no parameters", "TARGET_INCOMPATIBLE")
            qubit, resonator = qubits
            if resonator in occupied:
                if occupied[resonator] != qubit:
                    raise QStackError("Cannot MOVE into a resonator occupied by a different qubit", "TARGET_INCOMPATIBLE")
                del occupied[resonator]
                moved.remove(qubit)
            else:
                if qubit in moved:
                    raise QStackError("Cannot MOVE a qubit whose state is already in another resonator", "TARGET_INCOMPATIBLE")
                occupied[resonator] = qubit
                moved.add(qubit)
        elif op != "barrier":
            if moved.intersection(qubits):
                raise QStackError("An operation uses a qubit whose state is held in an IQM resonator", "TARGET_INCOMPATIBLE")
            active_resonators = set(qubits).intersection(resonators)
            if active_resonators and (op != "cz" or len(active_resonators) != 1 or not active_resonators.issubset(occupied)):
                raise QStackError("Reserved resonator slots support CZ only within a closed MOVE sandwich", "TARGET_INCOMPATIBLE")
    if occupied:
        raise QStackError("IQM circuit ends with an open MOVE sandwich; restore each qubit state", "TARGET_INCOMPATIBLE")
