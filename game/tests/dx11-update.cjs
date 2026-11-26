'use strict';
// DX11 game updates: profile validation, release discovery and installation (network and Electron are mocked).
const assert=require('assert/strict'),crypto=require('crypto'),fs=require('fs/promises'),os=require('os'),path=require('path');
const {NativeClient,validateManifest,DX11,UNREAL}=require('../../native-update.cjs');
const base='https://github.com/sungwoo1220/basic-fps/releases/download/';
const sha=data=>crypto.createHash('sha256').update(data).digest('hex');
function sample(build='dx11-test-13-1',version='0.2.1301',data=Buffer.from('hello')){
  const hash=sha(data);
  return {schema:2,kind:'dx11',platform:'win64',version,build,executable:'BasicFPS-DX11.exe',
    files:[{path:'BasicFPS-DX11.exe',size:data.length,sha256:hash,chunks:[{size:data.length,sha256:hash,url:`${base}${build}/${hash}.bin`}]}]};
}
const reply=data=>({ok:true,body:(async function*(){yield Buffer.isBuffer(data)?data:Buffer.from(JSON.stringify(data));})()});
async function main(){
  const tmp=await fs.mkdtemp(path.join(os.tmpdir(),'basic-fps-dx11-update-'));
  try{
    // The DX11 profile accepts its own manifest and rejects Unreal ones, and the default profile stays Unreal.
    assert.ok(validateManifest(sample(),DX11));
    assert.throws(()=>validateManifest(sample()),'기본 프로필은 언리얼이라 DX11 목록을 거부해야 해요.');
    assert.throws(()=>validateManifest({...sample(),kind:'unreal'},DX11));
    assert.throws(()=>validateManifest(sample('ue-test-1-1'),DX11));
    const nested=sample();nested.executable=nested.files[0].path='room/node.exe';
    assert.throws(()=>validateManifest(nested,DX11),'실행 파일은 BasicFPS-DX11.exe여야 해요.');
    assert.throws(()=>validateManifest({...sample('dx11-test-13-1'),build:'dx11-test-13-1',version:'0.2'},DX11));
    const foreign=sample();foreign.files[0].chunks[0].url='https://example.com/x.bin';
    assert.throws(()=>validateManifest(foreign,DX11));

    // Discovery: older releases without a manifest are skipped, Unreal trials are ignored, and the message names no Unreal.
    const manifest=sample();
    const asset=(tag,name)=>({name,browser_download_url:`${base}${tag}/${name}`});
    const listing=releases=>new NativeClient({root:path.join(tmp,'find'),profile:DX11,fetch:async url=>
      url.startsWith('https://api.github.com/')?reply(releases):reply(manifest)});
    const found=await listing([
      {tag_name:'build-99-1',prerelease:false,assets:[]},
      {tag_name:'dx11-test-13-1',prerelease:true,draft:false,assets:[asset('dx11-test-13-1','dx11-update.json')]},
      {tag_name:'dx11-test-12-1',prerelease:true,draft:false,assets:[asset('dx11-test-12-1','BasicFPS-DX11-Win64.zip')]},
    ]).latest();
    assert.equal(found.build,'dx11-test-13-1');
    for(const releases of [
      [{tag_name:'dx11-test-12-1',prerelease:true,draft:false,assets:[asset('dx11-test-12-1','BasicFPS-DX11-Win64.zip')]}],
      [{tag_name:'ue-test-5-1',prerelease:true,draft:false,assets:[asset('ue-test-5-1','unreal-update.json')]}],
      [],
    ])await assert.rejects(listing(releases).latest(),error=>/아직 다운로드할/.test(error.message)&&!/언리얼/.test(error.message));

    // Installation: progress reaches 100%, the folder uses the DX11 prefix, nothing mentions Unreal, a second run downloads nothing.
    const data=Buffer.from('hello'),messages=[],ratios=[];let calls=0;
    const client=new NativeClient({root:path.join(tmp,'installed'),profile:DX11,emit:text=>messages.push(text),progress:value=>ratios.push(value),
      fetch:async()=>{calls++;return reply(data);}});
    await client.install(manifest);
    const installed=await client.verifyLaunch();
    assert.match(installed.folder,/^dx11-13-1-[a-f0-9]{12}$/);
    assert.equal(path.basename(installed.executable),'BasicFPS-DX11.exe');
    assert.equal(ratios.at(-1),1);assert.ok(ratios.every((value,index)=>value>=0&&value<=1&&(!index||value>=ratios[index-1])));
    assert.match(messages.at(-1),/준비 완료/);assert.ok(messages.every(text=>!/언리얼/.test(text)),messages.join('|'));
    calls=0;await client.install(manifest);assert.equal(calls,0);
    await fs.writeFile(installed.executable,'tampered');
    await assert.rejects(client.verifyLaunch(),error=>/달라졌어요/.test(error.message)&&!/언리얼/.test(error.message));
    assert.equal(UNREAL.kind,'unreal');
    console.log('PASS: DX11 updater — own manifest profile, release discovery without Unreal trials, progress, install and tamper check.');
  }finally{await fs.rm(tmp,{recursive:true,force:true});}
}
main().catch(error=>{console.error('FAIL: '+error.stack);process.exitCode=1;});
