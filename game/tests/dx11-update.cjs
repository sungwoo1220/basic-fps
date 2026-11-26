'use strict';
// DX11 game updates: profile validation, release discovery and installation (network and Electron are mocked).
const assert=require('assert/strict'),crypto=require('crypto'),fs=require('fs/promises'),os=require('os'),path=require('path');
const {NativeClient,validateManifest,DX11,UNREAL,MAX_CHUNK}=require('../../native-update.cjs');
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

    // Packing the assembled folder: 8MB chunks, identical chunks stored once, debug files skipped, strict tag and exe checks.
    const {pack}=require('../../scripts/publish-dx11.cjs');
    const source=path.join(tmp,'package'),output=path.join(tmp,'chunks');
    await fs.mkdir(path.join(source,'room/public'),{recursive:true});
    await fs.writeFile(path.join(source,'BasicFPS-DX11.exe'),'exe-bytes');
    await fs.writeFile(path.join(source,'room/node.exe'),Buffer.alloc(MAX_CHUNK*2+100,7));
    await fs.writeFile(path.join(source,'room/server.js'),'same');
    await fs.writeFile(path.join(source,'room/public/copy.js'),'same');
    await fs.writeFile(path.join(source,'room/debug.pdb'),'skip');
    const packed=pack(source,output,'dx11-test-13-1');
    assert.equal(packed.version,'0.2.1301');assert.equal(packed.executable,'BasicFPS-DX11.exe');assert.equal(packed.kind,'dx11');
    const node=packed.files.find(file=>file.path==='room/node.exe');
    assert.equal(node.chunks.length,3);assert.equal(node.chunks[0].sha256,node.chunks[1].sha256);assert.equal(node.chunks[2].size,100);
    assert.ok(!packed.files.some(file=>file.path.endsWith('.pdb')));
    assert.deepEqual((await fs.readdir(output)).filter(name=>name.endsWith('.bin')).length,4,'exe, 두 종류의 node.exe 조각, 같은 내용 파일 하나');
    assert.deepEqual(JSON.parse(await fs.readFile(path.join(output,'dx11-update.json'),'utf8')),packed);
    assert.ok(validateManifest(packed,DX11));
    assert.throws(()=>pack(source,output,'dx11-test-13-1'),/empty/);
    assert.throws(()=>pack(source,path.join(tmp,'o1'),'ue-test-13-1'),/dx11-test/);
    assert.throws(()=>pack(source,path.join(source,'inside'),'dx11-test-1-1'),/outside/);
    const withoutExe=path.join(tmp,'no-exe');await fs.mkdir(withoutExe);await fs.writeFile(path.join(withoutExe,'readme.txt'),'x');
    assert.throws(()=>pack(withoutExe,path.join(tmp,'o2'),'dx11-test-1-1'),/BasicFPS-DX11\.exe/);
    assert.throws(()=>pack(source,path.join(tmp,'o3'),'dx11-test-2-1',{maxAssets:2}),/Too many/);
    console.log('PASS: DX11 packer — 8MB chunks stored once, debug files skipped, tag/exe/output/asset-count guards.');

    // Old trial releases are pruned by run/attempt number, drafts always go, other release kinds are never touched.
    const {staleTags}=require('../../scripts/upload-dx11-release.cjs');
    const release=(tagName,isDraft=false)=>({tagName,isDraft});
    const all=[3,4,5,6,7,8,9].map(n=>release(`dx11-test-${n}-1`));
    all.push(release('dx11-test-9-2'),release('build-40-1'),release('dx11-test-10-1',true),release('ue-test-4-1'),release('dx11-test-x-1'));
    assert.deepEqual(staleTags(all,5).sort(),['dx11-test-10-1','dx11-test-3-1','dx11-test-4-1','dx11-test-5-1']);
    assert.deepEqual(staleTags(all,20),['dx11-test-10-1'],'초안만 지워야 해요.');
    assert.deepEqual(staleTags([],5),[]);
    console.log('PASS: DX11 release pruning — keeps the newest by run number, removes drafts, ignores other releases.');
  }finally{await fs.rm(tmp,{recursive:true,force:true});}
}
main().catch(error=>{console.error('FAIL: '+error.stack);process.exitCode=1;});
