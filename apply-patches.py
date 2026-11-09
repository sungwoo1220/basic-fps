from pathlib import Path
r=Path('game')
p=r/'public/game.js'
s=p.read_text(encoding='utf-8')
if 'createTracers' not in s:
 s="import {createTracers} from '/tracer.js';\n"+s
 s=s.replace('const renderer=new THREE.WebGLRenderer','const tracers=createTracers(THREE,scene,camera);\nconst renderer=new THREE.WebGLRenderer')
 s=s.replace("if(m.type==='shot') {","if(m.type==='shot') {\n    tracers.add(m,m.id===ownId);")
 s=s.replace('  updateEffects(now);','  updateEffects(now);\n  tracers.update(now);')
p.write_text(s,encoding='utf-8')
p=r/'server.js'
s=p.read_text(encoding='utf-8').replace("'/':'index.html'","'/tracer.js':'tracer.js','/':'index.html'")
s=s.replace("type:'shot',id:p.id,to:","type:'shot',id:p.id,from:o,to:")
p.write_text(s,encoding='utf-8')
p=Path('index.html');s=p.read_text(encoding='utf-8').replace('<button id="rollback">이전 버전 복구</button>','');p.write_text(s,encoding='utf-8')
p=Path('ui.js');s=p.read_text(encoding='utf-8').replace("$('rollback').onclick=()=>window.launcher.rollback();\n",'');p.write_text(s,encoding='utf-8')
p=Path('preload.cjs');s=p.read_text(encoding='utf-8').replace("rollback:()=>ipcRenderer.invoke('rollback'),",'');p.write_text(s,encoding='utf-8')
p=Path('main.cjs');s=p.read_text(encoding='utf-8');s=s.replace(",previous:old?{folder:old.folder,version:old.version}:null",'');s='\n'.join(line for line in s.split('\n') if "ipcMain.handle('rollback'" not in line);p.write_text(s,encoding='utf-8')
