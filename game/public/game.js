import {createTracers} from '/tracer.js';
import * as THREE from '/three.module.js';
let ammo=12,reloadEnd=0;
let serverOffset=0;
const mobileMode=matchMedia('(pointer:coarse)').matches;
let mobileActive=false;
const {
  arena,move,characterParts,weapon,movementSpread,recoveredBloom
}
=window.FPS;
const el=Object.fromEntries(['menu','play','status','name','sens','sensValue','hud','feed','board','hit','aa','fps'].map(id=>[id,document.getElementById(id)]));
const scene=new THREE.Scene();
scene.background=new THREE.Color(0x93a7bb);
const camera=new THREE.PerspectiveCamera(70,1,.05,100);
camera.rotation.order='YXZ';
const tracers=createTracers(THREE,scene,camera);
let antialias=stored('fps-antialias','1')==='1';
function createRenderer() {
  const r=new THREE.WebGLRenderer({antialias,powerPreference:'high-performance',alpha:false});
  r.setPixelRatio(1);
  r.outputColorSpace=THREE.SRGBColorSpace;
  return r;
}
let renderer=createRenderer();
document.body.appendChild(renderer.domElement);
const material=new THREE.MeshBasicMaterial({
  color:0x687f90
});
const geometry=new THREE.BoxGeometry(1,1,1);
const arenaObjects=[{
  x:0,y:-.125,z:0,w:40,h:.25,d:40
}, {
  x:0,y:2,z:-20.5,w:41,h:4,d:1
}, {
  x:0,y:2,z:20.5,w:41,h:4,d:1
}, {
  x:-20.5,y:2,z:0,w:1,h:4,d:40
}, {
  x:20.5,y:2,z:0,w:1,h:4,d:40
},...arena.boxes.map(b=>({
  ...b,y:b.h/2
}))];
const world=new THREE.InstancedMesh(geometry,material,arenaObjects.length);
const transform=new THREE.Object3D();
for(let i=0;
i<arenaObjects.length;
i++) {
  const b=arenaObjects[i];
  transform.position.set(b.x,b.y,b.z);
  transform.scale.set(b.w,b.h,b.d);
  transform.updateMatrix();
  world.setMatrixAt(i,transform.matrix);
  world.setColorAt(i,new THREE.Color(i===0?0x8a9ba8:i<5?0x344b5d:0x526f83));
}
world.instanceMatrix.needsUpdate=true;
scene.add(world);
const grid=new THREE.GridHelper(40,20,0x607789,0x798d9d);
grid.position.y=.003;
scene.add(grid);
grid.updateMatrix();
grid.matrixAutoUpdate=false;
world.matrixAutoUpdate=false;
const gun=new THREE.Group();
function gunPart(x,y,z,w,h,d,color) {
  const m=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({
    color,depthTest:false,depthWrite:false
  }));
  m.position.set(x,y,z);
  m.scale.set(w,h,d);
  m.renderOrder=10;
  gun.add(m);
  return m;
}
gunPart(.25,-.23,-.43,.14,.15,.32,0x283d50);
gunPart(.25,-.21,-.68,.065,.065,.2,0x152433);
const flash=gunPart(.25,-.21,-.79,.11,.11,.05,0xffd47d);
flash.visible=false;
camera.add(gun);
scene.add(camera);
const enemies=new THREE.InstancedMesh(geometry,new THREE.MeshBasicMaterial({
  color:0xffffff
}),16*characterParts.length);
enemies.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
enemies.frustumCulled=false;
enemies.count=0;
scene.add(enemies);
const outlines = new THREE.InstancedMesh(geometry, new THREE.MeshBasicMaterial({color:0xff2020,side:THREE.BackSide}),16*characterParts.length);
outlines.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
outlines.frustumCulled = false;
outlines.count = 0;
scene.add(outlines);

const normalColor=new THREE.Color(0x252a30),shieldColor=new THREE.Color(0x75dacf),bloodColor=new THREE.Color(0xa81524);
const remote=new Map();
const effects=new THREE.InstancedMesh(geometry,new THREE.MeshBasicMaterial({
  color:0xffcf83
}),32);
effects.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
effects.frustumCulled=false;
effects.count=0;
scene.add(effects);
const impacts=Array.from({
  length:32
},()=>({
  x:0,y:0,z:0,until:0
}));
let impactIndex=0;
const predicted={
  x:0,z:0
},renderPosition={
  x:0,z:0
},keys=new Set(),pending=[],outgoing=[],feed=[];
let localBloom=0;
let ownId='',epoch=-1,connected=false,initialized=false,yaw=0,pitch=0,sensitivity=1,sequence=0;
let offsetX=0,offsetZ=0,lastTime=performance.now(),accumulator=0,lastShot=-1000,flashUntil=0,hitUntil=0,recoil=0,frames=0,fpsTime=performance.now(),lastHud='',lastBoard='',lastFeed='';
function stored(key,fallback) {
  try {
    return localStorage.getItem(key)||fallback;
  }
  catch {
    return fallback;
  }
}
function save(key,value) {
  try {
    localStorage.setItem(key,String(value));
  }
  catch {
  }
}
sensitivity=Math.max(.001,Math.min(10,Number(stored('fps-valorant-sensitivity','0.350'))||.350));
el.name.value=stored('fps-name','Player');
el.sens.value=sensitivity;
el.sensValue.textContent=sensitivity.toFixed(3);
el.sens.oninput=()=>{
  sensitivity=Math.round(Math.max(.001,Math.min(10,Number(el.sens.value)||.001))*1000)/1000;
  el.sensValue.textContent=sensitivity.toFixed(3);
  save('fps-valorant-sensitivity',sensitivity);
};
let fpsLimit=Math.max(0,Math.min(1000,Math.round(Number(stored('fps-limit','0'))||0)));
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
let currentDpr=0;
function resize() {
  currentDpr=devicePixelRatio||1;
  const aspect=innerWidth/innerHeight;
  camera.aspect=aspect;
  camera.fov=THREE.MathUtils.radToDeg(2*Math.atan(Math.tan(THREE.MathUtils.degToRad(51.5))/aspect));
  camera.updateProjectionMatrix();
  renderer.setSize(Math.max(1,Math.floor(innerWidth*currentDpr)),Math.max(1,Math.floor(innerHeight*currentDpr)),false);
  renderer.domElement.style.width='100vw';
  renderer.domElement.style.height='100vh';
}
el.aa.value=antialias?'1':'0';
el.aa.onchange=()=>{
  antialias=el.aa.value==='1';
  save('fps-antialias',antialias?'1':'0');
  const old=renderer;
  renderer=createRenderer();
  old.domElement.replaceWith(renderer.domElement);
  old.dispose();
  old.forceContextLoss();
  resize();
};
addEventListener('resize',resize);
resize();
const ws=new WebSocket(`${location.protocol==='https:'?'wss':'ws'}://${location.host}`);
function locked() {
  return mobileMode ? mobileActive : document.pointerLockElement===renderer.domElement;
}
function send(data) {
  if(connected&&ws.bufferedAmount<32768)ws.send(JSON.stringify(data));
}
ws.onopen=()=>{
  connected=true;
  el.status.textContent='서버 연결 완료. 상태 수신 중…';
};
ws.onerror=()=>{
  el.status.textContent='서버 연결에 실패했습니다.';
};
ws.onclose=()=>{
  connected=false;
  initialized=false;
  if(mobileMode)pauseTouch();
  keys.clear();
  el.play.disabled=true;
  if(document.exitPointerLock)document.exitPointerLock();
  el.menu.style.display='flex';
  el.status.textContent='연결이 종료됐습니다. 서버를 확인하고 새로고침하세요.';
};
el.play.onclick=async()=>{
  if(!initialized||!connected)return;
  send({
    type:'join',name:el.name.value
  });
  save('fps-name',el.name.value);
  if(mobileMode){mobileActive=true;el.menu.style.display='none';touchUI.hidden=false;return;}
  try {
    try {
      await renderer.domElement.requestPointerLock({unadjustedMovement:true});
    } catch {
      await renderer.domElement.requestPointerLock();
      el.status.textContent='원시 마우스 입력 미지원: OS 설정에 따라 감도가 달라질 수 있습니다.';
    }
  }
  catch {
    el.status.textContent='마우스 잠금을 허용하고 입장을 다시 눌러주세요.';
  }
};
document.addEventListener('pointerlockchange',()=>{
  el.menu.style.display=locked()?'none':'flex';
  keys.clear();
  el.board.hidden=true;
});
addEventListener('blur',()=>keys.clear());
document.addEventListener('visibilitychange',()=>{
  keys.clear();
  accumulator=0;
  lastTime=performance.now();
  if(document.hidden)document.exitPointerLock();
});
const degreesPerCount=Math.PI/180*.07;
let lastRawMove=-1000;
function aim(dx,dy) {
  yaw=(yaw-dx*degreesPerCount*sensitivity)%(Math.PI*2);
  pitch=Math.max(-1.45,Math.min(1.45,pitch-dy*degreesPerCount*sensitivity));
}
document.addEventListener('pointerrawupdate',e=>{
  if(mobileMode||!locked()||!(e.movementX||e.movementY))return;
  lastRawMove=performance.now();
  aim(e.movementX,e.movementY);
});
addEventListener('mousemove',e=>{
  if(!locked()||performance.now()-lastRawMove<100)return;
  aim(e.movementX,e.movementY);
});
addEventListener('keydown',e=>{
  if(!locked())return;
  if(['KeyW','KeyA','KeyS','KeyD','Tab','Space'].includes(e.code))e.preventDefault();
  keys.add(e.code);
  if(e.code==='Tab')el.board.hidden=false;
});
addEventListener('keyup',e=>{
  keys.delete(e.code);
  if(e.code==='Tab')el.board.hidden=true;
});
addEventListener('mousedown',e=>{
  if(locked()&&e.button===0)keys.add('Fire');
});
addEventListener('mouseup',e=>{
  if(e.button===0)keys.delete('Fire');
});
addEventListener('contextmenu',e=>e.preventDefault());
function reconcile(p) {
  if(epoch!==p.epoch) {
    epoch=p.epoch;
    pending.length=0;
    outgoing.length=0;
    predicted.x=p.x;
    predicted.z=p.z;
  predicted.vx=p.vx||0;
  predicted.vz=p.vz||0;
    offsetX=offsetZ=0;
    accumulator=0;
    lastShot=performance.now()-1000;
    localBloom=0;
    keys.delete('Fire');
    initialized=true;
    el.play.disabled=false;
    el.play.textContent='입장 / 계속하기';
    el.status.textContent='연결 완료';
    return;
  }
  const oldX=predicted.x,oldZ=predicted.z;
  let count=0;
  while(count<pending.length&&pending[count].seq<=p.ack)count++;
  if(count)pending.splice(0,count);
  predicted.x=p.x;
  predicted.z=p.z;
  predicted.vx=p.vx||0;
  predicted.vz=p.vz||0;
  for(const input of pending)move(predicted,input);
  const dx=oldX-predicted.x,dz=oldZ-predicted.z;
  if(dx*dx+dz*dz<9) {
    offsetX+=dx;
    offsetZ+=dz;
  }
  else {
    offsetX=offsetZ=0;
  }
}
ws.onmessage=e=>{
  let m;
  try {
    m=JSON.parse(e.data);
  }
  catch {
    return;
  }
  const now=performance.now();
  if(m.type==='state'&&Number.isFinite(m.serverTime))serverOffset=m.serverTime-now;
  if(m.type==='welcome') {
    ownId=m.id;
    return;
  }
  if(m.type==='error') {
    el.status.textContent=m.message;
    return;
  }
  if(m.type==='hit') {
    hitUntil=0;
    return;
  }
  if(m.type==='kill') {
    feed.unshift({
      text:`${m.killer} → ${m.victim}`,until:now+5000
    });
    if(feed.length>4)feed.pop();
    return;
  }
  if(m.type==='shot') {
    tracers.add(m,m.id===ownId);
    if(m.id!==ownId) {
      const obj=remote.get(m.id);
      if(obj)obj.flashUntil=now+60;
    }
    if(m.blood) {
      const fx=impacts[impactIndex++%impacts.length];
      fx.x=m.to[0];
      fx.y=m.to[1];
      fx.z=m.to[2];
      fx.until=now+160;
    }
    return;
  }
  if(m.type!=='state')return;
  const ids=new Set();
  for(const p of m.players) {
    if(p.id===ownId) {
      reconcile(p);
      ammo=p.ammo??12;reloadEnd=now+(p.reloadRemaining||0);
      const text=`HP ${p.hp} | ${p.reloadRemaining>0?"재장전 중":`${p.ammo??12} / 12`} | K ${p.kills} / D ${p.deaths} | ${m.players.length}명`;
      if(text!==lastHud) {
        el.hud.textContent=text;
        lastHud=text;
      }
      continue;
    }
    ids.add(p.id);
    let obj=remote.get(p.id);
    if(!obj) {
      obj={
        samples:[],shield:p.shield,flashUntil:0
      };
      remote.set(p.id,obj);
    }
    obj.shield=p.shield;
    const samples=obj.samples,last=samples[samples.length-1];
    if(last&&(last.x-p.x)**2+(last.z-p.z)**2>16)samples.length=0;
    samples.push({
      x:p.x,z:p.z,t:now
    });
    if(samples.length>6)samples.shift();
  }
  for(const id of remote.keys())if(!ids.has(id))remote.delete(id);
  const board='이름                 K / D\n'+m.players.slice().sort((a,b)=>b.kills-a.kills).map(p=>`${p.name}    ${p.kills} / ${p.deaths}`).join('\n');
  if(board!==lastBoard) {
    el.board.textContent=board;
    lastBoard=board;
  }
};
function flush(now) {
  if(!outgoing.length||!connected)return;
  if(ws.bufferedAmount>=32768)return;
  send({
    type:'inputs',items:outgoing.splice(0,12)
  });
}
function simulate(now) {
  if(!initialized||!connected)return;
  const active=locked();
  let bits=0;
  if(active) {
    if(keys.has('KeyW'))bits|=1;
    if(keys.has('KeyS'))bits|=2;
    if(keys.has('KeyA'))bits|=4;
    if(keys.has('KeyD'))bits|=8;
    if(keys.has('Fire'))bits|=16;
    if(keys.has('ShiftLeft')||keys.has('ShiftRight'))bits|=32;
  }
  if(pending.length>=120||outgoing.length>=60) {
    keys.clear();
    el.status.textContent='네트워크 응답이 지연되고 있습니다.';
    return;
  }
  const input={
    seq:++sequence,keys:bits,yaw,pitch,epoch,viewTime:now+serverOffset-100
  };
  move(predicted,input);
  pending.push(input);
  outgoing.push(input);
  if((bits&16)&&ammo>0&&now>=reloadEnd&&now-lastShot>=weapon.interval) {
    localBloom=Math.min(weapon.maxBloom,recoveredBloom(localBloom,now-lastShot)+weapon.bloomPerShot);
    lastShot=now;
    flashUntil=now+45;
    recoil=.025;
  }
  if(outgoing.length>=6)flush(now);
}
function updateRemote(now) {
  let count=0;
  const time=now-100;
  for(const obj of remote.values()) {
    const s=obj.samples;
    if(!s.length)continue;
    while(s.length>2&&s[1].t<=time)s.shift();
    const a=s[0],b=s[1]||a,alpha=b.t===a.t?1:Math.max(0,Math.min(1,(time-a.t)/(b.t-a.t)));
    const x=a.x+(b.x-a.x)*alpha,z=a.z+(b.z-a.z)*alpha;
    transform.rotation.set(0,0,0);
    for(const part of characterParts) {
      transform.position.set(x+part.x,part.y,z+part.z);
      transform.scale.set(part.w,part.h,part.d);
      transform.updateMatrix();
      enemies.setMatrixAt(count,transform.matrix);
      enemies.setColorAt(count,obj.shield?shieldColor:normalColor);
      transform.scale.set(part.w+.035,part.h+.035,part.d+.035);
      transform.updateMatrix();
      outlines.setMatrixAt(count,transform.matrix);
      count++;
    }
  }
  enemies.count=count;
  outlines.count=count;
  if(count)outlines.instanceMatrix.needsUpdate=true;
  if(count) {
    enemies.instanceMatrix.needsUpdate=true;
    enemies.instanceColor.needsUpdate=true;
  }
}
function updateEffects(now) {
  let count=0;
  for(const fx of impacts) {
    if(fx.until<=now)continue;
    transform.position.set(fx.x,fx.y,fx.z);
    transform.scale.set(.07,.07,.07);
    transform.updateMatrix();
    effects.setColorAt(count,bloodColor);
    effects.setMatrixAt(count++,transform.matrix);
  }
  effects.count=count;
  if(count){effects.instanceMatrix.needsUpdate=true;if(effects.instanceColor)effects.instanceColor.needsUpdate=true;}
}
function frame(now) {
  const menu=!locked();
  if(menu)setTimeout(()=>requestAnimationFrame(frame),33);
  else requestAnimationFrame(frame);
  if(!menu&&fpsLimit>0) {
    if(now+0.01<nextRenderAt)return;
    const interval=1000/fpsLimit;
    nextRenderAt=nextRenderAt>0&&now-nextRenderAt<interval?nextRenderAt+interval:now+interval;
  }else nextRenderAt=0;
  const dt=Math.min((now-lastTime)/1000,.1);
  lastTime=now;
  if(document.hidden)return;
  accumulator=Math.min(accumulator+dt,.1);
  let steps=0;
  while(accumulator>=arena.step&&steps++<6) {
    simulate(now);
    accumulator-=arena.step;
  }
  if(steps)flush(now);
  const decay=Math.exp(-20*dt);
  offsetX*=decay;
  offsetZ*=decay;
  renderPosition.x=predicted.x;
  renderPosition.z=predicted.z;
  renderPosition.vx=predicted.vx;
  renderPosition.vz=predicted.vz;
  const latest=pending[pending.length-1];
  if(latest&&locked()) {
    move(renderPosition,latest);
    const alpha=accumulator/arena.step;
    renderPosition.x=predicted.x+(renderPosition.x-predicted.x)*alpha;
    renderPosition.z=predicted.z+(renderPosition.z-predicted.z)*alpha;
  }
  camera.position.set(renderPosition.x+offsetX,1.6,renderPosition.z+offsetZ);
  camera.rotation.set(pitch,yaw,0,'YXZ');
  recoil*=Math.exp(-22*dt);
  const reloadProgress=reloadEnd>now?1-(reloadEnd-now)/2300:0;
  const reloadPose=reloadProgress>0?Math.sin(Math.PI*reloadProgress):0;
  gun.position.z=recoil+reloadPose*.15;
  gun.position.y=-reloadPose*.18;
  gun.rotation.z=-reloadPose*.65;
  gun.rotation.x=reloadPose*.35;
  flash.visible=locked()&&now<flashUntil;
  el.hit.style.display=now<hitUntil?'block':'none';
  updateRemote(now);
  updateEffects(now);
  tracers.update(now);
  while(feed.length&&feed[feed.length-1].until<=now)feed.pop();
  const text=feed.map(x=>x.text).join('\n');
  if(text!==lastFeed) {
    el.feed.textContent=text;
    lastFeed=text;
  }
  if((devicePixelRatio||1)!==currentDpr)resize();
  renderer.render(scene,camera);
  frames++;
  if(now-fpsTime>=1000) {
    el.fps.textContent=`${Math.round(frames*1000/(now-fpsTime))} FPS`;
    frames=0;
    fpsTime=now;
  }
}
requestAnimationFrame(frame);

document.getElementById('botsApply').onclick=()=>send({type:'bots',count:Number(document.getElementById('botsCount').value)});

const touchUI=document.createElement('div');
touchUI.id='touchUI';touchUI.hidden=true;
touchUI.innerHTML='<div id="touchMove"><span></span></div><div id="touchLook"></div><button id="touchFire">발사</button><button id="touchReload">재장전</button><button id="touchWalk">걷기</button><button id="touchMenu">메뉴</button>';
document.body.append(touchUI);
const movePad=document.getElementById('touchMove'),knob=movePad.firstElementChild,lookPad=document.getElementById('touchLook');
function clearMove(){for(const key of ['KeyW','KeyS','KeyA','KeyD'])keys.delete(key);knob.style.transform='translate(0,0)';}
function pauseTouch(){mobileActive=false;keys.clear();clearMove();touchUI.hidden=true;el.menu.style.display='flex';}
function bindPad(node,update,end){let id=null;node.addEventListener('pointerdown',e=>{if(!mobileActive||id!==null)return;e.preventDefault();id=e.pointerId;node.setPointerCapture(id);update(e,true);});node.addEventListener('pointermove',e=>{if(e.pointerId===id){e.preventDefault();update(e,false);}});for(const event of ['pointerup','pointercancel','lostpointercapture'])node.addEventListener(event,e=>{if(e.pointerId===id){id=null;end();}});}
bindPad(movePad,e=>{const b=movePad.getBoundingClientRect();let x=e.clientX-b.left-b.width/2,y=e.clientY-b.top-b.height/2;const length=Math.hypot(x,y);if(length>45){x*=45/length;y*=45/length;}clearMove();knob.style.transform=`translate(${x}px,${y}px)`;if(y< -10)keys.add('KeyW');if(y>10)keys.add('KeyS');if(x< -10)keys.add('KeyA');if(x>10)keys.add('KeyD');},clearMove);
let lastTouchX=0,lastTouchY=0;
bindPad(lookPad,(e,first)=>{if(!first){const factor=.006*sensitivity;yaw=(yaw-(e.clientX-lastTouchX)*factor)%(Math.PI*2);pitch=Math.max(-1.45,Math.min(1.45,pitch-(e.clientY-lastTouchY)*factor));}lastTouchX=e.clientX;lastTouchY=e.clientY;},()=>{});
bindPad(document.getElementById('touchFire'),()=>keys.add('Fire'),()=>keys.delete('Fire'));
bindPad(document.getElementById('touchWalk'),()=>keys.add('ShiftLeft'),()=>keys.delete('ShiftLeft'));
bindPad(document.getElementById('touchReload'),(e,first)=>{if(first)requestReload();},()=>{});
document.getElementById('touchMenu').onclick=pauseTouch;
addEventListener('blur',()=>{if(mobileMode)pauseTouch();});
document.addEventListener('visibilitychange',()=>{if(mobileMode&&document.hidden)pauseTouch();});
if(mobileMode){document.body.style.touchAction='none';el.status.textContent='휴대폰 터치 조작 지원 · 가로 화면을 권장합니다.';}

function requestReload(){if(connected&&initialized&&ammo<12&&performance.now()>=reloadEnd)send({type:'reload'});}
addEventListener('keydown',e=>{if(e.code==='KeyR'&&!e.repeat&&locked())requestReload();});
