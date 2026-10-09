"""Independent scientific checks: provider compilers are never invoked.

Actual: public spinorc JSON output or TwoQubitDecomposer's public API.
Expected: pinned Qiskit gate matrices, NumPy Pauli definitions and QR matrices.
No compiler matrix/simulation/phase helpers are imported.
"""
from __future__ import annotations
import argparse, hashlib, json, math, os, pathlib, subprocess, sys, time, tempfile
import numpy as np
import qiskit
from qiskit.circuit import library as gl
from evidence import binary, snapshot, environment

REPO=pathlib.Path(__file__).resolve().parents[3]
ROOT=pathlib.Path('.')
EXE=None
KAK_EXE=None
ENV=dict(os.environ)
SEED=0xB20261009
TOLERANCE=2e-13
I=np.eye(2,dtype=complex)
X=np.array([[0,1],[1,0]],complex);Y=np.array([[0,-1j],[1j,0]],complex);Z=np.diag([1,-1]).astype(complex)
S=np.array([[1,0,0,0],[0,0,1,0],[0,1,0,0],[0,0,0,1]],complex)
BASES=['cx','cz','ecr','ms','rxx','rzz','iswap','sqrt_iswap','sqrt_iswap_inv','syc']
STANDARD={'h':gl.HGate,'x':gl.XGate,'y':gl.YGate,'z':gl.ZGate,'s':gl.SGate,'sdg':gl.SdgGate,'t':gl.TGate,'tdg':gl.TdgGate,'sx':gl.SXGate,'sxdg':gl.SXdgGate,'rx':gl.RXGate,'ry':gl.RYGate,'rz':gl.RZGate,'cx':gl.CXGate,'cz':gl.CZGate,'swap':gl.SwapGate,'ecr':gl.ECRGate,'rxx':gl.RXXGate,'rzz':gl.RZZGate,'iswap':gl.iSwapGate}

def gate(name,p=()):
    if name in STANDARD:
        g=np.asarray(STANDARD[name](*p).to_matrix(),complex)
        return S@g@S if g.shape==(4,4) else g # SDK qarg0 little-endian -> audit qarg0 high.
    if name in ('gpi','gpi2'):
        phi=p[0];axis=np.cos(phi)*X+np.sin(phi)*Y
        return axis if name=='gpi' else (I-1j*axis)/np.sqrt(2)
    if name=='u1q':
        theta,phi=p;return np.cos(theta/2)*I-1j*np.sin(theta/2)*(np.cos(phi)*X+np.sin(phi)*Y)
    if name=='phased_xz':
        x,z,a=p
        # Google XPow/ZPow projector definition. Do not form x+z or z+a,
        # which would itself lose small phases for huge finite doubles.
        axis=np.cos(a)*X+np.sin(a)*Y
        xp=(I+axis)/2+np.exp(1j*x)*(I-axis)/2
        return np.diag([1,np.exp(1j*z)])@xp
    if name=='ms':return (np.eye(4)-1j*np.kron(X,X))/np.sqrt(2)
    if name in ('sqrt_iswap','sqrt_iswap_inv'):
        sign=1 if name=='sqrt_iswap' else -1
        out=np.eye(4,dtype=complex);out[1:3,1:3]=np.array([[1,1j*sign],[1j*sign,1]])/np.sqrt(2);return out
    if name=='syc':
        out=np.zeros((4,4),complex);out[0,0]=1;out[1,2]=out[2,1]=-1j;out[3,3]=np.exp(-1j*np.pi/6);return out
    raise ValueError(name)

def embed(g,qubits,n):
    out=np.zeros((1<<n,1<<n),complex)
    for col in range(1<<n):
        local=0
        for q in qubits:local=(local<<1)|((col>>(n-1-q))&1)
        for r in range(1<<len(qubits)):
            row=col
            for k,q in enumerate(qubits):
                bit=(r>>(len(qubits)-1-k))&1
                row=(row&~(1<<(n-1-q)))|(bit<<(n-1-q))
            out[row,col]=g[r,local]
    return out

def compose(ops,n,phase=0):
    out=np.eye(1<<n,dtype=complex)
    for op in ops:
        if op['op']=='barrier':continue
        if op['op']=='gphase':out*=np.exp(1j*op['params'][0]);continue
        out=embed(gate(op['op'],op.get('params',[])),op['qubits'],n)@out
    return np.exp(1j*phase)*out

def logical_matrix(physical,n):
    initial=physical.get('initial_logical_to_physical') or list(range(n))
    final=physical.get('logical_to_physical') or list(range(n))
    used=sorted(set(initial+final+[q for op in physical['instructions'] for q in op['qubits']]))
    if len(used)>8:raise ValueError('audit exhaustive-space bound exceeded')
    compact={q:i for i,q in enumerate(used)}
    ops=[{**op,'qubits':[compact[q] for q in op['qubits']]} for op in physical['instructions']]
    matrix=compose(ops,len(used),physical.get('global_phase',0))
    def encoded(layout):
        return [sum(((i>>(n-1-q))&1)<<(len(used)-1-compact[layout[q]]) for q in range(n)) for i in range(1<<n)]
    columns=matrix[:,encoded(initial)]
    projected=columns[encoded(final),:]
    leakage=float(np.linalg.norm(columns)**2-np.linalg.norm(projected)**2)
    return projected,max(0,leakage)

def measure(expected,actual):
    overlap=np.vdot(actual,expected);phase=np.angle(overlap)
    return {'max_entry_error':float(np.max(np.abs(actual-expected))),
            'spectral_error':float(np.linalg.norm(actual-expected,ord=2)),
            'phase_aligned_error':float(np.max(np.abs(np.exp(1j*phase)*actual-expected))),
            'untracked_phase':float(phase)}

def resources(instructions):
    depth={};count=0;two=0
    for op in instructions:
        if op['op'] in ('barrier','gphase'):continue
        count+=1;two+=len(op['qubits'])==2
        layer=1+max((depth.get(q,0) for q in op['qubits']),default=0)
        for q in op['qubits']:depth[q]=layer
    return {'native_count':count,'native_twoq':two,'depth':max(depth.values(),default=0)}

def native_basis(basis):
    if basis in ('sqrt_iswap','sqrt_iswap_inv','syc'):return ['phased_xz',basis]
    if basis in ('iswap','rxx'):return ['rx','rz',basis]
    if basis=='ms':return ['gpi','gpi2',basis]
    if basis=='rzz':return ['u1q','rz',basis]
    if basis in ('cz','ecr'):return ['rz','sx',basis]
    return ['rz','ry',basis]

def setup_registry():
    path=ROOT/'registry';(path/'chips').mkdir(parents=True,exist_ok=True)
    for basis in BASES:
        gates=', '.join(native_basis(basis))
        (path/'chips'/f'audit_{basis}.yaml').write_text(f'''id: audit_{basis}
provider: audit
qubits: 3
native_gates: [{gates}]
coupling_map:
  topology: all_to_all
  size: 3
supports:
  mid_circuit_measure: true
  feedforward: full
  reset: true
decomposition:
  one_qubit:
    rotation_gate: {'gpi' if basis=='ms' else 'rz'}
    pi_2_gate: {'gpi2' if basis=='ms' else ('rx' if basis in ('iswap','rxx') else 'sx')}
  two_qubit:
    entangler: {basis}
    entangler_count_max: 3
''',encoding='utf8')
    return path

def source(ops,n=2):
    lines=['target generic',f'qubit q[{n}]']
    for op in ops:
        p=op.get('params',[]);angles='('+','.join(format(x,'.17g') for x in p)+')' if p else ''
        lines.append(op['op']+angles+' '+', '.join(f'q[{q}]' for q in op['qubits']))
    return '\n'.join(lines)+'\n'

def compile_case(name,ops,n,basis,level,registry):
    src=ROOT/'input.spn';src.write_text(source(ops,n),encoding='utf8')
    result=subprocess.run([str(EXE),'emit','-t','audit_'+basis,'-f','json','-O',str(level),str(src)],cwd=REPO,env={**ENV,'SPINOR_REGISTRY_ROOT':str(registry)},capture_output=True,text=True)
    record={'case':name,'basis':basis,'level':level,'num_qubits':n}
    if result.returncode:
        return {**record,'error':result.stderr.strip()}
    physical=json.loads(result.stdout);actual,leakage=logical_matrix(physical,n)
    return {**record,**measure(compose(ops,n),actual),'leakage':leakage,**resources(physical['instructions'])}

def unitary_cases(random_count=16):
    rng=np.random.default_rng(SEED)
    cases=[]
    one=['h','x','y','z','s','sdg','t','tdg','sx','sxdg','rx','ry','rz','gpi','gpi2','u1q','phased_xz']
    for name in one:
        p={'rx':[.37],'ry':[-1.24],'rz':[3.14],'gpi':[.83],'gpi2':[-.49],'u1q':[2.1,-.81],'phased_xz':[.73,-.21,1.23]}.get(name,[])
        cases.append((name,[{'op':name,'params':p,'qubits':[0]}],1))
    for name in ['cx','cz','swap','ecr','ms','rxx','rzz','iswap']:
        for wires in [[0,1],[1,0]]:
            cases.append((name+str(wires),[{'op':name,'params':[.371] if name in ('rxx','rzz') else [],'qubits':wires}],2))
    for i in range(random_count):
        n=2 if i<8 else 3;ops=[]
        for j in range(18):
            if rng.random()<.45:
                name=str(rng.choice(['cx','cz','ecr','rxx','rzz','swap']));wires=rng.choice(n,2,replace=False).tolist()
            else:name=str(rng.choice(['h','x','y','z','s','sdg','t','tdg','sx','sxdg','rx','ry','rz']));wires=[int(rng.integers(n))]
            ops.append({'op':name,'qubits':wires,'params':[float(rng.uniform(-4*np.pi,4*np.pi))] if name in ('rx','ry','rz','rxx','rzz') else []})
        cases.append((f'random_{i}',ops,n))
    # Cancellation must not cross a non-commuting operation on the same wires.
    for middle in ['h','x','y','z','s','sdg','t','tdg','sx','sxdg','rx','ry','rz','cx','cz','rxx','rzz']:
        ops=[{'op':'cx','qubits':[0,1],'params':[]},{'op':middle,'qubits':[0,1] if middle in ['cx','cz','rxx','rzz'] else [0],'params':[.31] if middle in ['rx','ry','rz','rxx','rzz'] else []},{'op':'cx','qubits':[0,1],'params':[]}]
        cases.append(('sandwich_'+middle,ops,2))
    for angle in [0.,1e-14,1e-11,np.pi/2-1e-11,np.pi/2,np.pi-1e-11,np.pi,2*np.pi,-2*np.pi,100*np.pi]:
        cases.append(('euler_boundary_'+str(angle),[{'op':'rz','qubits':[0],'params':[.37]},{'op':'ry','qubits':[0],'params':[float(angle)]},{'op':'rz','qubits':[0],'params':[-2.11]}],1))
    return cases

def haar(rng):
    a=rng.normal(size=(4,4))+1j*rng.normal(size=(4,4));q,r=np.linalg.qr(a);d=np.diag(r);return q@np.diag(d/np.abs(d))

def kak_cases(haar_count=256):
    rng=np.random.default_rng(SEED+1);cases=[(f'haar_{i}',haar(rng)) for i in range(haar_count)]
    for name in ['cx','cz','swap','ecr','ms','iswap','sqrt_iswap','sqrt_iswap_inv','syc']:
        for phase in [0,np.pi/2,np.pi-1e-10,-np.pi+1e-10,.271]:cases.append((f'{name}_phase_{phase}',np.exp(1j*phase)*gate(name)))
    for angle in [0,1e-14,1e-12,1e-11,1e-10,1e-7,np.pi/2-1e-11,np.pi/2,np.pi/2+1e-11,np.pi-1e-10]:
        for z in [0,angle,-angle]:
            matrix=gate('rxx',[angle])@gate('rzz',[z]);cases.append((f'degenerate_{angle}_{z}',matrix))
    for i in range(12):
        a=gate('rz',[float(rng.normal())])@gate('ry',[float(rng.normal())]);b=gate('rx',[float(rng.normal())])@gate('rz',[float(rng.normal())]);cases.append((f'local_{i}',np.exp(1j*float(rng.normal()))*np.kron(a,b)))
    return cases

def main():
    global ROOT, EXE, KAK_EXE, ENV, SEED
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=['cli','kak','large','all'],default='cli')
    parser.add_argument('--spinorc',type=pathlib.Path,default=os.environ.get('QSTACK_SPINORC'))
    parser.add_argument('--kak-probe',type=pathlib.Path,default=os.environ.get('QSTACK_KAK_AUDIT_PROBE'))
    parser.add_argument('--runtime-dir',type=pathlib.Path,help='Optional native DLL directory on Windows')
    parser.add_argument('--output',type=pathlib.Path,help='Optional complete JSON evidence file')
    parser.add_argument('--bases',nargs='+',choices=BASES,default=BASES)
    parser.add_argument('--suite',choices=['full','pr'],default='full',help='Bound public CLI fixtures for pull requests')
    parser.add_argument('--seed',type=lambda value:int(value,0),default=SEED)
    parser.add_argument('--haar-count',type=int,default=256)
    parser.add_argument('--random-count',type=int,default=16)
    args=parser.parse_args()
    if not 1<=args.haar_count<=16384 or not 1<=args.random_count<=1024:parser.error('Require 1..16384 Haar matrices and 1..1024 random circuits')
    if args.seed<0:parser.error('Seed must be nonnegative')
    SEED=args.seed
    try:
        EXE=binary('spinorc',args.spinorc)
        KAK_EXE=binary('spinor_kak_audit_probe',args.kak_probe) if args.mode in ('kak','all') else None
    except FileNotFoundError as exc:parser.error(str(exc))
    if args.runtime_dir:ENV['PATH']=str(args.runtime_dir.resolve())+os.pathsep+ENV.get('PATH','')
    work=tempfile.TemporaryDirectory(prefix='qstack-independent-audit-')
    ROOT=pathlib.Path(work.name)
    executables={'spinorc':EXE,**({'kak_probe':KAK_EXE} if KAK_EXE else {})}
    start_snapshot=snapshot(executables)
    start=time.time();records=[]
    if args.mode in ['cli','all']:
        reg=setup_registry()
        cases=unitary_cases(args.random_count)
        if args.suite=='pr':
            selected={'h','rx','u1q','phased_xz','cx[0, 1]','cx[1, 0]','rxx[0, 1]',
                      'iswap[1, 0]','random_0','random_1','sandwich_rz','euler_boundary_1e-11'}
            cases=[case for case in cases if case[0] in selected]
        for basis in args.bases:
            for level in range(4):
                for name,ops,n in cases:records.append(compile_case(name,ops,n,basis,level,reg))
            print('cli',basis,len(records),flush=True)
    if args.mode in ['large','all']:
        reg=setup_registry()
        for name,basis in [('rx','rxx'),('rxx','rxx'),('gpi','ms'),('u1q','rzz')]:
            for angle in [1e5,1e8,1e12,1e16,1e20]:
                params=[angle,.1] if name=='u1q' else [angle]
                wires=[0,1] if name=='rxx' else [0]
                records.append(compile_case(f'large_{name}_{angle}',[{'op':name,'params':params,'qubits':wires}],len(wires),basis,0,reg))
    if args.mode in ['kak','all']:
        cases=kak_cases(args.haar_count);payload='\n'.join(' '.join(f'{x.real:.17g} {x.imag:.17g}' for x in matrix.ravel()) for _,matrix in cases)+'\n'
        for basis in args.bases:
            result=subprocess.run([str(KAK_EXE),basis],input=payload,capture_output=True,text=True,env=ENV,check=True)
            lines=[line for line in result.stdout.splitlines() if line.strip()]
            if len(lines)!=len(cases):raise RuntimeError((basis,len(lines),len(cases),result.stderr))
            diagnostics=[json.loads(line) for line in result.stderr.splitlines() if line.startswith('{')]
            if diagnostics and len(diagnostics)!=len(cases):raise RuntimeError('incomplete synthesis trial metadata')
            for index,((name,expected),line) in enumerate(zip(cases,lines)):
                record={'case':'kak_'+name,'basis':basis}
                if diagnostics:record['synthesis']=diagnostics[index]
                if line.startswith('ERROR'):records.append({**record,'error':line});continue
                physical=json.loads(line);actual=compose(physical['instructions'],2,physical['global_phase'])
                records.append({**record,**measure(expected,actual),**resources(physical['instructions'])})
            print('kak',basis,len(records),flush=True)
    failures=[r for r in records if 'error' in r or r['max_entry_error']>TOLERANCE or r.get('leakage',0)>TOLERANCE]
    end_snapshot=snapshot(executables)
    summary={**end_snapshot['source'],**environment(('numpy','qiskit')),
             'source_and_binaries_at_start':start_snapshot,'source_and_binaries_at_end':end_snapshot,
             'snapshot_changed_during_run':start_snapshot!=end_snapshot,
             'spinorc_sha256':end_snapshot['binaries']['spinorc'],
             'kak_probe_sha256':end_snapshot['binaries'].get('kak_probe'),
             'numpy':np.__version__,'qiskit':qiskit.__version__,'suite':args.suite,
             'haar_count':args.haar_count,'random_count':args.random_count,
             'seed':SEED,'tolerance':TOLERANCE,'cases':len(records),'failures':len(failures),'max_entry_error':max((r.get('max_entry_error',0) for r in records),default=0),'elapsed_s':time.time()-start}
    if args.output:args.output.write_text(json.dumps({'summary':summary,'failures':failures,'records':records},indent=2),encoding='utf8')
    print(json.dumps(summary,indent=2));print(json.dumps(failures[:30],indent=2))
    work.cleanup()
    return bool(failures)

if __name__=='__main__':raise SystemExit(main())
