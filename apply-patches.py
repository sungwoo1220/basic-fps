from pathlib import Path
r=Path('game')
p=r/'public/game.js'
s=p.read_text(encoding='utf-8')
if "createTracers" not in s:
 s="import {createTracers} from '/tracer.js';\n"+s
 s=s.replace("const renderer=new THREE.WebGLRenderer", "const tracers=createTracers(THREE,scene,camera);\nconst renderer=new THREE.WebGLRenderer")
 s=s.replace("if(m.type==='shot') {", "if(m.type==='shot') {\n    tracers.add(m,m.id===ownId);")
 s=s.replace('  updateEffects(now);','  updateEffects(now);\n  tracers.update(now);')
p.write_text(s,encoding='utf-8')
p=r/'server.js'
s=p.read_text(encoding='utf-8').replace("'/':'index.html'", "'/tracer.js':'tracer.js','/':'index.html'")
s=s.replace("type:'shot',id:p.id,to:","type:'shot',id:p.id,from:o,to:")
p.write_text(s,encoding='utf-8')
