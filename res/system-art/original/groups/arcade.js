/* Original generated ZyperOS art. Source PNGs stay immutable.
 * Symbols use exactly two object poses. Hardware media follows its real axis.
 * The Vectrex glyph bits and startup layout follow the cited BIOS listing.
 */
(() => {
  'use strict';
  const assets = [
  {
    "file": "ARCADE.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "ARCADE",
    "name": "Arcade",
    "description": "Red button presses and releases",
    "motionBounds": [
      552,
      478,
      154,
      142
    ],
    "cue": "button",
    "poseCount": 2
  },
  {
    "file": "AW.png",
    "width": 1254,
    "height": 1254,
    "duration": 6000,
    "key": "AW",
    "name": "Atomiswave",
    "description": "ROM cartridge inserts and lifts",
    "motionBounds": [
      275,
      95,
      560,
      610
    ],
    "cue": "cartridge"
  },
  {
    "file": "FAVORITES.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "FAVORITES",
    "name": "Favorites",
    "description": "Heart beats",
    "motionBounds": [
      170,
      230,
      920,
      880
    ],
    "cue": "heartbeat",
    "poseCount": 2
  },
  {
    "file": "SCUMMVM.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "SCUMMVM",
    "name": "ScummVM",
    "description": "Compass needle turns",
    "motionBounds": [
      205,
      795,
      205,
      215
    ],
    "cue": "compass",
    "poseCount": 2
  },
  {
    "file": "MAME.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "MAME",
    "name": "MAME",
    "description": "Arcade letters bounce",
    "motionBounds": [
      45,
      420,
      1160,
      470
    ],
    "cue": "letters",
    "poseCount": 2
  },
  {
    "file": "MSX2.png",
    "width": 1254,
    "height": 1254,
    "duration": 6000,
    "key": "MSX2",
    "name": "MSX2",
    "description": "Panasonic FS-A1 power lamp",
    "motionBounds": [
      985,
      591,
      14,
      16
    ],
    "cue": "lamp"
  },
  {
    "file": "MP4.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "MUSE",
    "name": "MP4",
    "description": "Video and audio playback",
    "motionBounds": [
      805,
      390,
      390,
      560
    ],
    "cue": "music-note",
    "poseCount": 2
  },
  {
    "file": "NAOMI.png",
    "width": 1254,
    "height": 1254,
    "duration": 6000,
    "key": "NAOMI",
    "name": "NAOMI",
    "description": "ROM-board case inserts and lifts",
    "motionBounds": [
      80,
      100,
      750,
      665
    ],
    "cue": "rom-board"
  },
  {
    "file": "PICO8.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "PICO8",
    "name": "PICO-8",
    "description": "Pixel flower bounces",
    "motionBounds": [
      715,
      420,
      280,
      280
    ],
    "cue": "flower",
    "poseCount": 2
  },
  {
    "file": "OPENBOR.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "OPENBOR",
    "name": "OpenBOR",
    "description": "Fist punches",
    "motionBounds": [
      260,
      230,
      740,
      860
    ],
    "cue": "punch",
    "poseCount": 2
  },
  {
    "file": "PORTS.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "PORTS",
    "name": "Ports",
    "description": "Transfer arrows move",
    "motionBounds": [
      265,
      170,
      680,
      980
    ],
    "cue": "arrows",
    "poseCount": 2
  },
  {
    "file": "VECTREX.png",
    "width": 1254,
    "height": 1254,
    "duration": 10240,
    "key": "VECTREX",
    "name": "Vectrex",
    "description": "Original startup characters and border",
    "motionBounds": [
      430,
      280,
      395,
      485
    ],
    "screen": {
      "rect": [
        430,
        280,
        395,
        485
      ],
      "native": [
        240,
        320
      ],
      "bezel": "#08090e"
    },
    "cue": "screen"
  },
  {
    "file": "DOOM.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "DOOM",
    "name": "Doom",
    "description": "Title recoils",
    "motionBounds": [
      45,
      340,
      1160,
      670
    ],
    "cue": "recoil",
    "poseCount": 2
  },
  {
    "file": "TIC80.png",
    "width": 1254,
    "height": 1254,
    "duration": 800,
    "key": "TIC80",
    "name": "TIC-80",
    "description": "Code brackets open",
    "motionBounds": [
      270,
      320,
      710,
      345
    ],
    "cue": "code",
    "poseCount": 2
  }
];
  const layers = new Map();
  const screens = new Map();
  const byKey = Object.fromEntries(assets.map(a => [a.key, a]));
  const phase = (t, duration = 6000) => Math.floor((((t % duration) + duration) % duration) / duration * 64);
  const pose = time => (((time % 800) + 800) % 800) >= 400;
  // Copy only the named object. The source PNG and fixed surrounding pixels stay unchanged.
  function sprite(key,rect,env,points=null) {
    const id=key+':'+rect.join(',')+':'+JSON.stringify(points);
    if (!layers.has(id)) {
      const [x,y,w,h]=rect, layer=env.canvas(w,h), c=layer.getContext('2d');
      c.imageSmoothingEnabled=false;c.translate(-x,-y);
      if(points){env.polygon(c,points);c.clip();}
      env.image(c,key);layers.set(id,layer);
    }
    return layers.get(id);
  }
  function erase(c,rect,env,points=null) {
    c.save();if(points){env.polygon(c,points);c.clip();}
    c.clearRect(...rect);c.restore();
  }
  function move(c,key,rect,env,{dx=0,dy=0,angle=0,scale=1,center=null,points=null}={}) {
    const [x,y,w,h]=rect,[cx,cy]=center||[x+w/2,y+h/2];
    erase(c,rect,env,points);c.save();c.translate(cx+dx,cy+dy);
    c.rotate(angle);c.scale(scale,scale);c.drawImage(sprite(key,rect,env,points),x-cx,y-cy);c.restore();
  }
  // A rectangular crop can include nearby lettering or another symbol.
  // Keep the opaque component containing the seed, plus its edge pixels.
  function isolatedSprite(key,rect,seed,env) {
    const id='isolated:'+key+':'+rect.join(',');
    if(!layers.has(id)){
      const [x,y,w,h]=rect,raw=sprite(key,rect,env);
      const data=raw.getContext('2d',{willReadFrequently:true}).getImageData(0,0,w,h);
      const owned=new Uint8Array(w*h),start=(seed[1]-y)*w+seed[0]-x,stack=[start];
      if(data.data[start*4+3]<128)throw new Error(`${key}: sprite seed is outside the object`);
      owned[start]=1;
      while(stack.length){
        const p=stack.pop(),px=p%w,py=Math.floor(p/w);
        for(const q of [px? p-1:-1,px+1<w?p+1:-1,py?p-w:-1,py+1<h?p+w:-1]){
          if(q>=0&&!owned[q]&&data.data[q*4+3]>=128){owned[q]=1;stack.push(q);}
        }
      }
      const rim=owned.slice();
      for(let p=0;p<owned.length;p++)if(owned[p]){
        const px=p%w,py=Math.floor(p/w);
        for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++)
          if(px+dx>=0&&px+dx<w&&py+dy>=0&&py+dy<h)rim[(py+dy)*w+px+dx]=1;
      }
      const layer=env.canvas(w,h),mask=env.canvas(w,h),eraseData=new ImageData(w,h);
      for(let p=0;p<owned.length;p++){
        if(!rim[p])data.data.fill(0,p*4,p*4+4);
        else eraseData.data[p*4+3]=255;
      }
      layer.getContext('2d').putImageData(data,0,0);mask.getContext('2d').putImageData(eraseData,0,0);
      layers.set(id,{layer,mask});
    }
    return layers.get(id);
  }
  function moveIsolated(c,key,rect,seed,env,dx,dy){
    const {layer,mask}=isolatedSprite(key,rect,seed,env);
    c.save();c.globalCompositeOperation='destination-out';c.drawImage(mask,rect[0],rect[1]);c.restore();
    c.drawImage(layer,rect[0]+dx,rect[1]+dy);
  }
  // One fixed affine projection aligns the generated media with the existing bay.
  // Animation adds only vertical translation, without stretching or rotating the media.
  function projectedMedia(c,layer,src,dst,lift,env,rect) {
    const [s0,s1,s2]=src,[d0,d1,d2]=dst;
    const ux=s1[0]-s0[0],uy=s1[1]-s0[1],vx=s2[0]-s0[0],vy=s2[1]-s0[1],det=ux*vy-uy*vx;
    const ax=d1[0]-d0[0],ay=d1[1]-d0[1],bx=d2[0]-d0[0],by=d2[1]-d0[1];
    const a=(ax*vy-bx*uy)/det,b=(ay*vy-by*uy)/det;
    const cc=(bx*ux-ax*vx)/det,d=(by*ux-ay*vx)/det;
    c.save();c.transform(a,b,cc,d,d0[0]-a*s0[0]-cc*s0[1],d0[1]-b*s0[0]-d*s0[1]-lift);
    c.drawImage(sprite(layer,rect,env),rect[0],rect[1]);c.restore();
  }
  function hardware(c,key,time,env) {
    const lift=210*(1-env.cycle(time));
    if(key==='AW'){
      projectedMedia(c,'AW_CARTRIDGE',[[515,398],[991,548],[270,650]],
        [[492,348],[786,421],[308,551]],lift,env,[250,390,760,490]);
      env.clipImage(c,'AW',[[275,544],[306,544],[475,593],[475,624],[576,653],[610,680],[835,680],[835,705],[275,705]]);
    }else{
      projectedMedia(c,'NAOMI_ROM_CASE',[[500,510],[1100,532],[191,794]],
        [[390,373],[785,390],[125,699]],lift,env,[175,500,955,380]);
      env.clipImage(c,'NAOMI',[[0,692],[125,692],[563,718],[740,741],[1254,741],[1254,1254],[0,1254]]);
    }
  }
  function symbols(c,key,time,env) {
    if(key==='OPENBOR'){
      if(pose(time))return;
      const rect=[300,405,565,650];
      const glove=[[305,420],[580,405],[630,417],[689,419],[755,469],[803,558],[797,597],
        [850,622],[865,725],[803,823],[741,874],[745,1004],[705,1048],[390,1048],[351,995],[354,891],[300,800]];
      erase(c,[300,240,690,820],env);
      c.drawImage(sprite(key,rect,env,glove),rect[0]-30,rect[1]+35);return;
    }
    if(!pose(time))return;
    if(key==='FAVORITES')move(c,key,[210,270,840,810],env,{scale:.88,center:[627,674]});
    if(key==='MAME'){
      erase(c,[58,468,1140,385],env);
      for(const [x,w,dy] of [[60,338,-32],[398,242,24],[640,318,-32],[958,240,24]])
        c.drawImage(sprite(key,[x,470,w,380],env),x,470+dy);
    }
    if(key==='DOOM')move(c,key,[80,385,1095,600],env,{dx:-12,dy:-25});
    // One export pixel is 1254/256 source units. These translations keep the
    // pixel grid intact; arbitrary-angle rotation scattered bright highlights.
    if(key==='PICO8')moveIsolated(c,key,[740,460,230,220],[850,565],env,0,-4*1254/256);
    if(key==='MUSE')move(c,key,[823,460,356,460],env,{dy:-60});
    if(key==='TIC80'){
      moveIsolated(c,key,[310,345,225,320],[390,480],env,-6*1254/256,0);
      moveIsolated(c,key,[728,345,217,320],[840,480],env,6*1254/256,0);
      moveIsolated(c,key,[515,365,230,260],[627,500],env,0,-4*1254/256);
    }
    if(key==='PORTS'){
      const top=[[310,174],[900,174],[900,420],[700,420],[700,355],[560,340],[450,380],[360,445],[310,445]];
      const bottom=[[356,970],[486,879],[538,879],[538,968],[670,978],[779,950],[819,898],
        [880,898],[892,972],[855,1030],[728,1090],[554,1100],[535,1138],[471,1138],[426,1100],[379,1050],[356,1050]];
      move(c,key,[310,174,590,271],env,{dx:35,points:top});
      move(c,key,[356,879,536,260],env,{dx:-35,points:bottom});
    }
    if(key==='ARCADE'){
      const rect=[573,491,121,95];
      const cap=[[592,493],[668,493],[668,509],[689,509],[689,555],[677,555],[677,572],[605,572],[605,559],[577,559],[577,518],[592,518]];
      c.save();env.polygon(c,cap);c.fillStyle='#252333';c.fill();c.restore();
      c.drawImage(sprite(key,rect,env,cap),rect[0],rect[1]+15);
    }
    if(key==='SCUMMVM'){
      const rect=[225,824,151,165];
      const needle=[[229,975],[265,919],[268,883],[347,834],[375,828],[347,880],[330,925],[280,966],[252,987]];
      c.save();env.polygon(c,needle);c.fillStyle='#ffe2b6';c.fill();c.restore();
      c.save();c.translate(299,904);c.rotate(-Math.PI/4);
      c.drawImage(sprite(key,rect,env,needle),rect[0]-299,rect[1]-904);c.restore();
    }
  }
  const glyphs = {
    ' ':[0,0,0,0,0,0,0], A:[32,80,136,136,248,136,136],
    C:[112,136,128,128,128,136,112], D:[240,72,72,72,72,72,240],
    E:[248,128,128,224,128,128,248], G:[120,128,128,152,136,136,120],
    I:[112,32,32,32,32,32,112], N:[136,200,168,152,136,136,136],
    R:[240,136,136,240,160,144,136], S:[112,136,64,32,16,136,112],
    T:[248,168,32,32,32,32,32], V:[136,136,136,80,80,32,32],
    W:[136,136,136,168,168,168,80], X:[136,136,80,32,80,136,136]
  };
  function text(c,word,y,sx,sy) {
    const width=(word.length*8-3)*sx, x=(240-width)/2;
    c.fillStyle='#e4f7ff';
    for(let i=0;i<word.length;i++) glyphs[word[i]].forEach((row,ry)=>{
      for(let bit=0;bit<8;bit++) if(row & (128>>bit))
        c.fillRect(x+(i*8+bit)*sx,y+ry*sy,Math.max(.8,sx*.86),1.15);
    });
  }
  function vectrex(time,env) {
    if (!screens.has('VECTREX')) screens.set('VECTREX',env.canvas(240,320));
    const canvas=screens.get('VECTREX'), c=canvas.getContext('2d');
    const q=phase(time,10240);
    c.fillStyle='#08090e'; c.fillRect(0,0,240,320);
    // The original executive uses four successive dashed-line patterns.
    c.strokeStyle='#ccecff'; c.lineWidth=1.1;
    c.setLineDash([4,7]); c.lineDashOffset=-(q%4)*2.75;
    c.strokeRect(22,21,196,278); c.strokeRect(34,34,172,252);
    c.setLineDash([]);
    // The executive alternates the extra tall title pass every 32 loops.
    text(c,'VECTREX',104,2.3,3.25);
    if (Math.floor(q/4)%2===0) text(c,'VECTREX',102,2.3,3.75);
    text(c,'GCE',205,1.65,1.15);
    text(c,'ENTERTAINING',226,.95,1.15);
    text(c,'NEW IDEAS',244,.95,1.15);
    return canvas;
  }
  window.ZyperOriginalGroups ||= [];
  window.ZyperOriginalGroups.push({
    id:'arcade', assets,
    extraImages:[
      {key:'AW_CARTRIDGE',file:'AW-cartridge.png',width:1254,height:1254},
      {key:'NAOMI_ROM_CASE',file:'NAOMI-rom-case.png',width:1254,height:1254}
    ],
    render(c,key,time,env) {
      const a=byKey[key];
      if (!a || key==='VECTREX') return;
      if (key==='MSX2') {
        const q=phase(time);
        c.fillStyle=q>=8 && q<56 ? '#ff493e' : '#6a2630';
        c.fillRect(986,592,12,14);
        return;
      }
      if(key==='AW'||key==='NAOMI'){
        c.save();c.beginPath();c.rect(...a.motionBounds);c.clip();
        hardware(c,key,time,env);c.restore();
      }
      else symbols(c,key,time,env);
    },
    getScreen(key,time,env) { return key==='VECTREX' ? vectrex(time,env) : null; }
  });
})();
