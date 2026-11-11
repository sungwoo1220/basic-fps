'use strict';
const assert=require('assert');
const fs=require('fs');
const os=require('os');
const net=require('net');
const path=require('path');
const crypto=require('crypto');
const Module=require('module');
const {EventEmitter}=require('events');
const {execFileSync}=require('child_process');

const root=path.join(__dirname,'..','..');
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'basic-fps-launcher-'));
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const canListen=port=>new Promise(resolve=>{
  const probe=net.createServer();
  probe.once('error',()=>resolve(false));
  probe.listen(port,'0.0.0.0',()=>probe.close(()=>resolve(true)));
});

function buildRelease(){
  const stage=path.join(tmp,'release');
  fs.cpSync(path.join(root,'game'),path.join(stage,'game'),{recursive:true,filter:file=>path.basename(file)!=='tests'});
  const three=path.join(stage,'game','public','three.module.js');
  if(!fs.existsSync(three))fs.writeFileSync(three,'// 시험용 대체 파일\n');
  fs.copyFileSync(path.join(root,'publish.cjs'),path.join(stage,'publish.cjs'));
  execFileSync(process.execPath,['publish.cjs'],{cwd:stage,env:{...process.env,RELEASE_TAG:'build-7-1'},stdio:'pipe'});
  const manifest=JSON.parse(fs.readFileSync(path.join(stage,'patch','update.json'),'utf8'));
  const bins=new Map(manifest.files.map(file=>[file.url,fs.readFileSync(path.join(stage,'patch',file.sha256+'.bin'))]));
  return {manifest,bins};
}

const windows=[];
const handlers={};
class FakeWindow extends EventEmitter{
  constructor(options){
    super();
    this.options=options;
    this.destroyed=false;
    this.sent=[];
    this.webContents=Object.assign(new EventEmitter(),{send:(channel,value)=>this.sent.push([channel,value]),setWindowOpenHandler(){}});
    windows.push(this);
  }
  isDestroyed(){return this.destroyed;}
  loadFile(){return Promise.resolve();}
  async loadURL(url){await sleep(5);this.url=url;}
  close(){this.destroyed=true;this.emit('closed');}
}
const gameWindows=()=>windows.filter(w=>w.options.width===1280&&!w.destroyed);
const lastStatus=()=>{const list=windows[0].sent.filter(item=>item[0]==='status');return list.length?list[list.length-1][1]:'';};

async function main(){
  const {manifest,bins}=buildRelease();
  const userData=path.join(tmp,'userData');
  const appDir=path.join(tmp,'app');
  fs.mkdirSync(appDir,{recursive:true});
  fs.symlinkSync(path.dirname(path.dirname(require.resolve('ws'))),path.join(appDir,'node_modules'),'junction');
  const electron={app:{getPath:()=>userData,getAppPath:()=>appDir,whenReady:()=>Promise.resolve(),on(){}},BrowserWindow:FakeWindow,ipcMain:{handle:(name,fn)=>{handlers[name]=fn;}},dialog:{}};
  const load=Module._load;
  Module._load=function(request,...rest){return request==='electron'?electron:load.call(this,request,...rest);};
  require(path.join(root,'main.cjs'));
  Module._load=load;
  await sleep(20);
  assert(handlers.update&&handlers.play,'런처가 업데이트/실행 기능을 등록하지 않았습니다.');

  let served=manifest;
  const calls=[];
  global.fetch=async url=>{
    calls.push(url);
    const data=url.endsWith('/update.json')?Buffer.from(JSON.stringify(served)):bins.get(url);
    if(!data)return {ok:false,status:404};
    return {ok:true,status:200,body:(async function*(){yield data;})()};
  };
  const pointer=()=>JSON.parse(fs.readFileSync(path.join(userData,'game','current.json'),'utf8'));
  const onlyCurrent=()=>assert.deepEqual(fs.readdirSync(path.join(userData,'game')).sort(),['current.json',pointer().folder].sort(),'현재 설치 폴더 하나만 남아야 합니다.');

  await handlers.update();
  assert.equal(calls.length,1+manifest.files.length,'처음에는 목록과 모든 파일을 받아야 합니다.');
  const first=pointer();
  for(const file of manifest.files){
    const saved=fs.readFileSync(path.join(userData,'game',first.folder,file.path));
    assert.equal(sha(saved),file.sha256,file.path+' 내용이 다릅니다.');
  }
  assert(windows[0].sent.some(item=>item[0]==='ready'&&item[1]===true),'준비 완료 신호가 없습니다.');
  onlyCurrent();
  console.log('PASS: launcher first install — 릴리스 목록 검증 통과, '+manifest.files.length+'개 파일 해시 일치.');

  calls.length=0;
  await handlers.update();
  assert.equal(calls.length,1,'바뀐 파일이 없으면 목록만 받아야 합니다.');
  assert(/변경 파일 0개/.test(lastStatus()),lastStatus());
  onlyCurrent();
  const installed=pointer();
  console.log('PASS: launcher incremental update — 변경 없음, 파일 다운로드 0개, 이전 설치 폴더 정리.');

  const broken=JSON.parse(JSON.stringify(manifest));
  broken.version='3.4.99999';
  const entry=broken.files.find(file=>file.path==='server.js');
  const bad=Buffer.from('console.log("변조된 파일")');
  entry.sha256=sha(Buffer.from('다른 내용'));
  entry.size=bad.length;
  entry.url='https://github.com/sungwoo1220/basic-fps/releases/download/build-8-1/bad.bin';
  bins.set(entry.url,bad);
  served=broken;
  calls.length=0;
  await handlers.update();
  assert.equal(calls.length,2,'바뀐 server.js 하나만 받아야 합니다.');
  assert(/업데이트 실패/.test(lastStatus()),lastStatus());
  assert.deepEqual(pointer(),installed,'실패한 업데이트가 기존 설치를 바꿨습니다.');
  onlyCurrent();
  assert.equal(windows[0].sent[windows[0].sent.length-1][1],true,'기존 설치로 계속 실행할 수 있어야 합니다.');
  served=manifest;
  console.log('PASS: launcher failed update — 변조된 파일 거부, 기존 설치 유지, 임시 폴더 삭제.');

  const address='http://example.test:3000';
  await handlers.play(null,{host:false,address:'주소 아님'});
  assert.equal(gameWindows().length,0);
  assert(/형식/.test(lastStatus()),lastStatus());

  await Promise.all([handlers.play(null,{host:false,address}),handlers.play(null,{host:false,address})]);
  assert.equal(gameWindows().length,1,'게임 실행을 두 번 눌러도 창은 하나여야 합니다.');
  assert.equal(gameWindows()[0].url,address+'/');
  gameWindows()[0].close();
  await handlers.play(null,{host:false,address});
  assert.equal(gameWindows().length,1,'게임을 닫은 뒤에는 다시 실행할 수 있어야 합니다.');
  gameWindows()[0].close();
  console.log('PASS: launcher play — 잘못된 주소 거부, 연타해도 창 1개, 종료 후 재실행 가능.');

  if(await canListen(3000)){
    await handlers.play(null,{host:true,address:''});
    assert.equal(gameWindows().length,1,'내 PC 서버 모드는 주소 칸이 비어 있어도 시작해야 합니다.');
    assert.equal(gameWindows()[0].url,'http://127.0.0.1:3000/');
    gameWindows()[0].close();
    console.log('PASS: launcher host mode — 주소 칸이 비어 있어도 내 PC 서버로 시작.');
  }else console.log('SKIP: launcher host mode — 3000 포트를 다른 프로그램이 쓰는 중입니다.');
}
main().then(()=>0,error=>{console.error('FAIL: '+(error&&error.stack||error));return 1;}).then(code=>{
  try{fs.rmSync(tmp,{recursive:true,force:true});}catch{}
  process.exit(code);
});
