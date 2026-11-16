'use strict';
const fs=require('fs'),os=require('os'),path=require('path'),assert=require('assert/strict');
const {execFileSync}=require('child_process');
const root=path.join(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'basic-fps-rules-export-'));
try{
  for(const file of ['scripts/export-unreal-rules.cjs','game/public/shared.js','unreal/Source/BasicFPS/Public/GeneratedRules.h']){
    const target=path.join(tmp,file);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(root,file),target);
  }
  const header=path.join(tmp,'unreal/Source/BasicFPS/Public/GeneratedRules.h');
  const text=fs.readFileSync(header,'utf8').replace(/\r\n/g,'\n');
  fs.writeFileSync(header,text.replace(/\n/g,'\r\n'));
  execFileSync(process.execPath,['scripts/export-unreal-rules.cjs','--check'],{cwd:tmp,stdio:'pipe'});
  fs.writeFileSync(header,text.replace('ShotInterval=','ShotInterval=0+'));
  assert.throws(()=>execFileSync(process.execPath,['scripts/export-unreal-rules.cjs','--check'],{cwd:tmp,stdio:'pipe'}));
  console.log('PASS: generated rules check accepts Windows CRLF but rejects changed rules.');
}finally{fs.rmSync(tmp,{recursive:true,force:true});}
