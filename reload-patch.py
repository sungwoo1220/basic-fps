from pathlib import Path
p=Path('game/server.js');s=p.read_text(encoding='utf-8')
s=s.replace('function shoot(p,now,viewTime) {','''function reload(p,now) {
  if(p.reloadUntil||p.ammo>=12)return;
  p.reloadUntil=now+2300;
}
function shoot(p,now,viewTime) {
  if(p.reloadUntil&&now>=p.reloadUntil){p.ammo=12;p.reloadUntil=0;}
  if(p.reloadUntil)return;
  if(p.ammo<=0){reload(p,now);return;}
''')
s=s.replace('  p.lastShot=now;','  p.lastShot=now;\n  p.ammo--;')
s=s.replace('  p.hp=100;','  p.hp=100;\n  p.ammo=12;\n  p.reloadUntil=0;')
s=s.replace("    if(m.type==='join'", "    if(m.type==='reload'){reload(p,performance.now());return;}\n    if(m.type==='join'")
s=s.replace('shield:now<p.shieldUntil','ammo:p.ammo??12,reloadRemaining:Math.max(0,(p.reloadUntil||0)-now),shield:now<p.shieldUntil')
s=s.replace('const now=performance.now();','const now=performance.now();\n  for(const q of players.values())if(q.reloadUntil&&now>=q.reloadUntil){q.ammo=12;q.reloadUntil=0;}')
p.write_text(s,encoding='utf-8')
p=Path('game/public/game.js');s=p.read_text(encoding='utf-8')
s='let ammo=12,reloadEnd=0;\n'+s
s=s.replace('      reconcile(p);','      reconcile(p);\n      ammo=p.ammo??12;reloadEnd=now+(p.reloadRemaining||0);')
s=s.replace('HP ${p.hp} |','HP ${p.hp} | ${p.reloadRemaining>0?"재장전 중":`${p.ammo??12} / 12`} |')
s=s.replace('if((bits&16)&&now-lastShot>=weapon.interval)', 'if((bits&16)&&ammo>0&&now>=reloadEnd&&now-lastShot>=weapon.interval)')
s=s.replace('  gun.position.z=recoil;','''  const reloadProgress=reloadEnd>now?1-(reloadEnd-now)/2300:0;
  const reloadPose=reloadProgress>0?Math.sin(Math.PI*reloadProgress):0;
  gun.position.z=recoil+reloadPose*.15;
  gun.position.y=-reloadPose*.18;
  gun.rotation.z=-reloadPose*.65;
  gun.rotation.x=reloadPose*.35;''')
s+='''
function requestReload(){if(connected&&initialized&&ammo<12&&performance.now()>=reloadEnd)send({type:'reload'});}
addEventListener('keydown',e=>{if(e.code==='KeyR'&&!e.repeat&&locked())requestReload();});
if(matchMedia('(pointer:coarse)').matches){
 const reloadButton=document.createElement('button');
 reloadButton.textContent='재장전';
 reloadButton.style.cssText='position:fixed;right:22px;bottom:180px;z-index:9;padding:18px;border-radius:18px;touch-action:none;background:#203348cc;color:white;border:1px solid #adc1d9';
 reloadButton.addEventListener('pointerdown',e=>{e.preventDefault();requestReload();});
 document.body.append(reloadButton);
}
'''
p.write_text(s,encoding='utf-8')
