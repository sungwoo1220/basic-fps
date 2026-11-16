'use strict';
const assert=require('assert/strict'),fs=require('fs/promises'),os=require('os'),path=require('path');
const {NativeClient,validateManifest,safePath,MAX_CHUNK}=require('../../native-update.cjs');
const {pack}=require('../../scripts/publish-unreal.cjs');
async function main(){
  const tmp=await fs.mkdtemp(path.join(os.tmpdir(),'basic-fps-native-'));
  try{
    const source=path.join(tmp,'source'),output=path.join(tmp,'release'),root=path.join(tmp,'installed');
    await fs.mkdir(path.join(source,'Windows/BasicFPS/Binaries/Win64'),{recursive:true});
    await fs.writeFile(path.join(source,'Windows/BasicFPS/Binaries/Win64/BasicFPS.exe'),Buffer.alloc(MAX_CHUNK+17,0x42));
    await fs.writeFile(path.join(source,'Windows/settings.ini'),'trial');
    const manifest=pack(source,output,'ue-test-1-1');
    assert.equal(manifest.files.find(file=>file.path===manifest.executable).chunks.length,2);
    const messages=[];let corrupt=false,calls=0;
    const client=new NativeClient({root,emit:text=>messages.push(text),fetch:async url=>{
      calls++;const name=new URL(url).pathname.split('/').pop();let data=await fs.readFile(path.join(output,name));if(corrupt)data=Buffer.alloc(data.length);
      return {ok:true,body:(async function*(){yield data;})()};
    }});
    await client.install(manifest);const first=await client.verifyLaunch();assert(first);assert.match(messages.at(-1),/준비 완료/);
    assert.equal((await fs.stat(first.executable)).size,MAX_CHUNK+17);
    calls=0;await client.install(manifest);assert.equal(calls,0);
    console.log('PASS: Unreal updater — 8MB chunk download, complete hash verification, unchanged files reused.');
    const changed=structuredClone(manifest);changed.build='ue-test-2-1';changed.version='0.1.201';
    for(const file of changed.files)for(const chunk of file.chunks)chunk.url=chunk.url.replace('ue-test-1-1','ue-test-2-1');
    await fs.writeFile(first.executable,'tampered');corrupt=true;
    await assert.rejects(client.install(changed),/손상/);assert.equal((await client.current()).folder,first.folder);
    assert.equal((await fs.readdir(root)).filter(name=>name.startsWith('ue-')).length,1);
    await assert.rejects(client.verifyLaunch(),/달라졌어요/);
    corrupt=false;await client.install(manifest);const repaired=await client.verifyLaunch();assert.notEqual(repaired.folder,first.folder);
    await client.install(changed);const second=await client.verifyLaunch();assert.notEqual(second.folder,repaired.folder);
    assert.equal(await client.rollback(),true);assert.equal((await client.current()).folder,repaired.folder);
    await fs.writeFile(second.executable,'broken previous install');
    assert.equal(await client.rollback(),false,'복구할 파일도 해시 검사를 통과해야 해요.');
    console.log('PASS: Unreal updater — rejects corrupted chunks and launch files, preserves old install, supports rollback.');
    for(const bad of ['../evil.exe','/absolute.exe','C:/evil.exe','Windows/../evil.exe','Windows/CON.txt','Windows/foo.','Windows/foo ','Windows\\evil.exe'])assert.equal(safePath(bad),false,bad);
    const duplicate=structuredClone(manifest);duplicate.files.push({...duplicate.files[0],path:duplicate.files[0].path.toUpperCase()});assert.throws(()=>validateManifest(duplicate));
    const traversal=structuredClone(manifest);traversal.files[0].path='../evil';assert.throws(()=>validateManifest(traversal));
    const wrongHost=structuredClone(manifest);wrongHost.files[0].chunks[0].url='https://example.com/evil';assert.throws(()=>validateManifest(wrongHost));
    const ownManifest=structuredClone(manifest);ownManifest.files.push({...ownManifest.files[0],path:'manifest.json'});assert.throws(()=>validateManifest(ownManifest));
    const parentFile=structuredClone(manifest);parentFile.files.push({...parentFile.files[0],path:'Windows'});assert.throws(()=>validateManifest(parentFile));
    assert.throws(()=>pack(source,path.join(source,'nested-output'),'ue-test-3-1'),/outside/);
    const emptyChannel=new NativeClient({root:path.join(tmp,'empty'),fetch:async()=>({ok:true,body:(async function*(){yield Buffer.from('[]');})()})});
    await assert.rejects(emptyChannel.latest(),/아직 다운로드할/);
    const listing=new NativeClient({root:path.join(tmp,'discovery'),fetch:async url=>{
      const data=url.startsWith('https://api.github.com/')?Buffer.from(JSON.stringify([
        {tag_name:'build-99-1',prerelease:false,assets:[]},
        {tag_name:manifest.build,prerelease:true,draft:true,assets:[]},
        {tag_name:manifest.build,prerelease:true,draft:false,assets:[{name:'unreal-update.json',browser_download_url:`https://github.com/sungwoo1220/basic-fps/releases/download/${manifest.build}/unreal-update.json`}]}
      ])):Buffer.from(JSON.stringify(manifest));
      return {ok:true,body:(async function*(){yield data;})()};
    }});
    assert.equal((await listing.latest()).build,manifest.build);
    console.log('PASS: Unreal updater — rejects traversal, Windows reserved paths, duplicate names, foreign URLs; no invented trial release.');
  }finally{await fs.rm(tmp,{recursive:true,force:true});}
}
main().catch(error=>{console.error('FAIL: '+error.stack);process.exitCode=1;});
