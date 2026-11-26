'use strict';
// Publishes one DX11 trial release. The release stays a draft until every file is up and dx11-update.json is the last upload,
// so the launcher can never see half a release. Afterwards old trial releases are pruned.
const fs=require('fs'),path=require('path'),{execFileSync}=require('child_process');
const NOTES='런처가 자동으로 받아 실행하는 DX11 Windows 시험판입니다. ZIP을 직접 풀어 BasicFPS-DX11.exe를 실행해도 됩니다. 앱이 연 방 입장, Windows 통신과 소프트웨어 DX11 렌더링을 자동 확인했습니다. 노트북 외장 GPU·독점 전체화면·MSAA 성능은 별도 확인이 필요합니다.';
const TAG=/^dx11-test-(\d+)-(\d+)$/;
// Trial tags to delete: every draft, and all but the newest `keep` published ones (ordered by run number, then attempt).
function staleTags(releases,keep=5){
  const trial=releases.filter(item=>TAG.test(item.tagName));
  const order=tag=>{const [,run,attempt]=TAG.exec(tag);return Number(run)*1000+Number(attempt);};
  const published=trial.filter(item=>!item.isDraft).sort((a,b)=>order(b.tagName)-order(a.tagName));
  return [...trial.filter(item=>item.isDraft),...published.slice(keep)].map(item=>item.tagName);
}
function publish(downloadDir,chunkDir){
  const {validateManifest,DX11}=require('../native-update.cjs');
  const manifest=validateManifest(JSON.parse(fs.readFileSync(path.join(chunkDir,'dx11-update.json'),'utf8')),DX11);
  const repo=process.env.GH_REPO||'sungwoo1220/basic-fps',tag=manifest.build,sha=process.env.SOURCE_SHA;
  if(!/^[a-f0-9]{40}$/.test(sha||''))throw Error('SOURCE_SHA required');
  const gh=(args,options)=>execFileSync('gh',args,{stdio:'inherit',...options});
  gh(['release','create',tag,'--repo',repo,'--target',sha,'--draft','--prerelease','--latest=false','--title','Basic FPS DX11 Windows 시험판','--notes',NOTES]);
  const files=['BasicFPS-DX11-Win64.zip','SHA256SUMS.txt'].map(name=>path.join(downloadDir,name));
  gh(['release','upload',tag,'--repo',repo,...files]);
  const chunks=fs.readdirSync(chunkDir).filter(name=>/^[a-f0-9]{64}\.bin$/.test(name)).map(name=>path.join(chunkDir,name));
  for(let i=0;i<chunks.length;i+=20)gh(['release','upload',tag,'--repo',repo,...chunks.slice(i,i+20)]);
  gh(['release','upload',tag,'--repo',repo,path.join(chunkDir,'dx11-update.json')]);
  gh(['release','edit',tag,'--repo',repo,'--draft=false','--prerelease','--latest=false']);
  const listed=JSON.parse(execFileSync('gh',['release','list','--repo',repo,'--limit','200','--json','tagName,isDraft'],{encoding:'utf8'}));
  for(const old of staleTags(listed.filter(item=>item.tagName!==tag),5))gh(['release','delete',old,'--repo',repo,'--cleanup-tag','--yes']);
}
if(require.main===module)publish(path.resolve(process.argv[2]||'dx11-download'),path.resolve(process.argv[3]||'dx11-chunks'));
module.exports={staleTags,publish};
