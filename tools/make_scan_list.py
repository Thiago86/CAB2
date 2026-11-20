"""Gera recomp/scan_list.txt: funcoes folha de biblioteca sem acesso a hardware, candidatas ao teste de identificacao.
uso: make_scan_list.py out recomp"""
import csv,json,sys
out,dst=sys.argv[1],sys.argv[2]
part=json.load(open(out+'/recomp_partial.json')); sel=[]
for r in csv.DictReader(open(out+'/symbols.csv')):
    t=set(r['tags'].split(';')); a=int(r['address'],16)
    if r['region'] in('lib_c','lib_asm') and 'leaf' in t and not t&{'pvr','aica','ta_or_sq_const','sh4_reg','vram','holly_sys','gdrom_reg','sq_pref'} and int(r['insn_count'])<=200 and '%08x'%a not in part:
        sel.append(a)
open(dst+'/scan_list.txt','w').write('\n'.join('%08x'%a for a in sel)+'\n'); print(len(sel),'candidatas')
