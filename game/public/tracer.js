export function createTracers(THREE,scene,camera){
  const capacity=64;
  const mesh=new THREE.InstancedMesh(new THREE.BoxGeometry(1,1,1),new THREE.MeshBasicMaterial({color:0xffe0a0,transparent:true,opacity:.75,depthWrite:false}),capacity);
  mesh.frustumCulled=false;
  mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  mesh.count=0;
  scene.add(mesh);
  const slots=Array.from({length:capacity},()=>({until:0,start:new THREE.Vector3(),end:new THREE.Vector3()}));
  const transform=new THREE.Object3D(),direction=new THREE.Vector3(),axis=new THREE.Vector3(0,0,1),muzzle=new THREE.Vector3();
  let cursor=0;
  function add(event,own){
    if(!Array.isArray(event.to)||event.to.length!==3||!event.to.every(Number.isFinite))return;
    const slot=slots[cursor++%capacity];
    if(own){camera.updateMatrixWorld();muzzle.set(.25,-.21,-.79).applyMatrix4(camera.matrixWorld);slot.start.copy(muzzle);}
    else if(Array.isArray(event.from)&&event.from.length===3&&event.from.every(Number.isFinite)){slot.start.fromArray(event.from);slot.start.y-=.12;}
    else return;
    slot.end.fromArray(event.to);
    slot.until=performance.now()+55;
  }
  function update(now){
    let count=0;
    for(const slot of slots){
      if(slot.until<=now)continue;
      direction.subVectors(slot.end,slot.start);
      const length=direction.length();
      if(length<.15)continue;
      transform.position.copy(slot.start).addScaledVector(direction,.5);
      transform.quaternion.setFromUnitVectors(axis,direction.divideScalar(length));
      const fade=Math.max(.15,(slot.until-now)/55);
      transform.scale.set(.009*fade,.009*fade,length);
      transform.updateMatrix();
      mesh.setMatrixAt(count++,transform.matrix);
    }
    mesh.count=count;
    if(count)mesh.instanceMatrix.needsUpdate=true;
  }
  return {add,update};
}
