'use strict';
// Electron and Windows process launching are mocked. This is not an actual Unreal launch.
const assert=require('assert/strict'),fs=require('fs/promises'),os=require('os'),path=require('path'),Module=require('module');
const {EventEmitter}=require('events');
const {NativeClient,DX11}=require('../../native-update.cjs');
const {pack}=require('../../scripts/publish-unreal.cjs');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function main(){
  const tmp=await fs.mkdtemp(path.join(os.tmpdir(),'basic-fps-native-launcher-'));
  const platform=Object.getOwnPropertyDescriptor(process,'platform');
  const load=Module._load;
  try{
    const source=path.join(tmp,'package'),output=path.join(tmp,'release'),userData=path.join(tmp,'userdata');
    await fs.mkdir(path.join(source,'Windows/BasicFPS/Binaries/Win64'),{recursive:true});
    await fs.writeFile(path.join(source,'Windows/BasicFPS/Binaries/Win64/BasicFPS.exe'),'native-test');
    const manifest=pack(source,output,'ue-test-11-1');
    const client=new NativeClient({root:path.join(userData,'unreal'),fetch:async url=>({ok:true,body:(async function*(){yield await fs.readFile(path.join(output,new URL(url).pathname.split('/').pop()));})()})});
    await client.install(manifest);
    const handlers={},windows=[],children=[];
    class Window extends EventEmitter{
      constructor(options){super();this.options=options;this.sent=[];this.webContents=Object.assign(new EventEmitter(),{send:(channel,value)=>this.sent.push([channel,value]),setWindowOpenHandler(){}});windows.push(this);}
      isDestroyed(){return false;}
      loadFile(){return Promise.resolve();}
    }
    const fakeElectron={app:{getPath:()=>userData,commandLine:{appendSwitch(){}},whenReady:()=>Promise.resolve(),on(){}},BrowserWindow:Window,ipcMain:{handle:(name,fn)=>{handlers[name]=fn;}}};
    Module._load=function(request,...args){
      if(request==='electron')return fakeElectron;
      if(request==='child_process')return {spawn:(file,args,options)=>{const child=new EventEmitter();Object.assign(child,{file,args,options,kill(){this.killed=true;}});children.push(child);return child;}};
      return load.call(this,request,...args);
    };
    require('../../main.cjs');Module._load=load;
    Object.defineProperty(process,'platform',{value:'win32'});
    await sleep(20);
    const status=()=>windows[0].sent.filter(([channel])=>channel==='status').at(-1)?.[1]||'';
    await handlers.play(null,{client:'unreal',host:false,address:'http://server.test:3000/" -ExecCmds=quit'});
    assert.equal(children.length,0,'명령 인수를 섞은 주소를 거부해야 해요.');
    const options={client:'unreal',host:false,address:'http://server.test:3000'};
    await Promise.all([handlers.play(null,options),handlers.play(null,options)]);
    assert.equal(children.length,1,'연타해도 새 게임은 하나만 실행해야 해요.');
    assert(children[0].args.includes('-d3d11'));
    assert(children[0].args.includes('-SaveToUserDir'));
    assert.equal(children[0].args[0],'-ServerUrl=http://server.test:3000/');
    assert.equal(children[0].options.shell,false);
    assert.equal(windows.length,1,'언리얼 실행은 브라우저 창을 추가하지 않아야 해요.');
    await handlers['native-update']();assert.match(status(),/게임이나 다른 업데이트/);
    children[0].emit('exit',0);await sleep(30);
    await handlers.play(null,options);assert.equal(children.length,2);
    children[1].emit('exit',0);await sleep(30);
    const current=await client.current();await fs.writeFile(current.executable,'tampered');
    await handlers.play(null,options);assert.equal(children.length,2);assert.match(status(),/달라졌어요/);
    await fs.writeFile(current.executable,'native-test');
    const next=structuredClone(manifest);next.build='ue-test-12-1';next.version='0.1.1201';
    for(const file of next.files)for(const chunk of file.chunks)chunk.url=chunk.url.replace(manifest.build,next.build);
    await client.install(next);
    await handlers.play(null,options);assert.equal(children.length,3);
    children[2].emit('error',Error('ENOENT'));children[2].emit('exit',1);
    for(let n=0;n<200&&((await client.current()).folder!==current.folder||!/이전 시험판으로 복구/.test(status()));n++)await sleep(10);
    assert.equal((await client.current()).folder,current.folder,'실행 오류가 나면 검증된 이전 파일로 복구해야 해요.');
    assert.match(status(),/이전 시험판으로 복구/);
    console.log('PASS: native launcher (mocked) — DX11 arguments, stable user settings location, double-click guard, running-update guard, relaunch, integrity refusal.');

    // The DX11 game is the main game: no browser game installed, offline start, plain launch without arguments.
    const {pack:packDx11}=require('../../scripts/publish-dx11.cjs');
    const said=pattern=>windows[0].sent.some(([channel,text])=>channel==='status'&&pattern.test(text));
    const dxOutputs=[];
    const dxPack=async(tag,exe)=>{
      const dxSource=path.join(tmp,'dx-source-'+tag),dxOutput=path.join(tmp,'dx-release-'+tag);
      await fs.mkdir(dxSource,{recursive:true});await fs.writeFile(path.join(dxSource,'BasicFPS-DX11.exe'),exe);
      dxOutputs.push(dxOutput);return packDx11(dxSource,dxOutput,tag);
    };
    const dxFetch=async url=>{
      const name=new URL(url).pathname.split('/').pop();
      for(const dir of dxOutputs)try{const data=await fs.readFile(path.join(dir,name));return {ok:true,body:(async function*(){yield data;})()};}catch{}
      throw Error('missing '+name);
    };
    const dxClient=new NativeClient({root:path.join(userData,'dx11'),profile:DX11,fetch:dxFetch});
    const beforeDx=children.length;
    await handlers.play(null,{client:'dx11'});
    assert.equal(children.length,beforeDx,'설치 전에는 실행하지 않고 안내만 해야 해요.');assert.match(status(),/설치되지 않았어요/);
    await dxClient.install(await dxPack('dx11-test-5-1','dx-exe-1'));

    // Opening the launcher without internet: DX11 is checked first, the browser game second, and the installed game stays playable.
    const realFetch=globalThis.fetch;globalThis.fetch=async()=>{throw Error('offline');};
    windows[0].webContents.emit('did-finish-load');
    for(let n=0;n<300&&!said(/^업데이트 실패: .*기존 설치/);n++)await sleep(10);
    globalThis.fetch=realFetch;
    const texts=windows[0].sent.filter(([channel])=>channel==='status').map(([,text])=>text);
    const dxFailed=texts.findIndex(text=>/^게임 업데이트 실패: .*설치된 게임은 실행할 수 있어요/.test(text)),browserFailed=texts.findIndex(text=>/^업데이트 실패: /.test(text));
    assert.ok(dxFailed>=0&&browserFailed>dxFailed,'DX11 확인이 먼저, 기존 브라우저 게임이 그다음이어야 해요.');
    assert.deepEqual(windows[0].sent.filter(([channel])=>channel==='dx11-state').at(-1)[1],{installed:true,version:'0.2.501',busy:false});

    await Promise.all([handlers.play(null,{client:'dx11'}),handlers.play(null,{client:'dx11'})]);
    assert.equal(children.length,beforeDx+1,'오프라인이어도 설치돼 있으면 실행하고, 연타해도 하나만 실행해야 해요.');
    const dxChild=children.at(-1);
    assert.deepEqual(dxChild.args,[]);assert.equal(dxChild.options.shell,false);assert.equal(path.basename(dxChild.file),'BasicFPS-DX11.exe');
    assert.equal(windows.length,1,'DX11 실행은 브라우저 창을 추가하지 않아야 해요.');
    await handlers['dx11-update']();assert.match(status(),/게임을 종료한 뒤/);
    dxChild.emit('exit',0);await sleep(30);assert.match(status(),/게임을 종료했어요/);

    const dxCurrent=await dxClient.current();await fs.writeFile(dxCurrent.executable,'tampered');
    await handlers.play(null,{client:'dx11'});
    assert.equal(children.length,beforeDx+1,'변조된 실행 파일은 실행하지 않아야 해요.');assert.match(status(),/달라졌어요/);
    await fs.writeFile(dxCurrent.executable,'dx-exe-1');

    await dxClient.install(await dxPack('dx11-test-5-2','dx-exe-2'));
    await handlers.play(null,{client:'dx11'});
    const crashing=children.at(-1);assert.equal(children.length,beforeDx+2);
    crashing.emit('error',Error('ENOENT'));crashing.emit('exit',1);
    for(let n=0;n<200&&((await dxClient.current()).folder!==dxCurrent.folder||!said(/이전 버전으로 복구/));n++)await sleep(10);
    assert.equal((await dxClient.current()).folder,dxCurrent.folder,'시작 직후 실패하면 검증된 이전 버전으로 복구해야 해요.');
    assert.match(status(),/이전 버전으로 복구/);
    console.log('PASS: launcher DX11 (mocked) — plain launch without arguments or browser game, DX11-first offline start, double-click and running-update guards, tamper refusal, rollback after a failed start.');
  }finally{Module._load=load;Object.defineProperty(process,'platform',platform);await fs.rm(tmp,{recursive:true,force:true});}
}
main().catch(error=>{console.error('FAIL: '+error.stack);process.exitCode=1;});
