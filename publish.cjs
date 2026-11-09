'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const tag=process.env.RELEASE_TAG;
if(!tag||!/^[-a-zA-Z0-9.]+$/.test(tag))throw Error('RELEASE_TAG required');
const match=/^build-(\d+)-(\d+)$/.exec(tag);
if(!match)throw Error('Expected build-N-attempt release tag');
const patch=Number(match[1])*100+Number(match[2]);
if(!Number.isSafeInteger(patch))throw Error('Invalid build number');
const version='3.4.'+patch;
const base='https://github.com/sungwoo1220/basic-fps/releases/download/'+tag+'/';
fs.mkdirSync('patch',{recursive:true});
const files=[];
function add(file){const bytes=fs.readFileSync(path.join('game',file));const sha256=crypto.createHash('sha256').update(bytes).digest('hex');const asset=sha256+'.bin';fs.writeFileSync(path.join('patch',asset),bytes);files.push({path:file,sha256,size:bytes.length,url:base+asset});}
function walk(dir){for(const entry of fs.readdirSync(path.join('game',dir),{withFileTypes:true})){const file=dir+'/'+entry.name;if(entry.isDirectory())walk(file);else add(file);}}
add('server.js');add('launcher.html');walk('public');
fs.writeFileSync('patch/update.json',JSON.stringify({schema:1,version,build:tag,files},null,2));
console.log('Published game version '+version+' ('+tag+')');
