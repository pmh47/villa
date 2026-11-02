import os,sys,time,json,cProfile,pstats,argparse,hashlib
import numpy as np
import torch
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('store');p.add_argument('--spiral',required=True);p.add_argument('--out',required=True);p.add_argument('--tracks',action='store_true');p.add_argument('--profile',action='store_true');args=p.parse_args()
sys.path.insert(0,args.spiral)
from config import Config
from object_store_inputs import load_fit_inputs
from tracks import PackedTrackCollection
config=Config().as_dict();config.update(object_store_path=args.store,object_store_refresh_links=False,z_begin=10000,z_end=11000,input_use_tracks=args.tracks)
print('START',args.store,flush=True)
prof=cProfile.Profile();start=time.monotonic()
if args.profile:prof.enable()
inputs=load_fit_inputs(config,log=lambda x:print(x,flush=True))
if args.profile:
 prof.disable();prof.dump_stats(args.out+'.prof');pstats.Stats(prof).sort_stats('cumtime').print_stats(35)
elapsed=time.monotonic()-start
# Compare every materialized constraint, not just the input manifest digest.
def digest_value(value):
 h=hashlib.sha256()
 def visit(value):
  if isinstance(value,torch.Tensor):value=value.detach().cpu().numpy()
  if isinstance(value,np.ndarray):
   h.update(b'array');h.update(str(value.dtype).encode());h.update(str(value.shape).encode());h.update(value.tobytes());return
  if isinstance(value,np.generic):value=value.item()
  if isinstance(value,dict):
   h.update(b'dict')
   for key in sorted(value,key=lambda x:(type(x).__name__,str(x))):visit(key);visit(value[key])
  elif isinstance(value,(list,tuple)):
   h.update(b'list')
   for item in value:visit(item)
  elif hasattr(value,'__dict__'):visit(vars(value))
  else:h.update(json.dumps(value,sort_keys=True).encode()+b'\0')
 visit(value);return h.hexdigest()
output_hashes={name:digest_value(getattr(inputs,name)) for name in
 ['patches','point_collections','fibers','fiber_links','patch_overlaps','dropped_attachments','outer_shell','tracks']}
report={'elapsed':elapsed,'output_hashes':output_hashes,'digest':inputs.digest,'counts':dict(inputs.counts),'report':inputs.report()}
Path(args.out+'.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)
