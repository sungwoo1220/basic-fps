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
p=r/'public/index.html';s=p.read_text(encoding='utf-8');s=s.replace('<button id="play"','<label>FPS 제한 (0 = 제한 없음)<input id="fpsLimit" type="number" min="0" max="1000" step="1" value="0"></label>\n<button id="play"');p.write_text(s,encoding='utf-8')
p=r/'public/game.js';s=p.read_text(encoding='utf-8');s=s.replace('function resize() {', '''let fpsLimit=Math.max(0,Math.min(1000,Math.round(Number(stored('fps-limit','0'))||0)));
let nextRenderAt=0;
const fpsLimitInput=document.getElementById('fpsLimit');
fpsLimitInput.value=String(fpsLimit);
fpsLimitInput.onchange=()=>{
  const value=Number(fpsLimitInput.value);
  fpsLimit=Number.isFinite(value)?Math.max(0,Math.min(1000,Math.round(value))):0;
  fpsLimitInput.value=String(fpsLimit);
  save('fps-limit',fpsLimit);
  nextRenderAt=0;
};
function resize() {''');s=s.replace('  renderer.render(scene,camera);', '''  if(fpsLimit>0){
    if(now+0.01<nextRenderAt)return;
    const interval=1000/fpsLimit;
    nextRenderAt=nextRenderAt>0&&now-nextRenderAt<interval?nextRenderAt+interval:now+interval;
  }else nextRenderAt=0;
  renderer.render(scene,camera);''');p.write_text(s,encoding='utf-8')
