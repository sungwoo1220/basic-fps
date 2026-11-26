'use strict';
// Origin guard on the privileged "bots" command, and bots keeping their identity when the count changes.
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
  const watchdog=setTimeout(()=>{console.error('FAIL: hardening test timed out\n'+log);child.kill();process.exit(1);},45000);
  const sockets=[];
  try{
    await new Promise((resolve,reject)=>{
      child.stdout.once('data',resolve);
      child.once('exit',()=>reject(new Error('server did not start\n'+log)));
    });
    const open=async headers=>{
      const ws=new WebSocket('ws://127.0.0.1:'+port,{headers});
      const client={ws,state:null};
      ws.on('message',raw=>{const m=JSON.parse(raw);if(m.type==='state')client.state=m;});
      await new Promise((resolve,reject)=>{ws.once('open',resolve);ws.once('error',reject);});
      sockets.push(ws);
      return client;
    };
    const until=async(test,label)=>{
      const end=Date.now()+8000;
      while(Date.now()<end){if(test())return;await sleep(20);}
      throw new Error('timed out waiting for: '+label);
    };
    const bots=c=>c.state.players.filter(p=>p.id.startsWith('bot-')).map(p=>p.id).sort();

    const host=await open({});
    await until(()=>host.state,'first snapshot');
    host.ws.send(JSON.stringify({type:'bots',count:3}));
    await until(()=>bots(host).length===3,'3 bots');

    // A page from another origin on the same machine must not be able to change the bots.
    const evil=await open({Origin:'http://evil.example'});
    evil.ws.send(JSON.stringify({type:'bots',count:0}));
    await sleep(400);
    if(bots(host).length!==3)throw new Error('a foreign Origin changed the bots: '+bots(host));

    // The room's own page (same host and port) still can.
    const page=await open({Origin:'http://127.0.0.1:'+port});
    page.ws.send(JSON.stringify({type:'bots',count:2}));
    await until(()=>bots(host).join()==='bot-0,bot-1','own page reduces bots to 2');

    // Raising the count again keeps the surviving bots and only adds the missing one.
    host.ws.send(JSON.stringify({type:'bots',count:3}));
    await until(()=>bots(host).join()==='bot-0,bot-1,bot-2','bots back to 3');
    console.log('PASS: bots command ignores foreign Origin, accepts the room page, and keeps surviving bots.');
  }finally{
    clearTimeout(watchdog);
    for(const ws of sockets)ws.terminate();
    child.kill();
  }
}
main().then(()=>process.exit(0),e=>{console.error('FAIL:',e.message);process.exit(1);});
