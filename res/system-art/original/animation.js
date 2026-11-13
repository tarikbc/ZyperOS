/* Original ZyperOS artwork preview. Generated base pixels stay immutable.
 * Hardware and startup marks belong to their respective makers.
 * Screen motion is a visual reconstruction, not an emulated boot ROM.
 */
(() => {
  'use strict';
  const names = {GB:'Game Boy', PS1:'PlayStation', '3DO':'3DO', SNES:'SNES', GBA:'Game Boy Advance'};
  const descriptions = {
    GB:'Original startup · 256 × 256',
    PS1:'Disc lid · open and close',
    '3DO':'Disc tray · open and close',
    SNES:'Cartridge · insert and remove',
    GBA:'Original Game Boy Advance startup'
  };
  const keys = Object.keys(names);
  const imageKeys = [...keys, 'PS1-bay', '3DO-tray', 'SNES-cartridge'];
  const sourceFiles = {PS1:'PS1-ports',GBA:'GBA-screen','3DO-tray':'3DO-tray-angle'};
  const images = {};
  const helpers = {};
  const basePixels = new Map();
  const sourceSizes = {GB:[1247,1261],PS1:[1448,1086],'PS1-bay':[1448,1086],
    '3DO':[1402,1122],'3DO-tray':[1402,1122],SNES:[1254,1254],GBA:[1254,1254],
    'SNES-cartridge':[1536,1024]};
  const groups = window.ZyperOriginalGroups || [];
  const profiles = {}, owners = {}, rendererParents = {}, pending = new Map(), loadedFrom = {};
  for(const group of groups){
    for(const asset of group.assets){
      if(names[asset.key])throw new Error(`Duplicate system artwork: ${asset.key}`);
      names[asset.key]=asset.name;descriptions[asset.key]=asset.description;
      keys.push(asset.key);imageKeys.push(asset.key);
      sourceFiles[asset.key]=asset.file.replace(/\.png$/,'');
      sourceSizes[asset.key]=[asset.width,asset.height];
      profiles[asset.key]=asset;owners[asset.key]=group;
    }
    for(const extra of group.extraImages||[]){
      if(sourceSizes[extra.key])throw new Error(`Duplicate image layer: ${extra.key}`);
      imageKeys.push(extra.key);sourceFiles[extra.key]=extra.file.replace(/\.png$/,'');
      sourceSizes[extra.key]=[extra.width,extra.height];
    }
  }
  for(const override of window.ZyperArtOverrides||[]){
    for(const patch of override.assets){
      if(!profiles[patch.key])throw new Error(`Unknown override system: ${patch.key}`);
      rendererParents[patch.key]=owners[patch.key].id;
      const asset={...profiles[patch.key],...patch};profiles[patch.key]=asset;owners[patch.key]=override;
      names[patch.key]=asset.name;descriptions[patch.key]=asset.description;
      sourceFiles[patch.key]=asset.file.replace(/\.png$/,'');sourceSizes[patch.key]=[asset.width,asset.height];
    }
    for(const extra of override.extraImages||[]){
      if(sourceSizes[extra.key])throw new Error(`Duplicate override layer: ${extra.key}`);
      imageKeys.push(extra.key);sourceFiles[extra.key]=extra.file.replace(/\.png$/,'');
      sourceSizes[extra.key]=[extra.width,extra.height];
    }
  }
  const durationFor = key=>key==='GBA'&&window.ZyperGBAStartup
    ? window.ZyperGBAStartup.duration : (profiles[key]?.duration||6000);
  const rendererFiles = key=>['animation.js','export.js',
    ...(rendererParents[key]?[`groups/${rendererParents[key]}.js`]:[]),
    ...(owners[key]?[`groups/${owners[key].id}.js`]:[]),
    ...(key==='GBA'&&window.ZyperGBAStartup?['gba-startup.js']:[])];
  function frameLayout(key,width,height=width){
    const [w,h]=sourceSizes[key];
    const [vx,vy,vw,vh]=profiles[key]?.viewBox||[0,0,w,h];
    const scale=Math.min(width/vw,height/vh);
    return {scale,x:(width-vw*scale)/2-vx*scale,y:(height-vh*scale)/2-vy*scale};
  }
  function motionRegion(key,cell=256){
    const fixed={GB:[94,78,153,127],GBA:[83,100,173,153],PS1:[90,4,205,133],
      '3DO':[62,115,164,150],SNES:[110,22,195,123]};
    if(fixed[key]){
      const [x0,y0,x1,y1]=fixed[key],s=cell/256;
      return [Math.floor(x0*s),Math.floor(y0*s),Math.ceil((x1-x0)*s),Math.ceil((y1-y0)*s)];
    }
    const p=profiles[key],[x,y,w,h]=p.motionBounds,{scale:s,x:ox,y:oy}=frameLayout(key,cell);
    const x0=Math.max(0,Math.floor(ox+x*s)-2),y0=Math.max(0,Math.floor(oy+y*s)-2);
    return [x0,y0,Math.min(cell,Math.ceil(ox+(x+w)*s)+2)-x0,Math.min(cell,Math.ceil(oy+(y+h)*s)+2)-y0];
  }
  function canvas(w,h) { const c=document.createElement('canvas'); c.width=w;c.height=h; return c; }
  function polygon(c, points) { c.beginPath(); points.forEach((p,i)=>i?c.lineTo(...p):c.moveTo(...p)); c.closePath(); }
  function sample(name) { return images[name]; }
  function image(c,name) { const [w,h]=sourceSizes[name]; c.drawImage(sample(name),0,0,w,h); }
  function clipImage(c,name,points) { c.save();polygon(c,points);c.clip();image(c,name);c.restore(); }
  const clamp = v=>Math.max(0,Math.min(1,v));
  const ease = v=>{v=clamp(v);return v*v*(3-2*v);};
  function cycle(t) {
    t=((t%6000)+6000)%6000;
    if(t<800)return 0;
    if(t<1900)return ease((t-800)/1100);
    if(t<3600)return 1;
    if(t<4700)return 1-ease((t-3600)/1100);
    return 0;
  }
  // This outline follows the generated closed lid, in source-image pixels.
  const lid = [[529,420],[540,365],[570,327],[620,286],[676,254],[730,234],
    [776,222],[918,220],[965,237],[1003,268],[1042,310],[1064,355],
    [1066,406],[1045,453],[1006,491],[951,524],[894,546],[817,559],
    [744,556],[675,541],[615,518],[570,484],[542,453]];
  const logoBytes = 'CE ED 66 66 CC 0D 00 0B 03 73 00 83 00 0C 00 0D 00 08 11 1F 88 89 00 0E DC CC 6E E6 DD DD D9 99 BB BB 67 63 6E 0E EC CC DD DC 99 9F BB B9 33 3E'.split(' ').map(n=>parseInt(n,16));
  const logoPixels=[];
  logoBytes.forEach((byte,i)=>{const half=Math.floor(i/24),local=i%24;
    for(let bit=0;bit<8;bit++)if(byte&(1<<(7-bit)))logoPixels.push([Math.floor(local/2)*4+bit%4,half*4+(local%2)*2+Math.floor(bit/4)]);
  });
  const registered=[0x3c,0x42,0xb9,0xa5,0xb9,0xa5,0x42,0x3c];
  function nintendo(c,x,y,scale,color) {
    c.fillStyle=color;
    for(const [px,py] of logoPixels)c.fillRect(x+px*scale,y+py*scale,scale,scale);
    registered.forEach((row,ry)=>{for(let bit=0;bit<8;bit++)if(row&(0x80>>bit))
      c.fillRect(x+48*scale+bit*scale/2,y+ry*scale/2,scale/2,scale/2);});
  }
  function gbScreen(time) {
    const c=helpers.gb.getContext('2d');
    c.fillStyle='#9cac54';c.fillRect(0,0,160,144);
    const t=time%6000;
    if(t>=450){const y=-40+Math.min(104,Math.floor((t-450)/(3500/104)));
      nintendo(c,32,y,2,'#263b24');}
    return helpers.gb;
  }
  function gbaScreen(time) {
    if(!window.ZyperGBAStartup)throw new Error('The GBA startup module is missing');
    window.ZyperGBAStartup.draw(helpers.gba,time);
    return helpers.gba;
  }
  function playstation(c,time) {
    const open=cycle(time); if(open===0)return;
    clipImage(c,'PS1-bay',lid);
    const angle=open*74*Math.PI/180;
    // Projected console-plane axes: across=(270,15), forward=(-94,168).
    // Rotate the forward axis toward vertical=(0,-230). The rear hinge
    // and the across axis stay fixed, including the case's sideways depth.
    const hx=894,hy=222,det=46770;
    const vx=-94*Math.cos(angle),vy=168*Math.cos(angle)-230*Math.sin(angle);
    const a=(270*168-15*vx)/det,b=(15*168-15*vy)/det;
    const cc=(270*94+270*vx)/det,d=(15*94+270*vy)/det;
    const facing=a*d-b*cc;
    if(Math.abs(facing)<.012)return;
    c.save();c.transform(a,b,cc,d,hx-a*hx-cc*hy,hy-b*hx-d*hy);
    if(facing>=0)clipImage(c,'PS1',lid);
    else {
      // The outer face turns away. The underside uses the source lid outline.
      polygon(c,lid);c.fillStyle='#777782';c.fill();
      c.save();polygon(c,lid);c.clip();
      c.strokeStyle='#393b48';c.lineWidth=14;c.stroke();
      c.beginPath();c.ellipse(800,388,215,137,0,0,Math.PI*2);
      c.fillStyle='#a6a2aa';c.fill();c.lineWidth=7;c.stroke();
      c.beginPath();c.ellipse(800,388,155,101,0,0,Math.PI*2);
      c.strokeStyle='#898591';c.lineWidth=9;c.stroke();
      c.restore();
    }
    c.restore();
  }
  function threeDO(c,time) {
    const open=cycle(time);if(open===0)return;
    // The cavity and moving tray are the only patches over the fixed case.
    c.fillStyle='#11131a';c.fillRect(456,497,410,86);
    // The tray is a rigid plane. Its fixed texture projects into the same
    // down-left depth axis as the case. Only translation varies with time.
    c.save();c.beginPath();c.rect(0,497,1402,625);c.clip();
    c.translate(110*(1-open),-82*(1-open));
    const across=410/347,depth=82/118,shear=(-110+105*across)/118;
    c.transform(across,0,shear,depth,455-across*503-shear*514,497-depth*514);
    clipImage(c,'3DO-tray',[[503,514],[850,514],[736,632],[398,632]]);
    c.restore();
    c.drawImage(sample('3DO'),455/sourceSizes['3DO'][0]*sample('3DO').width,
      497/sourceSizes['3DO'][1]*sample('3DO').height,
      410/sourceSizes['3DO'][0]*sample('3DO').width,
      86/sourceSizes['3DO'][1]*sample('3DO').height,
      455-110*open,497+82*open,410,86);
  }
  function snes(c,time) {
    const t=time%6000;
    const insertion=t<600?0:t<1700?ease((t-600)/1100):t<3900?1:t<5000?1-ease((t-3900)/1100):0;
    const y=115+insertion*275;
    c.save();c.beginPath();c.rect(0,0,1254,570);c.clip();
    c.drawImage(helpers.cart,548,y,400,253);c.restore();
    // The clip ends behind the fixed near lip; the case needs no redraw.
  }
  function render(target,key,time=0,verify=false) {
    if(!images[key])throw new Error(`Unknown or unloaded artwork: ${key}`);
    time=((time%durationFor(key))+durationFor(key))%durationFor(key);
    // A fresh working layer keeps alpha-compositing history out of the fixed
    // case. Only the declared motion region reaches the destination canvas.
    const working=canvas(target.width,target.height);
    const c=working.getContext('2d',{willReadFrequently:true});
    c.setTransform(1,0,0,1,0,0);
    c.imageSmoothingEnabled=false;
    const [w,h]=sourceSizes[key];
    const {scale:ratio,x:offsetX,y:offsetY}=frameLayout(key,target.width,target.height);
    const cacheKey=`${key}:${target.width}:${target.height}`;
    if(!basePixels.has(cacheKey)){
      const still=canvas(target.width,target.height);
      const fixed=still.getContext('2d',{willReadFrequently:true});
      fixed.imageSmoothingEnabled=false;
      fixed.translate(offsetX,offsetY);fixed.scale(ratio,ratio);
      image(fixed,key);
      basePixels.set(cacheKey,fixed.getImageData(0,0,target.width,target.height));
    }
    // Browser image resampling can change after the first readback. Copy the
    // same canonical pixels for every pose instead of resizing the case again.
    c.putImageData(basePixels.get(cacheKey),0,0);
    c.save();c.translate(offsetX,offsetY);c.scale(ratio,ratio);
    if(key==='PS1')playstation(c,time);
    if(key==='3DO')threeDO(c,time);
    if(key==='SNES')snes(c,time);
    const env={images,sourceSizes,canvas,polygon,clipImage,image,nintendo,cycle};
    if(owners[key]?.render)owners[key].render(c,key,time,env);
    c.restore();
    const screenSpecs=profiles[key]?.screens||(profiles[key]?.screen?[profiles[key].screen]:[]);
    if(key==='GB'||key==='GBA')screenSpecs.push(null);
    for(const [screenIndex,screenSpec] of screenSpecs.entries()){
      const rect=screenSpec?.rect||(key==='GB'?[468,391,274,227]:[414,495,425,246]);
      const x=Math.round(offsetX+rect[0]*ratio);
      const y=Math.round(offsetY+rect[1]*ratio);
      const sw=Math.round(rect[2]*ratio),sh=Math.round(rect[3]*ratio);
      const screen=screenSpec?owners[key].getScreen(key,time,env,screenIndex):(key==='GB'?gbScreen(time):gbaScreen(time));
      if(!screen)throw new Error(`${key}: screen animation returned no canvas`);
      const aspect=screenSpec?.displayAspect||(screen.width/screen.height);
      const innerWidth=Math.round(Math.min(sw,sh*aspect));
      const innerHeight=Math.round(Math.min(sh,sw/aspect));
      const left=offsetX+rect[0]*ratio;
      const top=offsetY+rect[1]*ratio;
      c.fillStyle=screenSpec?.bezel||(key==='GB'?'#292a31':'#272933');
      c.fillRect(Math.floor(left),Math.floor(top),
        Math.ceil(left+rect[2]*ratio)-Math.floor(left),Math.ceil(top+rect[3]*ratio)-Math.floor(top));
      // Preserve the native screen ratio and draw on whole output pixels.
      c.drawImage(screen,Math.round(x+(sw-innerWidth)/2),Math.round(y+(sh-innerHeight)/2),innerWidth,innerHeight);
    }
    const base=basePixels.get(cacheKey),paint=c.getImageData(0,0,target.width,target.height);
    const [mx,my,mw,mh]=motionRegion(key,target.width);
    if(verify){
      for(let y=0;y<target.height;y++)for(let x=0;x<target.width;x++){
        if(x>=mx&&x<mx+mw&&y>=my&&y<my+mh)continue;
        const p=(y*target.width+x)*4;
        // Browser premultiplied-alpha rounding can differ by one unit.
        if([0,1,2,3].some(k=>Math.abs(paint.data[p+k]-base.data[p+k])>1))
          throw new Error(`${key}: drawing exceeds motion bounds at ${x},${y}`);
      }
    }
    const result=new ImageData(new Uint8ClampedArray(base.data),target.width,target.height);
    for(let y=my;y<my+mh;y++){
      const offset=(y*target.width+mx)*4;
      result.data.set(paint.data.subarray(offset,offset+mw*4),offset);
    }
    target.getContext('2d').putImageData(result,0,0);
    return target;
  }
  function dependencies(key){
    const extra={PS1:['PS1-bay'],'3DO':['3DO-tray'],SNES:['SNES-cartridge']}[key]||[];
    return [key,...extra,...(owners[key]?.extraImages||[]).map(x=>x.key)];
  }
  async function load(sources,requested=keys) {
    const wanted=[...new Set(requested.flatMap(dependencies))];
    await Promise.all(wanted.map(async key=>{
      const src=typeof sources==='string'?`${sources}${sourceFiles[key]||key}.png`:sources[key];
      if(!src)throw new Error(`Missing image source: ${key}`);
      if(loadedFrom[key]===src)return;
      const jobKey=key+':'+src;
      if(!pending.has(jobKey))pending.set(jobKey,(async()=>{
        const im=new Image();im.src=src;await im.decode();images[key]=im;loadedFrom[key]=src;
        for(const cacheKey of basePixels.keys())if(cacheKey.startsWith(key+':'))basePixels.delete(cacheKey);
      })().finally(()=>pending.delete(jobKey)));
      await pending.get(jobKey);
    }));
    helpers.gb ||= canvas(160,144);helpers.gba ||= canvas(240,160);
    if(images['SNES-cartridge']&&helpers.cartSource!==loadedFrom['SNES-cartridge']){
      helpers.cart=canvas(1044,660);
      const cc=helpers.cart.getContext('2d');cc.imageSmoothingEnabled=false;
      cc.translate(-246,-216);
      clipImage(cc,'SNES-cartridge',[[246,322],[281,280],[386,266],[386,243],[422,216],
        [1146,216],[1155,265],[1254,266],[1290,302],[1290,824],[1260,873],[246,873]]);
      helpers.cartSource=loadedFrom['SNES-cartridge'];
    }
    return true;
  }
  function mount(root,sources) {
    const target=root.querySelector('[data-stage]');
    const pick=root.querySelector('[data-system]');
    const toggle=root.querySelector('[data-play]');
    const caption=root.querySelector('[data-caption]');
    const initial=pick.value||'PS1';
    pick.replaceChildren(...[...keys].sort((a,b)=>names[a].localeCompare(names[b])).map(key=>{
      const option=document.createElement('option');option.value=key;option.textContent=names[key];return option;
    }));pick.value=initial;
    let key=pick.value||'PS1';
    let playing=!matchMedia('(prefers-reduced-motion: reduce)').matches;
    let time=playing?0:2600,previous=null,ready=false;
    function labels(){toggle.textContent=playing?'Pause':'Play';toggle.setAttribute('aria-pressed',String(playing));
      caption.textContent=descriptions[key];target.setAttribute('aria-label',`${names[key]} animation preview`);}
    labels();
    async function select(next,t=playing?0:2600){
      key=next;pick.value=next;time=t;previous=null;ready=false;labels();
      try{await load(sources,[next]);if(key===next){ready=true;render(target,key,time);}}
      catch(error){if(key===next)caption.textContent=`The artwork could not load: ${error.message}`;throw error;}
    }
    pick.addEventListener('change',()=>{select(pick.value).catch(()=>{});});
    toggle.addEventListener('click',()=>{playing=!playing;previous=null;labels();});
    const dark=root.querySelector('[data-dark]');
    if(dark)dark.addEventListener('change',()=>root.classList.toggle('art-dark',dark.checked));
    function tick(now){if(!root.isConnected)return;
      if(previous!==null&&playing&&!document.hidden)time+=Math.min(now-previous,100);
      previous=now;if(ready)render(target,key,time);requestAnimationFrame(tick);}
    const promise=select(key,time).then(()=>{requestAnimationFrame(tick);})
      .catch(error=>{caption.textContent=`The artwork could not load: ${error.message}`;toggle.disabled=true;throw error;});
    return {ready:promise,show:async(next,t=2600)=>{playing=false;return select(next,t);}};
  }
  window.ZyperSystemArt={keys,names,descriptions,imageKeys,sourceFiles,sourceSizes,profiles,load,render,mount,cycle,durationFor,dependencies,rendererFiles,motionRegion};
})();
