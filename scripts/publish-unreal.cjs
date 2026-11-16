'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const {MAX_CHUNK,validateManifest}=require('../native-update.cjs');
function pack(directory,output,tag){
  directory=path.resolve(directory);output=path.resolve(output);
  if(output===directory||output.startsWith(directory+path.sep))throw Error('Output must be outside the packaged game directory');
  if(!/^ue-test-\d+-\d+$/.test(tag))throw Error('Expected ue-test-N-attempt tag');
  if(!fs.statSync(directory).isDirectory())throw Error('Packaged Windows directory is missing');
  if(fs.existsSync(output)&&fs.readdirSync(output).length)throw Error('Output directory must be empty');
  fs.mkdirSync(output,{recursive:true});const files=[];
  function walk(relative){
    for(const entry of fs.readdirSync(path.join(directory,relative),{withFileTypes:true})){
      const name=relative?relative+'/'+entry.name:entry.name;
      if(entry.isSymbolicLink())throw Error('Symlinks are not allowed in native packages');
      if(entry.isDirectory()){walk(name);continue;}
      if(!entry.isFile()||/\.(pdb|log)$/i.test(name))continue;
      const source=path.join(directory,name),size=fs.statSync(source).size,fd=fs.openSync(source,'r');
      const full=crypto.createHash('sha256'),chunks=[];
      try{
        const buffer=Buffer.alloc(MAX_CHUNK);let length;
        while((length=fs.readSync(fd,buffer,0,buffer.length,null))>0){
          const data=buffer.subarray(0,length);full.update(data);const hash=crypto.createHash('sha256').update(data).digest('hex');
          const asset=path.join(output,hash+'.bin');if(!fs.existsSync(asset))fs.writeFileSync(asset,data);
          chunks.push({size:length,sha256:hash,url:`https://github.com/sungwoo1220/basic-fps/releases/download/${tag}/${hash}.bin`});
        }
      }finally{fs.closeSync(fd);}
      files.push({path:name,size,sha256:full.digest('hex'),chunks});
    }
  }
  walk('');
  const executable=files.find(file=>/(?:^|\/)BasicFPS\/Binaries\/Win64\/BasicFPS(?:-Win64-(?:Development|Shipping))?\.exe$/.test(file.path))?.path;
  const [run,attempt]=tag.slice('ue-test-'.length).split('-').map(Number);
  const manifest=validateManifest({schema:2,kind:'unreal',platform:'win64',version:`0.1.${run*100+attempt}`,build:tag,executable,files});
  fs.writeFileSync(path.join(output,'unreal-update.json'),JSON.stringify(manifest,null,2));return manifest;
}
if(require.main===module){
  const manifest=pack(path.resolve(process.argv[2]||'unreal/BuildOutput/Package'),path.resolve(process.argv[3]||'unreal/BuildOutput/Release'),process.env.RELEASE_TAG||'');
  console.log(`Packaged ${manifest.files.length} native files (${manifest.version}). Trial channel only.`);
}
module.exports={pack};
