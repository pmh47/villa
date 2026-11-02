import argparse,sys,time,hashlib,json,resource
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('store');p.add_argument('--spiral',required=True);p.add_argument('--out',required=True);args=p.parse_args()
sys.path.insert(0,args.spiral)
import numpy as np
from vc_object_store import ObjectStore
from object_store_inputs import InputRequest,read_snapshot,materialize_tracks
from tracks import PackedTrackCollection
start=time.monotonic()
with ObjectStore.open(args.store,read_only=True) as store:
 snapshot=read_snapshot(store,InputRequest())
 snapshot_seconds=time.monotonic()-start;print('snapshot',snapshot_seconds,flush=True)
 start=time.monotonic();inputs,counts=materialize_tracks(store,snapshot,z_begin=0,z_end=20000)
 materialize_seconds=time.monotonic()-start;print('materialize',materialize_seconds,flush=True)
 arrays={name:getattr(inputs.tracks,name) for name in ['coordinates','offsets','source_ids','family_codes','arclengths','tortuosities']}
 arrays.update({'crossings.'+name:value for name,value in inputs.crossings.items()})
 result={'snapshot_seconds':snapshot_seconds,'materialize_seconds':materialize_seconds,'peak_rss_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,'counts':dict(counts),'hashes':{name:hashlib.sha256(value.tobytes()).hexdigest() for name,value in arrays.items()},'shapes':{name:list(value.shape) for name,value in arrays.items()}}
 Path(args.out).write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
