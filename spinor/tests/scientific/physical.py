"""Portable independent placement/routing/MOVE and complete-instrument audit.

No compiler gate matrices, simulator, or qstack verification helpers are used.
The public compile API invokes only the owned compiler; provider calls are absent.
"""
from __future__ import annotations
import argparse, copy, json, os, sys, tempfile, time
from pathlib import Path
import numpy as np
from evidence import REPO, binary, snapshot, environment
from audit import gate as reference_gate
sys.path.insert(0,str(REPO/'spinor/submit/python'))
from qstack.registry import cache_targets, profiles
from qstack.service import compile_file
from qstack.models import QStackError

SEED=95321
TOLERANCE=2e-9
I=np.eye(2,dtype=complex); X=np.array([[0,1],[1,0]],complex);Y=np.array([[0,-1j],[1j,0]],complex);Z=np.diag([1,-1]).astype(complex)
H=(X+Z)/np.sqrt(2)
def matrix(op):
 name=op['op'];p=op.get('params',[])
 if name in ('x','y','z','h','id'):return {'x':X,'y':Y,'z':Z,'h':H,'id':I}[name]
 if name=='sx':return .5*((1+1j)*I+(1-1j)*X)
 if name=='sxdg':return matrix({'op':'sx'}).conj().T
 if name=='s':return np.diag([1,1j])
 if name=='sdg':return np.diag([1,-1j])
 if name in ('rx','ry','rz'):return np.cos(p[0]/2)*I-1j*np.sin(p[0]/2)*{'rx':X,'ry':Y,'rz':Z}[name]
 if name=='u1q':return np.cos(p[0]/2)*I-1j*np.sin(p[0]/2)*(np.cos(p[1])*X+np.sin(p[1])*Y)
 if name=='cz':return np.diag([1,1,1,-1]).astype(complex)
 if name=='cx':return np.array([[1,0,0,0],[0,1,0,0],[0,0,0,1],[0,0,1,0]],complex)
 if name=='swap':return np.array([[1,0,0,0],[0,0,1,0],[0,1,0,0],[0,0,0,1]],complex)
 if name in ('rzz','rxx'):return np.cos(p[0]/2)*np.eye(4)-1j*np.sin(p[0]/2)*np.kron(Z if name=='rzz' else X,Z if name=='rzz' else X)
 if name=='move':
  # Official IQM matrix on {|00>, |01>, |10>}; |11> is undefined,
  # set it to zero and independently detect any occupied amplitude.
  a=np.exp(1j*(.73+sum((i+1)*q for i,q in enumerate(op['qubits']))*.11))
  return np.array([[1,0,0,0],[0,0,1/a,0],[0,a,0,0],[0,0,0,0]],complex)
 return reference_gate(name,p)  # Separate pinned-SDK/projector oracle, never compiler code.
def apply(M,qs,K,n):
 # Matrix local basis order is |first operand,...,last operand>, global q0 LSB.
 out=np.zeros_like(K); mask=sum(1<<q for q in qs)
 for base in range(1<<n):
  if base&mask:continue
  indices=[base+sum(((b>>(len(qs)-1-k))&1)<<q for k,q in enumerate(qs)) for b in range(1<<len(qs))]
  out[indices,:]=M@K[indices,:]
 return out
def injection(n,layout):
 out=np.zeros((1<<n,1<<len(layout)),complex)
 for basis in range(1<<len(layout)):
  out[sum(((basis>>q)&1)<<p for q,p in enumerate(layout)),basis]=1
 return out
def program(ops,n=3,nc=3):
 lines=['target generic',f'qubit q[{n}]',f'bit c[{nc}]']
 for op in ops:
  g=op['op'];q=op.get('qubits',[]);p=op.get('params',[])
  if g=='measure':lines.append(f"c[{op['clbits'][0]}] = measure q[{q[0]}]")
  elif g=='if':lines.append(f"if c[{op['clbits'][0]}] == {op['condition_value']} {{")
  elif g=='else':lines.append('} else {')
  elif g=='endif':lines.append('}')
  else:lines.append(g+('('+','.join(format(x,'.17g') for x in p)+')' if p else '')+' '+', '.join(f'q[{v}]' for v in q))
 return '\n'.join(lines)+'\n'
def op(g,*q,p=()):return {'op':g,'qubits':list(q),'params':list(p)}
def operator(ops,n,initial,phase=0):
 K=injection(n,initial)*np.exp(1j*phase)
 for item in ops:
  g=item['op'];qs=item.get('qubits',[])
  if g=='barrier':continue
  if g=='gphase':K*=np.exp(1j*item['params'][0]);continue
  if g=='move':
   occupied=[b for b in range(1<<n) if all(b&(1<<q) for q in qs)]
   assert np.linalg.norm(K[occupied,:])<1e-10,'MOVE undefined |11> was occupied'
  K=apply(matrix(item),qs,K,n)
 return K
def instr(ops,n,nc,initial,final,phase=0):
 K=injection(n,initial)*np.exp(1j*phase)
 trajectories=[(tuple([0]*nc),K)]
 def block(items,ts):
  j=0
  while j<len(items):
   item=items[j];g=item['op'];qs=item.get('qubits',[])
   if g=='if':
    depth=1;k=j+1;other=None
    while depth:
     name=items[k]['op']
     if name=='if':depth+=1
     if name=='endif':depth-=1
     if depth==1 and name=='else':other=k
     k+=1
    end=k-1
    yes=items[j+1:other if other is not None else end];no=items[other+1:end] if other is not None else []
    accumulated=[]
    for bits,mat in ts:accumulated+=block(yes if bits[item['clbits'][0]]==item['condition_value'] else no,[(bits,mat)])
    ts=accumulated;j=k;continue
   if g in ('measure','reset'):
    accumulated=[]
    for bits,mat in ts:
     for outcome in (0,1):
      projector=np.zeros((2,2),complex);projector[outcome if g=='measure' else 0,outcome]=1
      changed=list(bits)
      if g=='measure':changed[item['clbits'][0]]=outcome
      out=apply(projector,qs,mat,n)
      if np.linalg.norm(out)>1e-13:accumulated.append((tuple(changed),out))
    ts=accumulated
   elif g=='barrier':pass
   elif g=='gphase':ts=[(bits,np.exp(1j*item['params'][0])*mat) for bits,mat in ts]
   else:
    if g=='move':
     occupied=[b for b in range(1<<n) if all(b&(1<<q) for q in qs)]
     assert all(np.linalg.norm(mat[occupied,:])<1e-10 for _,mat in ts),'MOVE undefined |11> was occupied'
    ts=[(bits,apply(matrix(item),qs,mat,n)) for bits,mat in ts]
   j+=1
  return ts
 trajectories=block(ops,trajectories)
 # Trace unreturned physical ancillas; compare complete channel instruments.
 out={};unused=[q for q in range(n) if q not in final];d=1<<len(final)
 for bits,K in trajectories:
  for ancilla in range(1<<len(unused)):
   fixed=sum(((ancilla>>i)&1)<<p for i,p in enumerate(unused))
   indices=[fixed+sum(((basis>>q)&1)<<p for q,p in enumerate(final)) for basis in range(d)]
   v=K[indices,:].reshape(-1)
   out[bits]=out.get(bits,np.zeros((d*d,d*d),complex))+np.outer(v,v.conj())
 return out
base={'qubits':4,'native_gates':['x','sx','rz','cz'],'all_to_all':False,'coupling':[[0,1],[1,2],[2,3]],'formats':['qiskit-native'],'route':'ibm','vendor':'ibm','device':'audit-line','supports':{'feedforward':True,'reset':True,'mid_circuit_measure':True},'capability_verified':True}
star={'qubits':5,'computational_qubits':[0,2,3],'resonator_qubits':[1,4],'native_gates':['u1q','cz','move'],'all_to_all':False,'coupling':[[0,1],[2,1],[2,4],[3,4]],'gate_loci':{'u1q':[[0],[2],[3]],'measure':[[0],[2],[3]],'reset':[[0],[2],[3]],'move':[[0,1],[2,1],[2,4],[3,4]],'cz':[[0,1],[2,1],[2,4],[3,4]]},'formats':['iqm-json'],'route':'iqm','vendor':'iqm','device':'audit-stars','qubit_labels':['QB1','CR1','QB2','QB3','CR2'],'supports':{'feedforward':True,'reset':True,'mid_circuit_measure':True},'capability_verified':True}
dynamic=[op('h',0),op('cx',0,1),{'op':'measure','qubits':[1],'clbits':[2]},
 {'op':'if','clbits':[2],'condition_value':1},op('cx',0,2),op('reset',1),{'op':'measure','qubits':[2],'clbits':[2]},
 {'op':'else'},op('cx',1,2),op('ry',0,p=[.43]),{'op':'endif'},op('cx',0,2),{'op':'measure','qubits':[0],'clbits':[0]}]


def randomized_cases(seed, count):
    rng=np.random.default_rng(seed)
    cases=[]
    for trial in range(count):
        ops=[op('ry',q,p=[rng.uniform(-6,6)]) for q in range(3)]
        for _ in range(5):
            a,b=rng.choice(3,2,replace=False)
            ops += [op('cx',int(a),int(b)),op('rz',int(a),p=[rng.uniform(-6,6)])]
        cases.append(('unitary'+str(trial),ops,False))
    return cases+[('branch-write-reset',dynamic,True)]


def compact(ir):
    """Remove only idle physical dimensions, retaining all operation/layout IDs."""
    result=copy.deepcopy(ir)
    used=sorted(set(result['initial_logical_to_physical'])|set(result['logical_to_physical'])|
                {q for item in result['instructions'] for q in item.get('qubits',[])})
    if len(used)>8:
        raise ValueError('Independent exhaustive-space bound exceeded (8 active physical qubits)')
    mapping={q:i for i,q in enumerate(used)}
    result['num_qubits']=len(used)
    for field in ('initial_logical_to_physical','logical_to_physical','resonator_qubits'):
        if field in result:result[field]=[mapping[q] for q in result[field] if q in mapping]
    for item in result['instructions']:item['qubits']=[mapping[q] for q in item.get('qubits',[])]
    for item in result.get('measurement_mapping',[]):item['qubit']=mapping[item['qubit']]
    return result


def comparison(ops, artifact, n, nc, *, full_phase=False):
    ir=compact(artifact.physical_ir)
    expected=instr(ops,n,nc,list(range(n)),list(range(n)))
    actual=instr(ir['instructions'],ir['num_qubits'],ir['num_clbits'],
                 ir['initial_logical_to_physical'],ir['logical_to_physical'],ir.get('global_phase',0))
    error=max(float(np.max(np.abs(expected.get(k,0)-actual.get(k,0)))) for k in expected.keys()|actual.keys())
    if full_phase:
        expected_operator=operator(ops,n,list(range(n)))
        actual_operator=operator(ir['instructions'],ir['num_qubits'],ir['initial_logical_to_physical'],ir.get('global_phase',0))
        error=max(error,float(np.max(np.abs(actual_operator-injection(ir['num_qubits'],ir['logical_to_physical'])@expected_operator))))
    if error>=TOLERANCE:
        raise AssertionError(f'Independent operator/instrument mismatch {error:.17g} >= {TOLERANCE}')
    return {'max_error':error,'classical_outcomes':len(expected),'full_scalar_phase_checked':full_phase,
            'initial':artifact.physical_ir['initial_logical_to_physical'],
            'final':artifact.physical_ir['logical_to_physical'],
            'native_instructions':len(ir['instructions'])}


def compile_case(directory, record, ops, n, nc, level, *, config=None, source=None):
    cache_targets(record['route'],[record])
    if source is None:
        source=directory/'case.spn';source.write_text(program(ops,n,nc),encoding='utf-8')
    return compile_file(source,target=record['device'],optimization_level=level,
                        config={'provider':record['route'],**(config or {})})


def routing_rows(directory, seed, random_count):
    records=(base,star,{**base,'device':'audit-directed','native_gates':['x','sx','rz','cx'],
                       'coupling':[[1,0],[1,2],[3,2]],'directed_connectivity':True})
    for record in records:
        for name,ops,nonunitary in randomized_cases(seed,random_count):
            if nonunitary and record['route']=='iqm':
                continue  # IQM's serializer explicitly disallows these branch fixtures.
            for level in range(4):
                artifact=compile_case(directory,record,ops,3,3,level)
                yield {'group':'routing','target':record['device'],'case':name,'level':level,
                       **comparison(ops,artifact,3,3,full_phase=not nonunitary)}


def availability_rows(directory, seed):
    sparse={**base,'device':'sparse-directed-audit','qubits':7,'native_gates':['x','sx','rz','cx'],
        'coupling':[[5,2],[5,6]],'available_qubits':[2,5,6],'directed_connectivity':True,
        'gate_loci':{g:[[2],[5],[6]] for g in ('x','sx','rz','measure','reset')}}
    sparse['gate_loci']['cx']=[[5,2],[5,6]]
    disabled={**star,'device':'disabled-resonator-audit','unavailable_qubits':[4]}
    tests=[(sparse,randomized_cases(seed,1)[0][1],3,3),(sparse,dynamic,3,3),
           (disabled,[op('h',0),op('cx',0,1),op('rz',0,p=[.8])],2,0)]
    for record,ops,n,nc in tests:
        for level in range(4):
            artifact=compile_case(directory,record,ops,n,nc,level)
            used={q for item in artifact.physical_ir['instructions'] for q in item.get('qubits',[])}
            assert not used.intersection(record.get('unavailable_qubits',[]))
            if 'available_qubits' in record:assert used<=set(record['available_qubits'])
            yield {'group':'availability','target':record['device'],'level':level,
                   **comparison(ops,artifact,n,nc,full_phase=not any(i['op']=='measure' for i in ops))}


def instrument_rows(directory):
    source=Path(__file__).with_name('fixtures')/'branch-instrument.phn'
    for level in range(4):
        artifact=compile_case(directory,base,dynamic,3,3,level,source=source)
        yield {'group':'instrument','case':'Phonon projected-wire reuse and overwritten predicate','level':level,
               **comparison(dynamic,artifact,3,3)}


def join_rows(directory):
    measure=lambda q,c:{'op':'measure','qubits':[q],'clbits':[c]}
    branch=lambda c:{'op':'if','clbits':[c],'condition_value':1}
    simple=[measure(1,0),branch(0),op('cx',0,2),op('cx',0,3),{'op':'else'},
            op('cx',0,2),op('cx',3,0),{'op':'endif'},measure(0,3),measure(3,1)]
    nested=[op('h',0),op('cx',0,1),op('h',2),measure(2,1),measure(3,0),branch(1),
            op('cx',0,3),op('cx',1,2),branch(0),op('cx',0,2),{'op':'else'},
            op('ry',3,p=[.37]),{'op':'endif'},{'op':'else'},op('cx',0,3),op('cx',1,3),
            op('rz',0,p=[-.29]),{'op':'endif'},op('cx',0,2),measure(3,0)]
    tests=[('uniform-shared-prefix','uniform',0,4,{**base,'native_gates':['u1q','cx','swap']},
            [op('cx',0,1)]*10+[op('cx',1,2)]*6+simple),
           ('heterogeneous-shared-prefix','heterogeneous',0,4,
            {**base,'native_gates':['x','y','z','cx','swap'],'gate_loci':{'x':[[0]],'y':[[1]],'z':[[2]]}},
            [op('x',0),op('y',1),op('z',2)]+simple)]
    for strategy in ('uniform','heterogeneous'):
        for level in (0,3):
            tests.append(('nested-independent-predicates',strategy,level,2,
                          {**base,'native_gates':['u1q','rz','rxx']},nested))
    for name,strategy,level,nc,record,ops in tests:
        artifact=compile_case(directory,record,ops,4,nc,level,config={'placement_strategy':strategy})
        stats=artifact.numerical_report['trial_statistics'];notes=artifact.numerical_report['notes']
        if name=='uniform-shared-prefix':
            assert artifact.physical_ir['logical_to_physical']==[1,0,2,3]
            assert stats['branch_join_swaps_removed']==2
        if name=='heterogeneous-shared-prefix':
            assert notes['placement_strategy']=='heterogeneous'
            assert stats['branch_join_swaps_removed']==6
        yield {'group':'joins','case':name,'requested_strategy':strategy,'level':level,
               **comparison(ops,artifact,4,nc),'trial_statistics':stats,'notes':notes}


def profile_rows(directory):
    ops=[op('x',0),{'op':'measure','qubits':[0],'clbits':[0]}]
    source=directory/'profile.spn';source.write_text(program(ops,1,1),encoding='utf-8')
    records=profiles()
    assert len(records)==31,'Historical 31-profile contract changed; review the inventory deliberately'
    for record in records:
        route=record['routes'][0]
        for level in (0,3):
            row={'group':'profiles','profile':record['id'],'readiness':record['readiness'],'route':route,'level':level}
            # All current profiles admit this tiny gate program offline. A future
            # intentional restriction must update its fixture, not silently skip.
            artifact=compile_file(source,target=record['id'],optimization_level=level,config={'provider':route})
            yield {**row,'format':artifact.format,**comparison(ops,artifact,1,1)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=['all','routing','availability','instrument','joins','profiles'],default='all')
    parser.add_argument('--suite',choices=['full','pr'],default='full')
    parser.add_argument('--seed',type=lambda value:int(value,0),default=SEED)
    parser.add_argument('--random-count',type=int)
    parser.add_argument('--spinorc',type=Path,default=os.environ.get('QSTACK_SPINORC'))
    parser.add_argument('--phononc',type=Path,default=os.environ.get('QSTACK_PHONONC'))
    parser.add_argument('--runtime-dir',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    random_count=args.random_count if args.random_count is not None else (3 if args.suite=='pr' else 8)
    if not 1<=random_count<=64:parser.error('--random-count must be in [1,64]')
    executables={'spinorc':binary('spinorc',args.spinorc)}
    if args.mode in ('all','instrument'):executables['phononc']=binary('phononc',args.phononc)
    for name,path in executables.items():os.environ['QSTACK_'+name.upper()]=str(path)
    if args.runtime_dir:os.environ['PATH']=str(args.runtime_dir.resolve())+os.pathsep+os.environ.get('PATH','')
    before=snapshot(executables);started=time.monotonic();rows=[];failure=None
    try:
        with tempfile.TemporaryDirectory(prefix='qstack-physical-audit-') as temporary:
            directory=Path(temporary);os.environ['QSTACK_STATE_DIR']=str(directory/'state')
            groups={'routing':lambda:routing_rows(directory,args.seed,random_count),
                    'availability':lambda:availability_rows(directory,args.seed),
                    'instrument':lambda:instrument_rows(directory),'joins':lambda:join_rows(directory),
                    'profiles':lambda:profile_rows(directory)}
            for group in groups if args.mode=='all' else [args.mode]:
                for row in groups[group]():
                    rows.append(row)
                    print(f"{group}: {len(rows)} cases; residual {row['max_error']:.3g}",flush=True)
    except Exception as error:
        failure={'type':type(error).__name__,'message':str(error)}
    after=snapshot(executables)
    result={'schema_version':1,'suite':args.suite,'mode':args.mode,'seed':args.seed,'random_count':random_count,
        'threshold':TOLERANCE,'status':'failed' if failure or before!=after else 'passed',
        'source_and_binaries_unchanged':before==after,'before':before,'after':after,
        'environment':environment(('numpy','qiskit')),'elapsed_seconds':time.monotonic()-started,
        'cases':len(rows),'groups':{group:sum(row['group']==group for row in rows) for group in {row['group'] for row in rows}},
        'max_error':max((row['max_error'] for row in rows),default=None),'failure':failure,'rows':rows,
        'coverage':'Finite complete operators and Choi instruments, arbitrary reference-entangled input; no pulse, calibration or hardware certification',
        'iqm_boundary':'MOVE evaluated only with vacant resonators; undefined doubly-occupied subspace is checked for zero amplitude'}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
    print(json.dumps({key:value for key,value in result.items() if key not in ('rows','before','after')},indent=2))
    return 0 if result['status']=='passed' else 1


if __name__=='__main__':
    raise SystemExit(main())
