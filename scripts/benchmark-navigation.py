#!/usr/bin/env python3
import argparse,sys,time,json
from pathlib import Path
parser=argparse.ArgumentParser(description="Replay the 1024x768 Folio navigation benchmark on a leased VM")
parser.add_argument("run")
parser.add_argument("--tools", required=True, type=Path, help="AROS tools directory containing vmctl.py")
parser.add_argument("--output", required=True, type=Path)
args=parser.parse_args()
sys.path.insert(0,str(args.tools))
from vmctl import VM,png
name=args.run
root=args.output
root.mkdir(parents=True,exist_ok=True)
events=[]
with VM() as vm:
 if (vm.w,vm.h)!=(1024,768): raise RuntimeError("This protocol requires a 1024x768 display and the documented Folio window")
 vm.move(650,400)
 for direction,bursts in [('wheel-down',16),('wheel-up',4)]:
  for i in range(bursts):
   t=time.monotonic()
   if direction == "wheel-down" and i in (3,7,11):
    vm.move(180,400)
    time.sleep(.04)
    for j in range(8):
     vm.button(True,"wheel-down");time.sleep(.015);vm.button(False,"wheel-down")
    vm.move(650,400)
    time.sleep(.04)
   for j in range(8):
    vm.button(True,direction);time.sleep(.015);vm.button(False,direction)
   events.append({'direction':direction,'burst':i,'time':t})
   time.sleep(max(0,.5-(time.monotonic()-t)))
  time.sleep(2)
 png(vm.shot(str(root/(name+'-end.ppm'))),str(root/(name+'-end.png')))
 vm.click(105,12)
(root/(name+'-input.json')).write_text(json.dumps(events,indent=2)+'\n')
