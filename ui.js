const $=id=>document.getElementById(id);
$('address').value=localStorage.getItem('server')||'http://127.0.0.1:3000';
window.launcher.onStatus(text=>$('status').textContent=text);
window.launcher.onReady(ready=>$('play').disabled=!ready);
$('update').onclick=()=>window.launcher.update();
$('play').onclick=()=>{localStorage.setItem('server',$('address').value);window.launcher.play({host:$('host').checked,address:$('address').value});};
