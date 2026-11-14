/* Export the reviewed animation renderer without altering source artwork. */
(() => {
  'use strict';
  function hash(data){
    let value=2166136261;
    const words=new Uint32Array(data.buffer,data.byteOffset,data.byteLength/4);
    for(const word of words)value=Math.imul(value^word,16777619);
    return value>>>0;
  }
  function equal(a,b){
    if(a.length!==b.length)return false;
    for(let i=0;i<a.length;i++)if(a[i]!==b[i])return false;
    return true;
  }
  async function asset(key,sources='source/'){
    const art=window.ZyperSystemArt;
    if(!art.keys.includes(key))throw new Error(`Unknown system: ${key}`);
    await art.load(sources,[key]);
    const duration=art.durationFor(key);
    const count=Math.min(64,Math.max(2,Math.floor(duration/10)));
    const cell=256;
    const pose=document.createElement('canvas');pose.width=pose.height=cell;
    const context=pose.getContext('2d',{willReadFrequently:true});
    const unique=[],buckets=new Map(),frames=[];
    for(let i=0;i<count;i++){
      const start=Math.ceil(i*duration/count),end=Math.ceil((i+1)*duration/count);
      art.render(pose,key,start,true);
      const pixels=context.getImageData(0,0,cell,cell);
      const digest=hash(pixels.data),bucket=buckets.get(digest)||[];
      let tile=bucket.find(index=>equal(unique[index].data,pixels.data));
      if(tile===undefined){tile=unique.length;unique.push(pixels);bucket.push(tile);buckets.set(digest,bucket);}
      const previous=frames.at(-1),ms=end-start;
      if(previous&&previous.tile===tile&&previous.ms+ms<=2000)previous.ms+=ms;
      else frames.push({tile,ms});
    }
    const expectedPoses=art.profiles[key]?.poseCount;
    if(unique.length<2&&!art.profiles[key]?.static)throw new Error(`${key}: exported animation has no changing pixels`);
    if(expectedPoses&&unique.length!==expectedPoses)throw new Error(`${key}: expected ${expectedPoses} poses, received ${unique.length}`);
    const cols=Math.min(8,unique.length),rows=Math.ceil(unique.length/cols);
    const atlas=document.createElement('canvas');atlas.width=cols*cell;atlas.height=rows*cell;
    const ac=atlas.getContext('2d');
    unique.forEach((pixels,index)=>ac.putImageData(pixels,(index%cols)*cell,Math.floor(index/cols)*cell));
    const stillTime=art.profiles[key]?.stillTime??(key==='GBA'?1500:key==='SNES'?2700:Math.min(duration-100,4300));
    art.render(pose,key,stillTime);
    return {
      key,name:art.names[key],description:art.descriptions[key],cell,cols,rows,unique:unique.length,duration_ms:duration,
      kind:art.profiles[key]?.kind||'system',static:!!art.profiles[key]?.static,expected_poses:expectedPoses||null,
      motion_bounds:art.motionRegion(key,cell),
      frames,still_time_ms:stillTime,decoded_atlas_bytes:atlas.width*atlas.height*4,
      inputs:art.dependencies(key).map(k=>`source/${art.sourceFiles[k]||k}.png`),
      renderers:art.rendererFiles(key),
      atlas:atlas.toDataURL('image/png'),still:pose.toDataURL('image/png')
    };
  }
  async function batch(keys,sources='source/'){
    const assets=[];
    for(const key of keys)assets.push(await asset(key,sources));
    return {schema:1,assets};
  }
  window.ZyperArtExporter={asset,batch};
})();
