const $=id=>document.getElementById(id);
let browserReady=false,nativeReady=false,nativeBusy=false;
$('address').value=localStorage.getItem('server')||'http://127.0.0.1:3000';
$('client').value=localStorage.getItem('client')==='unreal'?'unreal':'browser';
function ready(){
  $('play').disabled=nativeBusy||($('client').value==='unreal'?(!nativeReady||($('host').checked&&!browserReady)):!browserReady);
  $('nativeUpdate').disabled=nativeBusy;
  $('update').disabled=nativeBusy;
}
window.launcher.onStatus(text=>$('status').textContent=text);
window.launcher.onReady(value=>{browserReady=value;ready();});
window.launcher.onNativeState(value=>{nativeReady=value.installed;nativeBusy=value.busy;$('nativeStatus').textContent=value.installed?`설치된 시험판 ${value.version} · 실기 확인 필요`:'아직 언리얼 시험판이 설치되지 않았어요. Windows 빌드가 성공한 뒤 다운로드할 수 있어요.';ready();});
$('update').onclick=()=>window.launcher.update();
$('nativeUpdate').onclick=()=>window.launcher.updateNative();
$('client').onchange=()=>{localStorage.setItem('client',$('client').value);ready();};
$('host').onchange=ready;
$('address').oninput=()=>{$('addressError').hidden=true;$('address').removeAttribute('aria-invalid');};
$('play').onclick=()=>{
  const address=$('address').value.trim(),host=$('host').checked;
  if(!host)try{const url=new URL(address);if(!['http:','https:'].includes(url.protocol)||url.username||url.password||address.length>2048||/[\s"\\]/.test(address))throw Error();}
  catch{$('addressError').textContent='http://서버주소:3000 형식으로 입력해 주세요.';$('addressError').hidden=false;$('address').setAttribute('aria-invalid','true');$('address').focus();return;}
  localStorage.setItem('server',address);window.launcher.play({host,address,client:$('client').value});
};
ready();
