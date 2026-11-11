'use strict';
const http=require('http');
const fs=require('fs');
const path=require('path');
const {
  performance
}
=require('perf_hooks');
const {
  WebSocketServer,WebSocket
}
=require('ws');
const {
  arena,move,ray,characterHit,weapon,movementSpread,recoveredBloom,spreadDirection
}
=require('./public/shared.js');
const players=new Map();
const port=Number(process.env.PORT)||3000;
const routes={
  '/three.module.js':'three.module.js','/tracer.js':'tracer.js','/':'index.html','/index.html':'index.html','/game.js':'game.js','/shared.js':'shared.js','/style.css':'style.css'
};
const assets=new Map(Object.entries(routes).map(([url,name])=>[url, {
  data:fs.readFileSync(path.join(__dirname,'public',name)),mime:name.endsWith('.js')?'application/javascript':name.endsWith('.css')?'text/css':'text/html; charset=utf-8'
}]));
const server=http.createServer((req,res)=>{
  let pathname;
  try {
    pathname=new URL(req.url,'http://localhost').pathname;
  }
  catch {
    res.writeHead(400);
    res.end();
    return;
  }
  const asset=assets.get(pathname);
  if(!asset) {
    res.writeHead(404);
    res.end('Not found');
    return;
  }
  res.writeHead(200, {
    'Content-Type':asset.mime,'Cache-Control':'no-cache','X-Content-Type-Options':'nosniff'
  });
  res.end(asset.data);
});
const wss=new WebSocketServer({
  server,maxPayload:8192,perMessageDeflate:false
});
let nextId=1,lastTick=performance.now(),snapshotElapsed=0;
function send(ws,data) {
  if(ws&&ws.readyState===WebSocket.OPEN&&ws.bufferedAmount<65536)ws.send(data);
}
function broadcast(event) {
  const data=JSON.stringify(event);
  for(const p of players.values())send(p.ws,data);
}
function spawn(p) {
  let best=arena.spawns[0],bestScore=-1;
  for(const s of arena.spawns) {
    let score=Infinity;
    for(const q of players.values())if(q!==p)score=Math.min(score,(s[0]-q.x)**2+(s[1]-q.z)**2);
    if(score>bestScore) {
      best=s;
      bestScore=score;
    }
  }
  p.vx=0;
  p.vz=0;
  p.x=best[0];
  p.z=best[1];
  p.hp=100;
  p.ammo=12;
  p.reloadUntil=0;
  p.epoch++;
  p.shieldUntil=performance.now()+1500;
  p.queue.length=0;
  p.fire=false;
  p.bloom=0;
}
function historical(q,time) {
  const h=q.history||[];
  if(!h.length)return q;
  let a=h[0],b=a;
  for(const sample of h){if(sample.t<=time)a=sample;if(sample.t>=time){b=sample;break;}b=sample;}
  if(a.epoch!==q.epoch||b.epoch!==q.epoch)return null;
  const f=b.t>a.t?Math.max(0,Math.min(1,(time-a.t)/(b.t-a.t))):0;
  return {x:a.x+(b.x-a.x)*f,z:a.z+(b.z-a.z)*f};
}
function record(p,now){
  if(!p.history)p.history=[];
  p.history.push({t:now,x:p.x,z:p.z,epoch:p.epoch});
  while(p.history.length&&p.history[0].t<now-400)p.history.shift();
}
function reload(p,now) {
  if(p.reloadUntil||p.ammo>=12)return;
  p.reloadUntil=now+2300;
}
function shoot(p,now,viewTime) {
  if(p.reloadUntil&&now>=p.reloadUntil){p.ammo=12;p.reloadUntil=0;}
  if(p.reloadUntil)return;
  if(p.ammo<=0){reload(p,now);return;}

  if(now-p.lastShot<weapon.interval)return;
  const bloom=recoveredBloom(p.bloom||0,now-p.lastShot);
  const spread=movementSpread(Math.hypot(p.vx||0,p.vz||0))+bloom;
  const o=[p.x,1.6,p.z],d=spreadDirection(p.yaw,p.pitch,spread);
  p.bloom=Math.min(weapon.maxBloom,bloom+weapon.bloomPerShot);
  p.lastShot=now;
  p.ammo--;
  let distance=100,target=null,hitDamage=0;
  for(const b of arena.boxes)distance=Math.min(distance,ray(o,d,[b.x-b.w/2,0,b.z-b.d/2],[b.x+b.w/2,b.h,b.z+b.d/2]));
  for(const q of players.values()) {
    if(q===p||now<q.shieldUntil)continue;
    const pose=historical(q,Math.max(now-200,Math.min(now,Number.isFinite(viewTime)?viewTime:now)));
    if(!pose)continue;
    const hit=characterHit(o,d,pose);
    const t=hit.distance;
    if(t<distance) {
      distance=t;
      target=q;
      hitDamage=hit.damage;
    }
  }
  if(target) {
    const damage = hitDamage;
    target.hp -= damage;
    send(p.ws,JSON.stringify({
      type:'hit',damage,part:damage===100?'head':damage===25?'body':'legs'
    }));
    if(target.hp<=0) {
      p.kills++;
      target.deaths++;
      spawn(target);
      broadcast({
        type:'kill',killer:p.name,victim:target.name
      });
    }
  }
  broadcast({
    type:'shot',id:p.id,from:o,blood:!!target,to:o.map((v,k)=>v+d[k]*Math.min(distance,50)),impact:distance<100
  });
}
wss.on('connection',ws=>{
  if(players.size>=16) {
    send(ws,JSON.stringify({
      type:'error',message:'서버가 가득 찼습니다 (16명).'
    }));
    ws.close();
    return;
  }
  const p={
    id:String(nextId++),ws,name:'Player',x:0,z:0,yaw:0,pitch:0,hp:100,kills:0,deaths:0,epoch:0,ack:0,received:0,queue:[],credit:2,fire:false,lastShot:-1000,lastInput:performance.now(),rateTime:performance.now(),rate:0,alive:true
  };
  spawn(p);
  players.set(p.id,p);
  send(ws,JSON.stringify({
    type:'welcome',id:p.id
  }));
  ws.on('pong',()=>{
    p.alive=true;
  });
  ws.on('message',raw=>{
    const now=performance.now();
  for(const q of players.values())if(q.reloadUntil&&now>=q.reloadUntil){q.ammo=12;q.reloadUntil=0;}
    if(now-p.rateTime>=1000) {
      p.rateTime=now;
      p.rate=0;
    }
    if(++p.rate>90) {
      ws.close(1008,'Rate limit');
      return;
    }
    let m;
    try {
      m=JSON.parse(raw);
    }
    catch {
      return;
    }
    if(!m||typeof m!=='object')return;
    if(m.type==='reload'){reload(p,performance.now());return;}
    if(m.type==='join'&&typeof m.name==='string')p.name=m.name.replace(/[\x00-\x1f]/g,'').trim().slice(0,16)||'Player';
    if(m.type==='bots'&&['127.0.0.1','::1','::ffff:127.0.0.1'].includes(ws._socket.remoteAddress)){setBots(Math.max(0,Math.min(6,Math.floor(Number(m.count)||0))));return;}
    if(m.type!=='inputs'||!Array.isArray(m.items)||m.items.length>12)return;
    p.lastInput=now;
    for(const i of m.items) {
      if(!i||!Number.isSafeInteger(i.seq)||i.seq<=p.received||i.seq>p.received+600||!Number.isFinite(i.yaw)||!Number.isFinite(i.pitch)||!Number.isInteger(i.keys)||i.keys<0||i.keys>63||i.epoch!==p.epoch)continue;
      p.received=i.seq;
      if(p.queue.length>=120) {
        ws.close(1008,'Input backlog');
        return;
      }
      p.queue.push({
        viewTime:Number.isFinite(i.viewTime)?Math.max(now-200,Math.min(now,i.viewTime)):now,seq:i.seq,keys:i.keys,yaw:i.yaw%(Math.PI*2),pitch:Math.max(-1.45,Math.min(1.45,i.pitch))
      });
    }
  });
  ws.on('error',()=>{
  });
  ws.on('close',()=>players.delete(p.id));
});
function setBots(count){
  for(const [id,p] of players)if(p.bot)players.delete(id);
  const positions=[[-6,-13],[0,-13],[6,-13],[-6,13],[0,13],[6,13]];
  for(let n=0;n<count&&players.size<16;n++){
    const p={id:'bot-'+n,bot:true,mode:n%2?'strafe':'still',phase:n,name:n%2?'이동봇 '+(n+1):'정지봇 '+(n+1),x:positions[n][0],z:positions[n][1],vx:0,vz:0,yaw:0,pitch:0,hp:100,kills:0,deaths:0,epoch:1,ack:0,queue:[],shieldUntil:0,bloom:0};
    players.set(p.id,p);
  }
}
setBots(3);
setInterval(()=>{
  const now=performance.now(),dt=Math.min((now-lastTick)/1000,.1);
  lastTick=now;
  for(const p of players.values())record(p,now);
  for(const p of players.values()) {
    if(p.bot){
      if(p.mode==='strafe'){
        const direction=Math.sin(now/1100+p.phase)>0?8:4;
        move(p,{keys:direction,yaw:0});
      }
      continue;
    }
    p.credit=Math.min(6,p.credit+dt*60);
    while(p.credit>=1&&p.queue.length) {
      const i=p.queue.shift();
      move(p,i);
      p.yaw=i.yaw;
      p.pitch=i.pitch;
      p.fire=!!(i.keys&16);
      p.ack=i.seq;
      p.credit--;
      if(p.fire)shoot(p,now,i.viewTime);
    }
    if(now-p.lastInput>250)p.fire=false;

  }
  snapshotElapsed+=dt;
  if(snapshotElapsed>=.05) {
    snapshotElapsed%=.05;
    const data=JSON.stringify({
      type:'state',serverTime:now,players:[...players.values()].map(p=>({
        id:p.id,name:p.name,x:p.x,z:p.z,vx:p.vx,vz:p.vz,yaw:p.yaw,hp:p.hp,kills:p.kills,deaths:p.deaths,epoch:p.epoch,ack:p.ack,ammo:p.ammo??12,reloadRemaining:Math.max(0,(p.reloadUntil||0)-now),shield:now<p.shieldUntil
      }))
    });
    for(const p of players.values())send(p.ws,data);
  }
},1000/60);
setInterval(()=>{
  for(const p of players.values()) {
    if(p.bot)continue;
    if(!p.alive) {
      p.ws.terminate();
      continue;
    }
    p.alive=false;
    p.ws.ping();
  }
},15000).unref();
server.listen(port,'0.0.0.0',()=>console.log(`Basic FPS v2: http://localhost:${port} | Ctrl+C to stop`));
