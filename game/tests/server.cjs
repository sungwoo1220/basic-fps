'use strict';
const net=require('net');
const path=require('path');
const {spawn}=require('child_process');
const WebSocket=require('ws');

const freePort=()=>new Promise((resolve,reject)=>{
  const probe=net.createServer();
  probe.on('error',reject);
  probe.listen(0,'127.0.0.1',()=>{const {port}=probe.address();probe.close(()=>resolve(port));});
});
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));

async function main(){
  const port=await freePort();
  const child=spawn(process.execPath,[path.join(__dirname,'..','server.js')],{env:{...process.env,PORT:String(port)},stdio:['ignore','pipe','pipe']});
  let log='';
  child.stdout.on('data',d=>{log+=d;});
  child.stderr.on('data',d=>{log+=d;});
  let ws;
  const watchdog=setTimeout(()=>{console.error('FAIL: 서버 시험이 제한 시간을 넘었습니다.\n'+log);child.kill();process.exit(1);},60000);
  try{
    await new Promise((resolve,reject)=>{
      child.stdout.once('data',resolve);
      child.once('exit',()=>reject(new Error('서버가 시작되지 않았습니다.\n'+log)));
    });
    ws=new WebSocket('ws://127.0.0.1:'+port);
    let id=null,state=null,me=null;
    ws.on('message',raw=>{
      const m=JSON.parse(raw);
      if(m.type==='welcome')id=m.id;
      if(m.type==='state'&&id){state=m;me=m.players.find(p=>p.id===id)||me;}
    });
    await new Promise((resolve,reject)=>{ws.once('open',resolve);ws.once('error',reject);});
    async function until(test,label,limit=12000){
      const end=Date.now()+limit;
      while(Date.now()<end){if(me&&test())return;await sleep(15);}
      throw new Error('기다리던 상태가 오지 않았습니다: '+label+' / 마지막 상태 '+JSON.stringify(me));
    }

    ws.send(JSON.stringify({type:'bots',count:0}));
    await until(()=>state.players.length===1,'연습 봇 제거');
    if(me.ammo!==12||me.reloadRemaining!==0)throw new Error('입장 직후에는 12발이어야 합니다: '+JSON.stringify(me));

    let seq=0,fire=false;
    const pump=setInterval(()=>{
      if(!me||ws.readyState!==WebSocket.OPEN)return;
      ws.send(JSON.stringify({type:'inputs',items:[{seq:++seq,keys:fire?16:0,yaw:0,pitch:0,epoch:me.epoch,viewTime:state.serverTime}]}));
    },30);

    try{
      ws.send(JSON.stringify({type:'reload'}));
      await sleep(250);
      if(me.reloadRemaining!==0)throw new Error('탄창이 가득하면 재장전이 시작되면 안 됩니다.');

      fire=true;
      await until(()=>me.ammo<12,'사격하면 탄이 줄어듦');
      await until(()=>me.reloadRemaining>0,'탄이 떨어지면 자동 재장전 시작');
      if(me.ammo!==0)throw new Error('자동 재장전은 탄이 0일 때 시작해야 합니다: '+me.ammo);
      if(me.reloadRemaining>2300.01)throw new Error('재장전 시간이 2.3초를 넘습니다: '+me.reloadRemaining);
      fire=false;
      await until(()=>me.ammo===12&&me.reloadRemaining===0,'재장전이 끝나면 12발');

      fire=true;
      await until(()=>me.ammo<12,'다시 사격');
      fire=false;
      await sleep(100);
      const left=me.ammo;
      ws.send(JSON.stringify({type:'reload'}));
      await until(()=>me.reloadRemaining>0,'R 키 재장전 시작');
      await until(()=>me.ammo===12&&me.reloadRemaining===0,'수동 재장전이 끝나면 12발');
      console.log('PASS: server magazine — 12발 시작, 사격 시 감소, 탄 소진 시 자동 재장전, 수동 재장전, 가득 찼을 때 재장전 무시 (수동 재장전 직전 '+left+'발).');
    }finally{
      clearInterval(pump);
    }
  }finally{
    clearTimeout(watchdog);
    if(ws)ws.terminate();
    child.kill();
  }
}
main().then(()=>process.exit(0),error=>{console.error('FAIL: '+error.message);process.exit(1);});
