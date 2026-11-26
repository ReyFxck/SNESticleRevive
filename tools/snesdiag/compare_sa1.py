#!/usr/bin/env python3
"""Compare SNESticle snesdiag-v1 windows. Python 3, standard library only."""
import argparse, re, statistics, sys
from pathlib import Path

def read(path):
    rows=[]; rom=None; row=None; prev=None; diag={}; current_session=None; run=0; last_frame=None
    def val(s,key):
        m=re.search(r'\b'+re.escape(key)+r'=([^\s]+)',s)
        return m.group(1) if m else None
    for line in Path(path).open(encoding='utf-8',errors='replace'):
        if '[rom]' in line:
            rom=val(line,'crc32'); row=None; prev=None; current_session=None; last_frame=None; run+=1
        elif '[snes-diag]' in line:
            session=val(line,'session')
            if session!=current_session:
                if current_session is not None: run+=1; last_frame=None; row=None
                prev=None; current_session=session
            diag={k:val(line,k) for k in ['schema','level','session','window']}
        elif '[snes-frame]' in line:
            m=re.search(r'f=(\d+).*?budget=(\d+) cycles min/avg/max=(\d+)/(\d+)/(\d+).*?slow=(\d+).*?capacity=([\d.]+)',line)
            if m:
                f,b,mi,avg,ma,slow=map(int,m.groups()[:6])
                if last_frame is not None and f<=last_frame: run+=1; prev=None
                last_frame=f
                row=dict(diag,rom=rom,run=run,f=f,budget=b,avg=avg,slow=slow,capacity=float(m[7])); rows.append(row)
        elif row is not None and '[snes-perf]' in line:
            for k in ('cpu','sa1','ppu','apu','mdma','hdma','gsu','mix'):
                x=val(line,k)
                if x is not None: row[k]=int(x.rstrip('%'))
        elif row is not None and '[snes-sa1] io-rw=' in line:
            x=val(line,'io-rw'); row['reads'],row['writes']=map(int,x.split('/'))
            row['slices_total']=int(val(line,'slices'))
            row['slices']=row['slices_total']-(prev['slices_total'] if prev else 0)
        elif row is not None and '[snes-sa1-hot]' in line:
            m=re.search(r'advance/noadvance/samples=(\d+)/(\d+)/(\d+) sample-cycles bus/step/irq=(\d+)/(\d+)/(\d+)',line)
            if m:
                for k,v in zip(('advance','noadvance','samples','bus','step','irq'),m.groups()): row[k]=int(v)
        elif row is not None and '[snes-cpu-poll]' in line:
            m=re.search(r'iterations vblank/iram=(\d+)/(\d+)',line)
            if m: row['poll_vblank'],row['poll_iram']=map(int,m.groups())
        elif row is not None and '[snes-cpu-poll-active]' in line:
            x=val(line,'iram-reads')
            if x is not None: row['poll_active_iram']=int(x)
        elif row is not None and '[ps2-frame]' in line:
            m=re.search(r'avg loop/prep/submit/flip=(\d+)/(\d+)/(\d+)/(\d+) work min/avg/max=(\d+)/(\d+)/(\d+)',line)
            if m: row['loop'],row['prep'],row['submit'],row['flip'],_,row['work'],_=map(int,m.groups())
        elif row is not None and '[snes-sa1-bus]' in line:
            row['dropped']=int(val(line,'dropped-events'))
        elif row is not None and '[snes-sa1-idle]' in line:
            row['sleep_total']=int(val(line,'sleep-slices'))
            row['sleep']=row['sleep_total']-(prev['sleep_total'] if prev else 0)
            if 'slices_total' in row: prev=row
    return [r for r in rows if r.get('schema')=='snesdiag-v1' and r.get('window') and r['rom']]

def summarize(label, rows):
    n=sum(int(r['window']) for r in rows)
    cyc=sum(r['avg']*int(r['window']) for r in rows)/n
    # The logged capacity is rounded; compute equivalent throughput from EE clock.
    fps=294912000/cyc
    print(f'{label}: {len(rows)} janelas, {n} frames; {cyc:,.0f} ciclos/frame; capacidade {fps:.2f} FPS')
    for k in ['cpu','sa1','ppu','apu','mdma','hdma']:
        v=[r for r in rows if k in r]
        if v: print(f'  {k:5}: {sum(r[k]*r["avg"]*int(r["window"]) for r in v)/sum(r["avg"]*int(r["window"]) for r in v):.2f}% tempo inclusivo')
    v=[r for r in rows if 'slices' in r and 'sleep' in r and r['slices']>=0 and 0<=r['sleep']<=r['slices']]
    if v:
        slices=sum(r['slices'] for r in v); sleep=sum(r['sleep'] for r in v)
        print(f'  slices/frame: {slices/sum(int(r["window"]) for r in v):.1f}; idle: {100*sleep/max(1,slices):.2f}% das slices')
    if any('dropped' in r for r in rows): print('  eventos descartados (máximo acumulado):',max(r.get('dropped',0) for r in rows))
    hot=[r for r in rows if 'samples' in r]
    if hot:
        samples=sum(r['samples'] for r in hot)
        if samples: print('  ciclos amostrados/sincronização bus/step/irq:', '/'.join(f'{sum(r[k] for r in hot)/samples:.1f}' for k in ('bus','step','irq')))
        print('  sync avanço/sem avanço:',sum(r['advance'] for r in hot),sum(r['noadvance'] for r in hot))
    poll=[r for r in rows if 'poll_vblank' in r]
    if poll: print('  polling iterações VBlank/I-RAM:',sum(r['poll_vblank'] for r in poll),sum(r['poll_iram'] for r in poll))
    if any('poll_active_iram' in r for r in rows): print('  polling ativo, leituras reais I-RAM:',sum(r.get('poll_active_iram',0) for r in rows))
    # Frontend windows may cover a different number of source frames when frameskip is active.
    front=[r for r in rows if 'loop' in r]
    if front: print(f'  média frontend (janelas): work={statistics.mean(r["work"] for r in front):.0f} loop={statistics.mean(r["loop"] for r in front):.0f} ciclos; não é FPS medido na tela')
    print(f'  frames acima do limite: {sum(r["slow"] for r in rows)}/{n}')
    return cyc

def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('baseline'); p.add_argument('candidate',nargs='?'); p.add_argument('--crc',help='CRC32 da ROM, obrigatório se houver mais de uma ROM'); p.add_argument('--start',type=int,default=0); p.add_argument('--end',type=int,default=2**31-1); p.add_argument('--baseline-run',type=int,help='Carregamento da ROM no baseline (padrão: último)'); p.add_argument('--candidate-run',type=int,help='Carregamento da ROM no candidato (padrão: último)'); a=p.parse_args()
    def choose(path, requested_run):
        r=read(path); crcs={x['rom'] for x in r}; crc=a.crc.upper() if a.crc else next(iter(crcs)) if len(crcs)==1 else None
        if not crc: p.error(f'{path}: selecione --crc dentre {sorted(crcs)}')
        r=[x for x in r if x['rom'].upper()==crc ]
        if not r: p.error(f'{path}: nenhuma janela correspondente')
        runs=sorted({x['run'] for x in r}); selected=requested_run if requested_run is not None else runs[-1]
        print(f'{path}: carregamento {selected}, disponíveis {runs}')
        r=[x for x in r if x['run']==selected and a.start<=x['f']<=a.end]
        if not r: p.error(f'{path}: nenhuma janela no carregamento/intervalo escolhido')
        return r
    old=choose(a.baseline,a.baseline_run)
    if not a.candidate: summarize('Baseline',old); return
    new=choose(a.candidate,a.candidate_run)
    if old[0]['rom']!=new[0]['rom']: p.error('CRCs diferentes')
    if {x['level'] for x in old}!={x['level'] for x in new}: p.error('Níveis de diagnóstico diferentes; compare 1 com 1 ou 2 com 2')
    idx=lambda r:{(x['f'],x['window'],x['budget']):x for x in r}
    o,n=idx(old),idx(new); common=sorted(o.keys() & n.keys())
    if not common: p.error('Sem janelas de frame equivalentes')
    c0=summarize('Baseline',[o[k] for k in common]); c1=summarize('Candidato',[n[k] for k in common]); print(f'Ganho de capacidade: {(c0/c1-1)*100:+.2f}%; redução de ciclos: {(1-c1/c0)*100:+.2f}%')
    print('ATENÇÃO: mesmos frames não garantem mesmos inputs/cena. Compare hashes de estado e use a mesma configuração do PS2/NetherSX2.')
if __name__=='__main__': main()
