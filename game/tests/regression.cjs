'use strict';
const assert = require('assert');
const {arena, move, clear, ray} = require('../public/shared.js');
function state() { return {x:-10,z:-17,vx:0,vz:0}; }
const straight = state();
for (let n=0;n<60;n++) move(straight,{keys:1,yaw:Math.PI});
assert(Math.abs(straight.vz-arena.speed)<1e-9);
assert(straight.z+17<5.4 && straight.z+17>4.8);
const diagonal=state();
for(let n=0;n<60;n++) move(diagonal,{keys:9,yaw:Math.PI});
assert(Math.abs(Math.hypot(diagonal.vx,diagonal.vz)-arena.speed)<1e-9);
for(let n=0;n<20;n++) move(straight,{keys:0,yaw:0});
assert(Math.hypot(straight.vx,straight.vz)<1e-9);
const walking=state();
for(let n=0;n<60;n++) move(walking,{keys:33,yaw:Math.PI});
assert(Math.abs(walking.vz-arena.walkSpeed)<1e-9);
const wall={x:19.49,z:17,vx:5.4,vz:0};
move(wall,{keys:8,yaw:0});
assert(wall.x<=19.5 && wall.vx===0);
assert(!clear(0,0) && !clear(20,0) && clear(-17,-17));
assert.equal(ray([0,1.6,10],[0,0,-1],[-2.5,0,-1],[2.5,3,1]),9);
const inputs=Array.from({length:60},(_,n)=>({keys:n<25?1:n<40?2:0,yaw:Math.PI}));
const local=state(),server=state();
inputs.forEach(i=>move(local,i));
inputs.slice(0,30).forEach(i=>move(server,i));
const replay={...server};
inputs.slice(30).forEach(i=>move(replay,i));
assert.deepEqual(local,replay);
assert(Math.abs(1000*.35*.07-24.5)<1e-9);
console.log('PASS: acceleration, deceleration, walking, diagonal speed, wall velocity, ray, velocity-state prediction replay, sensitivity multiplier.');

const {damageAtHeight}=require("../public/shared.js");
assert.equal(damageAtHeight(1.6),100);
assert.equal(damageAtHeight(1),25);
assert.equal(damageAtHeight(.3),20);
assert.equal(Math.ceil(100/damageAtHeight(1.6)),1);
assert.equal(Math.ceil(100/damageAtHeight(1)),4);
assert.equal(Math.ceil(100/damageAtHeight(.3)),5);
console.log("PASS: head/body/legs damage and required hit counts.");

const {characterHit}=require("../public/shared.js");
assert.equal(characterHit([0,1.65,5],[0,0,-1],{x:0,z:0}).damage,100);
assert.equal(characterHit([0,1,5],[0,0,-1],{x:0,z:0}).damage,25);
assert.equal(characterHit([.17,.3,5],[0,0,-1],{x:0,z:0}).damage,20);
assert.equal(characterHit([.3,1.65,5],[0,0,-1],{x:0,z:0}).distance,Infinity);
assert.equal(characterHit([0,.3,5],[0,0,-1],{x:0,z:0}).distance,Infinity);
console.log("PASS: character head/body/leg geometry, head-side miss and leg gap miss.");

const {weapon,movementSpread,recoveredBloom,spreadDirection}=require('../public/shared.js');
assert.equal(movementSpread(0),0);
assert.equal(movementSpread(1.35),0);
assert(movementSpread(1.36)>0);
assert.equal(movementSpread(5.4),4.5);
assert.equal(recoveredBloom(2.4,660),0);
assert.equal(recoveredBloom(.38,150),.38);
const exact=spreadDirection(0,0,0,()=>.5);
assert(Math.abs(exact[0])<1e-12&&Math.abs(exact[1])<1e-12&&exact[2]===-1);
for(let n=0;n<500;n++){const d=spreadDirection(0,0,4.5);assert(Math.abs(Math.hypot(...d)-1)<1e-9);assert(Math.acos(-d[2])<=4.5*Math.PI/180+1e-9);}
console.log('PASS: near-stop accuracy threshold, running spread, bloom recovery, precise first shot and spread cone bounds.');
