"""Use an isolated native build without touching the installed environment."""
import sys,runpy
from pathlib import Path
bindings=sys.argv[1]
sys.meta_path[:]=[f for f in sys.meta_path if type(f).__module__!='_editable_skbc_vc_object_store']
sys.path.insert(0,bindings)
import vc_object_store._native as native
print('native_build',native.__file__,flush=True)
sys.argv=sys.argv[2:]
if sys.argv[0]=='-m':
 sys.argv=sys.argv[1:];runpy.run_module(sys.argv[0],run_name='__main__',alter_sys=True)
else:
 sys.path.insert(0,str(Path(sys.argv[0]).resolve().parent));runpy.run_path(sys.argv[0],run_name='__main__')
