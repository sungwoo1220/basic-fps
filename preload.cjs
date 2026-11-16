'use strict';
const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('launcher',{update:()=>ipcRenderer.invoke('update'),updateNative:()=>ipcRenderer.invoke('native-update'),play:args=>ipcRenderer.invoke('play',args),onStatus:fn=>ipcRenderer.on('status',(_,value)=>fn(value)),onReady:fn=>ipcRenderer.on('ready',(_,value)=>fn(value)),onNativeState:fn=>ipcRenderer.on('native-state',(_,value)=>fn(value))});
