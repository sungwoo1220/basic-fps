'use strict';
// Packs the assembled DX11 trial folder (BasicFPS-DX11.exe + room/...) into 8MB chunks named by SHA-256 plus dx11-update.json.
// The launcher downloads only the chunks of files whose hash changed. Same idea as publish-unreal.cjs, kept separate on purpose.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const {MAX_CHUNK,validateManifest,DX11}=require('../native-update.cjs');
function pack(directory,output,tag,{maxAssets=990}={}){
  directory=path.resolve(directory);output=path.resolve(output);
  if(output===directory||output.startsWith(directory+path.sep))throw Error('Output must be outside the packaged game directory');
  const match=/^dx11-test-(\d+)-(\d+)$/.exec(tag);
  if(!match)throw Error('Expected a dx11-test-N-attempt tag');
  if(!fs.existsSync(directory)||!fs.statSync(directory).isDirectory())throw Error('Packaged game directory is missing');
  if(!fs.existsSync(path.join(directory,'BasicFPS-DX11.exe')))throw Error('BasicFPS-DX11.exe is missing from the packaged directory');
  if(fs.existsSync(output)&&fs.readdirSync(output).length)throw Error('Output directory must be empty');
  fs.mkdirSync(output,{recursive:true});
  const files=[],assets=new Set();
  function walk(relative){
    for(const entry of fs.readdirSync(path.join(directory,relative),{withFileTypes:true})){
      const name=relative?relative+'/'+entry.name:entry.name;
      if(entry.isSymbolicLink())throw Error('Symlinks are not allowed in game packages');
      if(entry.isDirectory()){walk(name);continue;}
      if(!entry.isFile()||/\.(pdb|log)$/i.test(name))continue;
      const source=path.join(directory,name),size=fs.statSync(source).size,fd=fs.openSync(source,'r');
      const full=crypto.createHash('sha256'),chunks=[];
      try{
        const buffer=Buffer.alloc(MAX_CHUNK);let length;
        while((length=fs.readSync(fd,buffer,0,buffer.length,null))>0){
          const data=buffer.subarray(0,length);full.update(data);const hash=crypto.createHash('sha256').update(data).digest('hex');
          if(!assets.has(hash)){
            if(assets.size>=maxAssets)throw Error(`Too many unique chunks (limit ${maxAssets}); a release holds at most 1000 assets`);
            fs.writeFileSync(path.join(output,hash+'.bin'),data);assets.add(hash);
          }
          chunks.push({size:length,sha256:hash,url:`https://github.com/sungwoo1220/basic-fps/releases/download/${tag}/${hash}.bin`});
        }
      }finally{fs.closeSync(fd);}
      files.push({path:name,size,sha256:full.digest('hex'),chunks});
    }
  }
  walk('');
  const [run,attempt]=[Number(match[1]),Number(match[2])];
  const manifest=validateManifest({schema:2,kind:'dx11',platform:'win64',version:`0.2.${run*100+attempt}`,build:tag,executable:'BasicFPS-DX11.exe',files},DX11);
  fs.writeFileSync(path.join(output,'dx11-update.json'),JSON.stringify(manifest,null,2));return manifest;
}
if(require.main===module){
  if(process.argv.length<4)throw Error('Usage: node scripts/publish-dx11.cjs <assembled-folder> <empty-output-folder> (RELEASE_TAG=dx11-test-N-attempt)');
  const manifest=pack(process.argv[2],process.argv[3],process.env.RELEASE_TAG||'');
  console.log(`Packaged ${manifest.files.length} files (${manifest.version}).`);
}
module.exports={pack};
