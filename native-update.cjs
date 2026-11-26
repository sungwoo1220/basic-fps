'use strict';
const fs=require('fs/promises'),rawFs=require('fs'),path=require('path'),crypto=require('crypto');
const REPO='sungwoo1220/basic-fps';
const MAX_CHUNK=8*1024*1024,MAX_TOTAL=4*1024*1024*1024;
// A profile says which release channel a NativeClient follows. UNREAL keeps the original names and Korean texts exactly:
// tests match them. DX11 is the self-contained Windows trial the launcher installs and starts.
const UNREAL={
  kind:'unreal',tagPrefix:'ue-test-',tagPattern:/^ue-test-\d+-\d+$/,manifestName:'unreal-update.json',folderPrefix:'ue',
  executablePattern:/(?:^|\/)BasicFPS\/Binaries\/Win64\/BasicFPS(?:-Win64-(?:Development|Shipping))?\.exe$/,
  text:{
    badManifest:'언리얼 업데이트 목록 형식이 올바르지 않아요.',
    none:'아직 다운로드할 언리얼 시험판이 없어요. Windows 빌드 PC 연결과 빌드 성공이 먼저 필요해요.',
    busy:'이미 언리얼 게임을 업데이트하고 있어요.',
    checking:'언리얼 시험판을 확인하고 있어요…',
    upToDate:'이미 최신 언리얼 시험판이에요.',
    downloading:(percent,done,total)=>`언리얼 다운로드 ${percent}% · ${done} / ${total}MB`,
    ready:version=>`언리얼 시험판 ${version} 준비 완료 · Windows 실기 확인이 필요해요.`,
    notInstalled:'언리얼 시험판을 먼저 다운로드해 주세요.',
    changed:'설치된 게임 파일이 달라졌어요. 언리얼 시험판을 다시 다운로드해 주세요.',
  },
};
const DX11={
  kind:'dx11',tagPrefix:'dx11-test-',tagPattern:/^dx11-test-\d+-\d+$/,manifestName:'dx11-update.json',folderPrefix:'dx11',
  executablePattern:/^BasicFPS-DX11\.exe$/,
  text:{
    badManifest:'게임 업데이트 목록 형식이 올바르지 않아요.',
    none:'아직 다운로드할 게임이 없어요. 잠시 뒤에 다시 시도해 주세요.',
    busy:'이미 게임을 업데이트하고 있어요.',
    checking:'게임 업데이트를 확인하고 있어요…',
    upToDate:'이미 최신 게임이에요.',
    downloading:(percent,done,total)=>`게임 다운로드 ${percent}% · ${done} / ${total}MB`,
    ready:version=>`게임 ${version} 준비 완료`,
    notInstalled:'게임이 아직 설치되지 않았어요. 업데이트를 먼저 해 주세요.',
    changed:'설치된 게임 파일이 달라졌어요. 게임을 다시 내려받아 주세요.',
  },
};
const folderPattern=profile=>new RegExp(`^${profile.folderPrefix}-[0-9]+-[0-9]+-[a-f0-9]{12}$`);
function safePath(value){
  return typeof value==='string'&&value.length<=240&&/^[a-zA-Z0-9._ /-]+$/.test(value)&&value.split('/').every(part=>
    part&&part!=='.'&&part!=='..'&&!/[. ]$/.test(part)&&!/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part));
}
function validateManifest(m,profile=UNREAL){
  if(!m||m.schema!==2||m.kind!==profile.kind||m.platform!=='win64'||typeof m.build!=='string'||!profile.tagPattern.test(m.build)||
    !/^\d+\.\d+\.\d+$/.test(m.version)||!Array.isArray(m.files)||!m.files.length||m.files.length>4096||
    !safePath(m.executable)||!profile.executablePattern.test(m.executable))throw Error(profile.text.badManifest);
  let total=0;const names=new Set();
  for(const file of m.files){
    if(!safePath(file.path)||names.has(file.path.toLowerCase())||!Number.isSafeInteger(file.size)||file.size<0||file.size>2*1024*1024*1024||
      !/^[a-f0-9]{64}$/.test(file.sha256)||!Array.isArray(file.chunks)||file.chunks.length>256)throw Error('안전하지 않은 게임 파일 목록이에요.');
    if(file.path.toLowerCase()==='manifest.json')throw Error('설치 정보와 겹치는 파일 이름이에요.');
    names.add(file.path.toLowerCase());let size=0;
    for(const chunk of file.chunks){
      if(!Number.isSafeInteger(chunk.size)||chunk.size<=0||chunk.size>MAX_CHUNK||!/^[a-f0-9]{64}$/.test(chunk.sha256))throw Error('파일 조각 정보가 올바르지 않아요.');
      const expected=`https://github.com/${REPO}/releases/download/${m.build}/${chunk.sha256}.bin`;
      if(chunk.url!==expected)throw Error('허용되지 않은 다운로드 주소예요.');size+=chunk.size;
    }
    if(size!==file.size)throw Error('게임 파일 크기 정보가 맞지 않아요.');
    total+=file.size;if(total>MAX_TOTAL)throw Error('게임 전체 크기가 허용 범위를 넘었어요.');
  }
  for(const name of names){const parts=name.split('/');parts.pop();while(parts.length){if(names.has(parts.join('/')))throw Error('파일과 폴더 이름이 겹쳐요.');parts.pop();}}
  if(!m.files.some(file=>file.path===m.executable))throw Error('게임 실행 파일이 없어요.');
  return m;
}
async function hashFile(file){const hash=crypto.createHash('sha256');for await(const data of rawFs.createReadStream(file))hash.update(data);return hash.digest('hex');}
class NativeClient {
  constructor({root,emit=()=>{},fetch:fetcher=(...args)=>fetch(...args),progress=()=>{},profile=UNREAL}){
    this.root=root;this.emit=emit;this.fetch=fetcher;this.progress=progress;this.profile=profile;this.busy=false;
    const pattern=folderPattern(profile);this.folderOK=value=>typeof value==='string'&&pattern.test(value);
  }
  async download(url,max){
    const response=await this.fetch(url,{signal:AbortSignal.timeout(120000),headers:{'User-Agent':'BasicFPS-Launcher',Accept:url.startsWith('https://api.github.com/')?'application/vnd.github+json':'application/octet-stream'}});
    if(!response.ok)throw Error('다운로드에 실패했어요. HTTP '+response.status);
    const parts=[];let size=0;for await(const part of response.body){size+=part.length;if(size>max)throw Error('다운로드 크기가 예상보다 커요.');parts.push(Buffer.from(part));}
    return Buffer.concat(parts);
  }
  async latest(){
    const {profile}=this;
    // 100 per page and three pages: every push to main adds a "build-*" release, so a short list could hide the trial releases.
    for(let page=1;page<=3;page++){
      const bytes=await this.download(`https://api.github.com/repos/${REPO}/releases?per_page=100&page=${page}`,8*1024*1024);
      const releases=JSON.parse(bytes.toString('utf8'));if(!Array.isArray(releases))throw Error('릴리스 목록을 읽지 못했어요.');
      for(const release of releases){
        if(!release.prerelease||release.draft||!profile.tagPattern.test(release.tag_name)||!Array.isArray(release.assets))continue;
        const expected=`https://github.com/${REPO}/releases/download/${release.tag_name}/${profile.manifestName}`;
        const asset=release.assets.find(item=>item.name===profile.manifestName&&item.browser_download_url===expected);if(!asset)continue;
        const manifest=validateManifest(JSON.parse((await this.download(expected,4*1024*1024)).toString('utf8')),profile);
        if(manifest.build!==release.tag_name)throw Error('릴리스와 게임 목록이 일치하지 않아요.');return manifest;
      }
      if(releases.length<100)break;
    }
    throw Error(profile.text.none);
  }
  async current(){
    try{
      const pointer=JSON.parse(await fs.readFile(path.join(this.root,'current.json'),'utf8'));if(!this.folderOK(pointer.folder))return null;
      const dir=path.join(this.root,pointer.folder),manifest=validateManifest(JSON.parse(await fs.readFile(path.join(dir,'manifest.json'),'utf8')),this.profile);
      return {...pointer,dir,manifest,executable:path.join(dir,...manifest.executable.split('/'))};
    }catch{return null;}
  }
  async writePointer(pointer){
    await fs.mkdir(this.root,{recursive:true});const file=path.join(this.root,'current.json');
    await fs.writeFile(file+'.tmp',JSON.stringify(pointer));await fs.rename(file+'.tmp',file);
  }
  async install(supplied){
    const {profile}=this,{text}=profile;
    if(this.busy)throw Error(text.busy);this.busy=true;let stage;
    try{
      this.emit(text.checking);this.progress(0);const manifest=validateManifest(supplied||await this.latest(),profile);const old=await this.current();
      if(old&&JSON.stringify(old.manifest)===JSON.stringify(manifest)){
        let valid=true;
        for(const file of manifest.files)if(await hashFile(path.join(old.dir,...file.path.split('/'))).catch(()=>null)!==file.sha256){valid=false;break;}
        if(valid){this.emit(text.upToDate);this.progress(1);return old;}
      }
      const [major,attempt]=manifest.build.slice(profile.tagPrefix.length).split('-');
      const folder=`${profile.folderPrefix}-${major}-${attempt}-${crypto.randomBytes(6).toString('hex')}`;stage=path.join(this.root,folder);await fs.mkdir(stage,{recursive:true});
      let done=0;const total=manifest.files.reduce((sum,file)=>sum+file.size,0);
      for(const file of manifest.files){
        const target=path.join(stage,...file.path.split('/'));await fs.mkdir(path.dirname(target),{recursive:true});
        const previous=old&&old.manifest.files.find(item=>item.path===file.path&&item.sha256===file.sha256);
        if(previous&&await hashFile(path.join(old.dir,...file.path.split('/'))).catch(()=>null)===file.sha256){
          await fs.copyFile(path.join(old.dir,...file.path.split('/')),target);
        }else{
          const handle=await fs.open(target,'wx');const hash=crypto.createHash('sha256');
          try{
            for(const chunk of file.chunks){
              const bytes=await this.download(chunk.url,chunk.size);
              if(bytes.length!==chunk.size||crypto.createHash('sha256').update(bytes).digest('hex')!==chunk.sha256)throw Error('다운로드한 게임 조각이 손상됐어요.');
              hash.update(bytes);let offset=0;
              while(offset<bytes.length){const result=await handle.write(bytes,offset,bytes.length-offset);if(!result.bytesWritten)throw Error('게임 파일을 기록하지 못했어요.');offset+=result.bytesWritten;}
            }
          }finally{await handle.close();}
          if(hash.digest('hex')!==file.sha256)throw Error('게임 파일 검증에 실패했어요.');
        }
        done+=file.size;this.progress(done/Math.max(1,total));
        this.emit(text.downloading(Math.round(done*100/Math.max(1,total)),Math.round(done/1048576),Math.round(total/1048576)));
      }
      await fs.writeFile(path.join(stage,'manifest.json'),JSON.stringify(manifest));
      await this.writePointer({folder,previous:old?.folder||null});stage=null;
      // Keep one known previous install; only remove directories created by this updater.
      const keep=new Set([folder,old?.folder]);
      for(const entry of await fs.readdir(this.root))if(this.folderOK(entry)&&!keep.has(entry))await fs.rm(path.join(this.root,entry),{recursive:true,force:true}).catch(()=>{});
      this.progress(1);this.emit(text.ready(manifest.version));return await this.current();
    }finally{if(stage)await fs.rm(stage,{recursive:true,force:true}).catch(()=>{});this.busy=false;}
  }
  async verifyLaunch(){
    const installed=await this.current();if(!installed)throw Error(this.profile.text.notInstalled);
    for(const file of installed.manifest.files)if(await hashFile(path.join(installed.dir,...file.path.split('/'))).catch(()=>null)!==file.sha256)throw Error(this.profile.text.changed);
    return installed;
  }
  async rollback(){
    const current=await this.current();if(!current||!this.folderOK(current.previous))return false;
    const previousDir=path.join(this.root,current.previous);
    try{
      const manifest=validateManifest(JSON.parse(await fs.readFile(path.join(previousDir,'manifest.json'),'utf8')),this.profile);
      for(const file of manifest.files)if(await hashFile(path.join(previousDir,...file.path.split('/')))!==file.sha256)return false;
    }catch{return false;}
    await this.writePointer({folder:current.previous,previous:current.folder});return true;
  }
}
module.exports={NativeClient,validateManifest,safePath,MAX_CHUNK,UNREAL,DX11};
