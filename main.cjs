'use strict';
const {app,BrowserWindow,ipcMain,dialog}=require('electron');
const fs=require('fs/promises');
const path=require('path');
const crypto=require('crypto');
const {spawn}=require('child_process');
const repo='sungwoo1220/basic-fps';
app.commandLine.appendSwitch('disable-frame-rate-limit');
app.commandLine.appendSwitch('disable-gpu-vsync');
app.commandLine.appendSwitch('force_high_performance_gpu');
let win,server,game,busy=false,gameReady=false;
const root=()=>path.join(app.getPath('userData'),'game');
function emit(text){if(win&&!win.isDestroyed())win.webContents.send('status',text);}
async function current(){try{const value=JSON.parse(await fs.readFile(path.join(root(),'current.json'),'utf8'));if(!/^[a-zA-Z0-9._-]+$/.test(value.folder))throw Error('Invalid folder');return {...value,dir:path.join(root(),value.folder)};}catch{return null;}}
function digest(data){return crypto.createHash('sha256').update(data).digest('hex');}
async function download(url,max){
 const u=new URL(url);
 if(u.protocol!=='https:'||u.hostname!=='github.com'||!u.pathname.startsWith('/'+repo+'/releases/download/')&&!u.pathname.startsWith('/'+repo+'/releases/latest/download/'))throw Error('허용되지 않은 업데이트 주소');
 const response=await fetch(url,{signal:AbortSignal.timeout(90000),headers:{'User-Agent':'BasicFPS-Launcher'}});
 if(!response.ok)throw Error('다운로드 실패: HTTP '+response.status);
 const parts=[];let size=0;
 for await(const chunk of response.body){size+=chunk.length;if(size>max)throw Error('다운로드 크기 제한 초과');parts.push(chunk);}
 return Buffer.concat(parts);
}
async function prune(keep){
 try{for(const name of await fs.readdir(root()))if(name!==keep&&/^\d+\.\d+\.\d+-[0-9a-f]{12}$/.test(name))await fs.rm(path.join(root(),name),{recursive:true,force:true}).catch(()=>{});}catch{}
}
async function update(){
 if(busy)return;
 if(game){emit('게임을 종료한 뒤 업데이트하세요.');return;}
 busy=true;gameReady=false;
 try{
  emit('업데이트 확인 중…');
  const m=JSON.parse((await download('https://github.com/'+repo+'/releases/latest/download/update.json',262144)).toString('utf8'));
  if(m.schema!==1||!/^\d+\.\d+\.\d+$/.test(m.version)||!Array.isArray(m.files)||m.files.length>256||!m.files.length)throw Error('업데이트 목록 형식 오류');
  const seen=new Set();let total=0;
  for(const f of m.files){if(typeof f.path!=='string'||!(/^(server\.js|launcher\.html|public\/[a-zA-Z0-9._/-]+)$/).test(f.path)||f.path.split('/').some(x=>x==='..'||x==='.'||!x)||seen.has(f.path)||!(/^[a-f0-9]{64}$/).test(f.sha256)||!Number.isSafeInteger(f.size)||f.size<0||f.size>16777216)throw Error('업데이트 파일 검증 실패');seen.add(f.path);total+=f.size;}
  if(total>67108864||!seen.has('server.js')||!seen.has('launcher.html')||!seen.has('public/index.html'))throw Error('필수 파일 누락 또는 용량 초과');
  const old=await current();
  const folder=m.version+'-'+crypto.randomBytes(6).toString('hex'),stage=path.join(root(),folder);
  await fs.mkdir(stage,{recursive:true});
  try{
   let changed=0;
   for(let n=0;n<m.files.length;n++){
    const f=m.files[n];let bytes=null;
    if(old){try{const candidate=await fs.readFile(path.join(old.dir,f.path));if(candidate.length===f.size&&digest(candidate)===f.sha256)bytes=candidate;}catch{}}
    if(!bytes){emit(`다운로드 ${n+1}/${m.files.length}: ${f.path}`);bytes=await download(f.url,Math.max(1,f.size));changed++;}
    if(bytes.length!==f.size||digest(bytes)!==f.sha256)throw Error('파일 무결성 오류: '+f.path);
    const target=path.join(stage,f.path);await fs.mkdir(path.dirname(target),{recursive:true});await fs.writeFile(target,bytes);
   }
   const pointer=path.join(root(),'current.json');
   await fs.writeFile(pointer+'.tmp',JSON.stringify({folder,version:m.version}));
   await fs.rename(pointer+'.tmp',pointer);
   await prune(folder);
   gameReady=true;emit(`준비 완료 v${m.version} · 변경 파일 ${changed}개 다운로드`);
   win.webContents.send('ready',true);
  }catch(e){await fs.rm(stage,{recursive:true,force:true});throw e;}
 }catch(e){emit('업데이트 실패: '+e.message+' · 기존 설치가 있으면 실행할 수 있습니다.');gameReady=!!(await current());win.webContents.send('ready',gameReady);}
 finally{busy=false;}
}
async function play(host,address){
 if(busy||game||!gameReady)return;
 busy=true;
 try{await start(host,address);}finally{busy=false;}
}
async function start(host,address){
 const installed=await current();if(!installed)return;
 let target;
 if(!host)try{target=new URL(address);if(!['http:','https:'].includes(target.protocol)||target.username||target.password)throw Error();}catch{emit('http://서버IP:3000 형식의 주소를 입력하세요.');return;}
 if(host){
  target=new URL('http://127.0.0.1:3000');
  server=spawn(process.execPath,[path.join(installed.dir,'server.js')],{windowsHide:true,env:{...process.env,ELECTRON_RUN_AS_NODE:'1',NODE_PATH:path.join(app.getAppPath(),'node_modules')},stdio:['ignore','pipe','pipe']});
  let errors='';server.stderr.on('data',data=>{errors=(errors+data).slice(-3000);});
  server.on('error',e=>emit('서버 시작 실패: '+e.message));
  const started=await new Promise(resolve=>{const timer=setTimeout(()=>resolve(false),10000);server.stdout.once('data',()=>{clearTimeout(timer);resolve(true);});server.once('exit',()=>{clearTimeout(timer);resolve(false);});});
  if(!started){emit('서버를 시작하지 못했습니다. 기존 서버를 종료하세요. '+errors);if(server)server.kill();server=null;return;}
 }
 game=new BrowserWindow({width:1280,height:800,autoHideMenuBar:true,backgroundColor:'#17202b',webPreferences:{nodeIntegration:false,contextIsolation:true,sandbox:true,backgroundThrottling:false}});
 const g=game;
 g.webContents.setWindowOpenHandler(()=>({action:'deny'}));
 g.webContents.on('will-navigate',(event,url)=>{try{if(new URL(url).origin!==target.origin)event.preventDefault();}catch{event.preventDefault();}});
 g.on('closed',()=>{game=null;if(server){server.kill();server=null;}emit('게임 종료. 업데이트를 확인하거나 다시 실행하세요.');});
 try{await g.loadURL(target.href);emit('게임 실행 중');}catch(e){emit('접속 실패: 서버와 방화벽을 확인하세요.');g.close();}
}
app.whenReady().then(()=>{
 win=new BrowserWindow({width:760,height:600,resizable:false,autoHideMenuBar:true,backgroundColor:'#111c2c',webPreferences:{preload:path.join(__dirname,'preload.cjs'),nodeIntegration:false,contextIsolation:true,sandbox:true}});
 win.webContents.setWindowOpenHandler(()=>({action:'deny'}));
 win.loadFile(path.join(__dirname,'index.html'));
 win.webContents.once('did-finish-load',update);
 ipcMain.handle('update',()=>update());
 ipcMain.handle('play',(event,args)=>play(!!args.host,String(args.address||'')));
});
app.on('window-all-closed',()=>app.quit());
app.on('before-quit',()=>{if(server)server.kill();});
