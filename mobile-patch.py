from pathlib import Path
p=Path('game/public/game.js');s=p.read_text(encoding='utf-8')
s=s.replace("return document.pointerLockElement===renderer.domElement;","return mobileMode ? mobileActive : document.pointerLockElement===renderer.domElement;")
s=s.replace("  try {\n    try {\n      await renderer.domElement.requestPointerLock", "  if(mobileMode){mobileActive=true;el.menu.style.display='none';touchUI.hidden=false;return;}\n  try {\n    try {\n      await renderer.domElement.requestPointerLock")
s=s.replace('  document.exitPointerLock();','  if(document.exitPointerLock)document.exitPointerLock();')
s=s.replace("  initialized=false;\n  keys.clear();","  initialized=false;\n  if(mobileMode)pauseTouch();\n  keys.clear();")
s+='''
const mobileMode=matchMedia('(pointer:coarse)').matches||navigator.maxTouchPoints>0;
let mobileActive=false;
const touchUI=document.createElement('div');
touchUI.id='touchUI';touchUI.hidden=true;
touchUI.innerHTML='<div id="touchMove"><span></span></div><div id="touchLook"></div><button id="touchFire">射撃</button><button id="touchWalk">걷기</button><button id="touchMenu">메뉴</button>';
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
document.getElementById('touchMenu').onclick=pauseTouch;
addEventListener('blur',()=>{if(mobileMode)pauseTouch();});
document.addEventListener('visibilitychange',()=>{if(mobileMode&&document.hidden)pauseTouch();});
if(mobileMode){document.body.style.touchAction='none';el.status.textContent='휴대폰 터치 조작 지원 · 가로 화면을 권장합니다.';}
'''
p.write_text(s,encoding='utf-8')
p=Path('game/public/style.css');s=p.read_text(encoding='utf-8');s+='''
#touchUI[hidden]{display:none}#touchUI{position:fixed;inset:0;z-index:4;pointer-events:none;user-select:none;-webkit-user-select:none}#touchUI>*{pointer-events:auto;touch-action:none}#touchMove{position:absolute;left:max(24px,env(safe-area-inset-left));bottom:30px;width:140px;height:140px;border-radius:50%;background:#ffffff20;border:2px solid #ffffff70;display:flex;align-items:center;justify-content:center}#touchMove span{width:45px;height:45px;border-radius:50%;background:#ffffff70;pointer-events:none}#touchLook{position:absolute;left:45%;right:0;top:60px;bottom:0}#touchUI button{position:absolute;border-radius:50%;background:#263749aa;color:white;border:2px solid #ffffff90;font-size:17px}#touchFire{right:30px;bottom:45px;width:86px;height:86px}#touchWalk{right:130px;bottom:35px;width:60px;height:60px}#touchMenu{right:15px;top:12px;width:58px;height:42px;border-radius:12px!important}
''';p.write_text(s,encoding='utf-8')
