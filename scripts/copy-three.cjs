'use strict';
const fs=require('fs');
const path=require('path');
const root=path.join(__dirname,'..');
const from=path.join(root,'node_modules','three','build','three.module.js');
const to=path.join(root,'game','public','three.module.js');
if(fs.existsSync(from))fs.copyFileSync(from,to);
else if(!fs.existsSync(to))throw new Error('three.module.js를 찾을 수 없습니다. 먼저 npm install을 실행하세요.');
