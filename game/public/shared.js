(function(root) {
  'use strict';
  const arena={
    size:40,radius:.35,speed:5.4,walkSpeed:3.24,acceleration:54,deceleration:42,step:1/60,boxes:[{
      x:0,z:0,w:5,d:2,h:3
    }, {
      x:-9,z:-7,w:3,d:7,h:3
    }, {
      x:9,z:7,w:3,d:7,h:3
    }, {
      x:9,z:-9,w:5,d:2,h:3
    }, {
      x:-9,z:9,w:5,d:2,h:3
    }],spawns:[[-17,-17],[17,17],[-17,17],[17,-17],[0,-17],[0,17]]
  };
  function clear(x,z) {
    if(Math.abs(x)>19.5||Math.abs(z)>19.5)return false;
    for(const b of arena.boxes)if(Math.abs(x-b.x)<b.w/2+arena.radius&&Math.abs(z-b.z)<b.d/2+arena.radius)return false;
    return true;
  }
  function move(p, i) {
    const forward = ((i.keys & 1) ? 1 : 0) - ((i.keys & 2) ? 1 : 0);
    const right = ((i.keys & 8) ? 1 : 0) - ((i.keys & 4) ? 1 : 0);
    const length = Math.hypot(forward, right);
    const speed = (i.keys & 32) ? arena.walkSpeed : arena.speed;
    const sin = Math.sin(i.yaw), cos = Math.cos(i.yaw);
    const targetX = length ? (-sin * forward + cos * right) * speed / length : 0;
    const targetZ = length ? (-cos * forward - sin * right) * speed / length : 0;
    p.vx = Number.isFinite(p.vx) ? p.vx : 0;
    p.vz = Number.isFinite(p.vz) ? p.vz : 0;
    const dx = targetX - p.vx, dz = targetZ - p.vz;
    const difference = Math.hypot(dx, dz);
    const change = (length ? arena.acceleration : arena.deceleration) * arena.step;
    const ratio = difference ? Math.min(1, change / difference) : 1;
    p.vx += dx * ratio;
    p.vz += dz * ratio;
    const x = p.x + p.vx * arena.step;
    if (clear(x, p.z)) p.x = x;
    else p.vx = 0;
    const z = p.z + p.vz * arena.step;
    if (clear(p.x, z)) p.z = z;
    else p.vz = 0;
  }
  function ray(o,d,min,max) {
    let lo=0,hi=100;
    for(let k=0;
    k<3;
    k++) {
      if(Math.abs(d[k])<1e-8) {
        if(o[k]<min[k]||o[k]>max[k])return Infinity;
      }
      else {
        let a=(min[k]-o[k])/d[k],b=(max[k]-o[k])/d[k];
        if(a>b) {
          const t=a;
          a=b;
          b=t;
        }
        lo=Math.max(lo,a);
        hi=Math.min(hi,b);
        if(hi<lo)return Infinity;
      }
    }
    return lo;
  }
  const characterParts = [
    {x:0,y:1.65,z:0,w:.34,h:.4,d:.34,damage:100},
    {x:0,y:1.05,z:0,w:.62,h:.8,d:.36,damage:25},
    {x:-.39,y:1.08,z:0,w:.16,h:.65,d:.28,damage:25},
    {x:.39,y:1.08,z:0,w:.16,h:.65,d:.28,damage:25},
    {x:-.17,y:.325,z:0,w:.24,h:.65,d:.3,damage:20},
    {x:.17,y:.325,z:0,w:.24,h:.65,d:.3,damage:20}
  ];
  function characterHit(origin, direction, player) {
    let distance=Infinity,damage=0;
    for(const b of characterParts) {
      const x=player.x+b.x,z=player.z+b.z;
      const t=ray(origin,direction,[x-b.w/2,b.y-b.h/2,z-b.d/2],[x+b.w/2,b.y+b.h/2,z+b.d/2]);
      if(t<distance) {distance=t;damage=b.damage;}
    }
    return {distance,damage};
  }
  function damageAtHeight(height) {
    return height >= 1.45 ? 100 : height >= .65 ? 25 : 20;
  }
  const weapon = {interval:150,accurateSpeed:1.35,maxSpeed:5.4,movingSpread:4.5,bloomPerShot:.38,maxBloom:2.4,recoveryDelay:180,recoveryPerSecond:5};
  function movementSpread(speed) {
    const fraction=Math.max(0,Math.min(1,(speed-weapon.accurateSpeed)/(weapon.maxSpeed-weapon.accurateSpeed)));
    return weapon.movingSpread*fraction;
  }
  function recoveredBloom(bloom,elapsed) {
    return Math.max(0,bloom-Math.max(0,elapsed-weapon.recoveryDelay)*weapon.recoveryPerSecond/1000);
  }
  function spreadDirection(yaw,pitch,degrees,random=Math.random) {
    const radius=Math.sqrt(random())*Math.tan(degrees*Math.PI/180),angle=random()*Math.PI*2;
    const u=radius*Math.cos(angle),v=radius*Math.sin(angle);
    const sy=Math.sin(yaw),cy=Math.cos(yaw),sp=Math.sin(pitch),cp=Math.cos(pitch);
    const x=-sy*cp+cy*u+sy*sp*v,y=sp+cp*v,z=-cy*cp-sy*u+cy*sp*v;
    const length=Math.hypot(x,y,z);
    return [x/length,y/length,z/length];
  }
  const api={
    arena,clear,move,ray,damageAtHeight,characterParts,characterHit,weapon,movementSpread,recoveredBloom,spreadDirection
  };
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
  else root.FPS=api;
})(typeof globalThis!=='undefined'?globalThis:this);
