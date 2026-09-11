#!/usr/bin/env python3
"""Exhaustive adversarial simulation of the three exploration algorithms of the paper.

usage: simulate_algorithms.py {tc3|c4|us5} MAX_VERTICES

For every labeled graph of the algorithm's class with at most MAX_VERTICES vertices, every start,
and every sequence of adversarial choices, the run must visit all vertices and stop at the start
without reaching an observation omitted from the displayed rule tables.
"""
import itertools, sys
from functools import lru_cache
INIT, FIN, FRONT, PATH = 0, 1, 2, 3
NAME = {INIT:"init", FIN:"fin", FRONT:"front", PATH:"path"}
STOP = "stop"
def rule(c, M):
    """Algorithm 2 (C-rules). M = multiset as dict color->count. Returns (rulename, newcolor, dest) with dest color or STOP."""
    has = lambda *cs: all(M.get(x,0) > 0 for x in cs)
    if not has(INIT) and not has(FRONT) and not has(PATH): return ("C1", FIN, STOP)
    if not has(FIN) and not has(FRONT) and not has(PATH): return ("C2", FRONT, INIT)
    if c == INIT:
        if not has(FRONT) and not has(INIT): return ("C3", FIN, PATH)
        if not has(INIT): return ("C4", FIN, FRONT)
        if has(FRONT, PATH): return ("C5", PATH, INIT)
        if has(PATH): return ("C6", FRONT, INIT)
        if has(FRONT): return ("C7", FRONT, FRONT)
        return ("OMITTED", None, None)
    if c == FRONT:
        if has(FRONT): return ("C8", PATH, FRONT)
        if has(INIT): return ("C9", FRONT, INIT)
        return ("C10", FIN, PATH)
    if c == PATH:
        if not has(FRONT) and has(INIT): return ("C11", FRONT, INIT)
        if has(FRONT, INIT): return ("C12", PATH, INIT)
        if has(FRONT): return ("C13", FIN, FRONT)
        return ("C14", FIN, PATH)
    return ("ONFIN", None, None)

def check_graph(adj, n):
    """Exhaustive adversarial check of Algorithm 2 from every start. Returns (ok, reason)."""
    for s in range(n):
        start = (s, tuple([INIT]*n), frozenset([s]))
        result = {}
        onstack = set()
        def dfs(state):
            if state in result: return result[state]
            if state in onstack: return "cycle"
            onstack.add(state)
            pos, col, vis = state
            M = {}
            for u in adj[pos]: M[col[u]] = M.get(col[u],0)+1
            rname, newc, dest = rule(col[pos], M)
            out = None
            if rname in ("OMITTED","ONFIN"): out = f"{rname} at {state}"
            elif dest == STOP:
                out = None if (pos == s and len(vis) == n) else f"bad stop at {pos} vis={sorted(vis)} start={s} rule={rname}"
            else:
                newcol = list(col); newcol[pos] = newc; newcol = tuple(newcol)
                targets = [u for u in adj[pos] if col[u] == dest]
                if not targets: out = f"absent color {NAME[dest]} at {pos} rule={rname} start={s}"
                else:
                    for u in targets:
                        r = dfs((u, newcol, vis | {u}))
                        if r is not None: out = r if r != "cycle" else f"infinite execution via {rname} start={s}"; break
            onstack.discard(state); result[state] = out
            return out
        r = dfs(start)
        if r is not None: return (False, r)
    return (True, "")

def blocks(adj, n):
    """Biconnected components (as vertex sets) via edge-based DFS lowpoint."""
    disc = [-1]*n; low=[0]*n; time=[0]; stack=[]; comps=[]
    def dfs(u, parent):
        disc[u]=low[u]=time[0]; time[0]+=1
        for v in adj[u]:
            if disc[v]==-1:
                stack.append((u,v)); dfs(v,u); low[u]=min(low[u],low[v])
                if low[v]>=disc[u]:
                    comp=set()
                    while True:
                        e=stack.pop(); comp.update(e)
                        if e==(u,v): break
                    comps.append(frozenset(comp))
            elif v!=parent and disc[v]<disc[u]:
                stack.append((u,v)); low[u]=min(low[u],disc[v])
    dfs(0,-1); return comps

def in_gcb(adj, n):
    for B in blocks(adj, n):
        Bl = sorted(B); k=len(Bl)
        sub = {u:[v for v in adj[u] if v in B] for u in Bl}
        m = sum(len(sub[u]) for u in Bl)//2
        if k==2 and m==1: continue                       # bridge = K_{1,1}
        if all(len(sub[u])==2 for u in Bl) and m==k and k>=3: continue   # cycle
        # complete bipartite? 2-color and check all cross edges
        colr={Bl[0]:0}; st=[Bl[0]]; ok=True
        while st:
            u=st.pop()
            for v in sub[u]:
                if v not in colr: colr[v]=1-colr[u]; st.append(v)
                elif colr[v]==colr[u]: ok=False
        if not ok: return False
        X=[u for u in Bl if colr[u]==0]; Y=[u for u in Bl if colr[u]==1]
        if m != len(X)*len(Y): return False
    return True

def connected(adj,n):
    seen={0}; st=[0]
    while st:
        u=st.pop()
        for v in adj[u]:
            if v not in seen: seen.add(v); st.append(v)
    return len(seen)==n

rule_c4 = rule
STOP, STAY = "stop", "stay"
# ---- Algorithm 1 (TC3): colors init=0, l0=1, l1=2
I0, L0, L1 = 0, 1, 2
def rule_tc3(c, M):
    has = lambda *cs: all(M.get(x,0)>0 for x in cs)
    if c == I0:
        if not has(I0) and not has(L0) and not has(L1): return ("D1", L1, STOP)
        if has(I0) and not has(L0) and not has(L1): return ("D2", L0, I0)
        if has(I0, L0): return ("D3", L1, I0)
        if has(I0, L1): return ("D4", I0, L1)
        if has(L1): return ("D5", L1, L1)
        if has(L0): return ("D6", L1, L0)
    if c == L0:
        if has(I0, L1): return ("D7", L0, I0)
        if has(L0, L1): return ("D8", L1, L0)
        if has(L1) and not has(I0) and not has(L0): return ("D9", L1, STOP)
    if c == L1:
        if has(I0, L0): return ("D10", L0, I0)
        if has(L0, L1): return ("D11", L1, L0)
    return ("OMITTED", None, None)
# ---- Algorithm 3 (US5): colors init=0, path=1, neigh=2, fin=3, head=4
IN, PA, NE, FI, HE = 0, 1, 2, 3, 4
def rule_us5(c, M):
    has = lambda *cs: all(M.get(x,0)>0 for x in cs)
    size = sum(M.values())
    if c == IN:
        if size > 0 and all(k == IN for k in M): return ("U1", PA, IN)
        if has(NE) and not has(HE): return ("U2", PA, STAY)
        if has(HE) and has(PA): return ("U3", NE, HE)
        if has(HE) and not has(PA): return ("U4", HE, HE)
        return ("U5", HE, STAY)
    if c == HE:
        if has(IN, NE) and not has(PA) and not has(HE): return ("U6", FI, IN)
        if has(HE, NE): return ("U7", HE, NE)
        if has(HE, PA): return ("U8", PA, HE)
        if has(HE): return ("U9", FI, HE)
        if has(PA, IN): return ("U10", HE, IN)
        if has(PA): return ("U11", HE, PA)
        if has(IN): return ("U12", PA, IN)
        return ("U13", FI, STOP)
    if c == PA:
        if has(NE, HE): return ("U14", IN, HE)
        if has(NE): return ("U15", PA, NE)
        if not has(HE): return ("U16", HE, STAY)
        return ("U17", HE, HE)
    if c == NE:
        if has(PA) and not has(HE): return ("U18", IN, PA)
        return ("U19", IN, HE)
    return ("ONFIN", None, None)

def check(adj, n, rule, init):
    for s in range(n):
        start = (s, tuple([init]*n), frozenset([s])); result={}; onstack=set()
        def dfs(state):
            if state in result: return result[state]
            if state in onstack: return "cycle"
            onstack.add(state); pos,col,vis = state
            M={}
            for u in adj[pos]: M[col[u]]=M.get(col[u],0)+1
            rname,newc,dest = rule(col[pos], M); out=None
            if rname in ("OMITTED","ONFIN"): out=f"{rname} at pos={pos} col={col} start={s}"
            elif dest==STOP: out=None if (pos==s and len(vis)==n) else f"bad stop rule={rname} pos={pos} start={s}"
            else:
                newcol=list(col); newcol[pos]=newc; newcol=tuple(newcol)
                if dest==STAY: targets=[pos]
                else: targets=[u for u in adj[pos] if col[u]==dest]
                if not targets: out=f"absent color rule={rname} pos={pos} start={s}"
                else:
                    for u in targets:
                        r=dfs((u,newcol,vis|{u}))
                        if r is not None: out = f"infinite via {rname} start={s}" if r=="cycle" else r; break
            onstack.discard(state); result[state]=out; return out
        r=dfs(start)
        if r is not None: return (False, r)
    return (True,"")

def is_tree_or_cycle(adj,n):
    m=sum(len(a) for a in adj)//2
    return m==n-1 or (m==n and all(len(a)==2 for a in adj))
def in_gktf(adj,n):
    for B in blocks(adj,n):
        Bl=sorted(B); sub={u:[v for v in adj[u] if v in B] for u in Bl}; k=len(Bl)
        m=sum(len(sub[u]) for u in Bl)//2
        if m==k*(k-1)//2: continue                         # clique
        tri=any(w in sub[u] for u in Bl for v in sub[u] for w in sub[v] if w!=u)
        if tri: return False
    return True

def run(label, maxn, member, rule, init):
    for n in range(2,maxn+1):
        pairs=list(itertools.combinations(range(n),2)); tested=failed=0
        for mask in range(1<<len(pairs)):
            adj=[[] for _ in range(n)]
            for i,(a,b) in enumerate(pairs):
                if mask>>i&1: adj[a].append(b); adj[b].append(a)
            if not connected(adj,n) or not member(adj,n): continue
            tested+=1; ok,why=check(adj,n,rule,init)
            if not ok:
                failed+=1
                if failed<=3: print(f"[{label}] FAIL n={n} edges={[p for i,p in enumerate(pairs) if mask>>i&1]}: {why}", flush=True)
        print(f"[{label}] n={n}: labeled graphs tested={tested} failed={failed}", flush=True)

if __name__=="__main__":
    which=sys.argv[1]; maxn=int(sys.argv[2])
    if which=="tc3": run("TC3 trees+cycles", maxn, is_tree_or_cycle, rule_tc3, I0)
    if which=="us5": run("US5 G_KTF", maxn, in_gktf, rule_us5, IN)
    if which=="c4":  run("C4 G_CB", maxn, in_gcb, rule_c4, 0)
