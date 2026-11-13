/* Mechanical media motion over the approved console cases. See console-lids.json. */
(() => {
  'use strict';
  const clamp = value => Math.max(0, Math.min(1, value));
  const ease = value => { const v=clamp(value); return v*v*(3-2*v); };
  const phase = (time,start,end) => ease((time-start)/(end-start));
  const lids = {
    SATURN: {
      outline:[[552,311],[831,311],[839,321],[900,321],[932,333],[948,349],[956,369],[956,389],[944,420],[924,462],[907,495],[884,520],[849,541],[810,555],[785,564],[598,564],[544,552],[505,542],[472,528],[452,512],[443,495],[443,460],[454,422],[471,385],[489,359],[510,339],[533,321]],
      hinge:[552,311], across:[350,12], forward:[-80,250], up:[0,-325], angle:74,
      well:[695,440,180,90], underside:'#292b36', rib:'#515362'
    },
    DC: {
      outline:[[673,172],[874,172],[905,185],[933,207],[958,237],[980,270],[994,299],[995,339],[982,371],[957,404],[930,434],[893,462],[849,487],[801,507],[747,521],[689,532],[638,532],[581,521],[539,508],[496,486],[466,461],[441,435],[423,407],[416,382],[416,351],[427,322],[445,292],[468,266],[493,240],[522,215],[554,196],[589,182],[626,175]],
      hinge:[673,172], across:[260,0], forward:[-75,355], up:[0,-460], angle:74,
      well:[706,360,235,135], underside:'#8d8d99', rib:'#b9b5bc'
    },
    NEOCD: {
      outline:[[470,340],[922,340],[912,392],[896,454],[877,526],[863,577],[841,595],[802,607],[769,617],[507,617],[465,605],[431,593],[401,575],[411,533],[429,463],[445,397]],
      hinge:[470,340], across:[451,0], forward:[-70,270], up:[0,-325], angle:74,
      well:[660,480,180,100], underside:'#252832', rib:'#464a56'
    }
  };
  const assets = [
    {key:'SATURN',description:'Disc lid opens and closes',duration:6000,motionBounds:[435,25,620,550]},
    {key:'DC',description:'Disc lid opens and closes',duration:6000,viewBox:[0,-220,1254,1474],motionBounds:[405,-185,680,722]},
    {key:'NEOCD',description:'Disc lid opens and closes',duration:6000,motionBounds:[390,28,560,600]},
    {key:'SEGACD',description:'Front flap opens and disc tray extends',duration:6000,motionBounds:[126,725,550,202]},
    {key:'NES',description:'Front door opens and Game Pak loads',duration:6000,motionBounds:[170,365,635,415]}
  ];
  const extraImages = [
    {key:'SATURN_BAY',file:'SATURN-bay.png',width:1254,height:1254},
    {key:'DC_BAY',file:'DC-bay.png',width:1254,height:1254},
    {key:'NEOCD_BAY',file:'NEOCD-bay.png',width:1254,height:1254},
    {key:'SEGACD_TRAY',file:'SEGACD-tray.png',width:1254,height:1254},
    {key:'NES_CART',file:'NES-cartridge.png',width:1254,height:1254}
  ];
  // Map one fixed source plane into another. The basis changes only for hinges.
  function basis(c,origin,u,v,target,tu,tv) {
    const determinant=u[0]*v[1]-u[1]*v[0];
    const a=(tu[0]*v[1]-tv[0]*u[1])/determinant;
    const b=(tu[1]*v[1]-tv[1]*u[1])/determinant;
    const cc=(-tu[0]*v[0]+tv[0]*u[0])/determinant;
    const d=(-tu[1]*v[0]+tv[1]*u[0])/determinant;
    c.transform(a,b,cc,d,target[0]-a*origin[0]-cc*origin[1],target[1]-b*origin[0]-d*origin[1]);
    return a*d-b*cc;
  }
  function discLid(c,key,time,env) {
    const open=env.cycle(time); if(!open)return;
    const m=lids[key],angle=open*m.angle*Math.PI/180;
    env.clipImage(c,key+'_BAY',m.outline);
    const forward=m.forward.map((v,i)=>v*Math.cos(angle)+m.up[i]*Math.sin(angle));
    c.save();
    const facing=basis(c,m.hinge,m.across,m.forward,m.hinge,m.across,forward);
    if(Math.abs(facing)>.006){
      if(facing>=0)env.clipImage(c,key,m.outline);
      else {
        env.polygon(c,m.outline);c.fillStyle=m.underside;c.fill();
        c.save();env.polygon(c,m.outline);c.clip();
        c.strokeStyle='#171a25';c.lineWidth=17;c.stroke();
        const [x,y,rx,ry]=m.well;
        c.beginPath();c.ellipse(x,y,rx,ry,0,0,Math.PI*2);
        c.strokeStyle=m.rib;c.lineWidth=12;c.stroke();
        c.beginPath();c.ellipse(x,y,rx*.62,ry*.62,0,0,Math.PI*2);
        c.lineWidth=7;c.stroke();
        c.restore();
      }
    }
    c.restore();
  }
  const segaFlap=[[210,738],[667,738],[667,815],[210,815]];
  function segaCD(c,time,env) {
    const t=((time%6000)+6000)%6000;
    const flap=phase(t,650,1150)*(1-phase(t,4650,5100));
    const extension=phase(t,1050,2050)*(1-phase(t,3500,4550));
    if(!flap&&!extension)return;
    env.polygon(c,segaFlap);c.fillStyle='#10121a';c.fill();
    // The logo flap hinges on its bottom edge. It is not the tray face.
    const angle=flap*Math.PI/2;
    c.save();
    c.transform(1,0,25*Math.sin(angle)/77,Math.cos(angle)-55*Math.sin(angle)/77,
      -25*Math.sin(angle)*815/77,815*(1-Math.cos(angle)+55*Math.sin(angle)/77));
    env.clipImage(c,'SEGACD',segaFlap);c.restore();
    if(extension>0){
      c.save();env.polygon(c,[[210,751],[666,751],[666,815],[610,927],[126,927],[126,815],[210,815]]);c.clip();
      // This fixed projection matches the down-left case depth. Motion is translation only.
      c.translate(70*(1-extension),-151*(1-extension));
      basis(c,[382,434],[712,0],[-247,420],[218,754],[392,0],[-70,151]);
      env.image(c,'SEGACD_TRAY');c.restore();
    }
    // Preserve the original cable where it passes in front of the lower case.
    env.clipImage(c,'SEGACD',[[121,733],[153,733],[153,869],[171,894],[277,894],[317,912],[338,929],[306,929],[288,918],[167,918],[132,894],[121,870]]);
  }
  const nesDoor=[[304,479],[796,479],[779,510],[779,615],[579,615],[579,627],[488,627],[488,615],[272,615],[272,510]];
  function nes(c,time,env) {
    const t=((time%6000)+6000)%6000;
    const door=phase(t,400,1050)*(1-phase(t,4950,5550));
    const insert=phase(t,1150,2200)*(1-phase(t,3700,4800));
    const press=phase(t,2250,2525)*(1-phase(t,3325,3600));
    if(!door)return;
    // The open chamber is a shadowed cavity. Its fixed near edge hides the Game Pak.
    env.polygon(c,nesDoor);c.fillStyle='#171923';c.fill();
    c.fillStyle='#343542';c.fillRect(283,592,490,15);
    if(t>=800&&t<=5000){
      c.save();c.globalAlpha=phase(t,800,1100)*(1-phase(t,4700,5000));c.beginPath();c.rect(0,518,1254,290);c.clip();
      c.translate(135*insert,-170*insert+18*press);
      // Reverse the generated flat cartridge in its own plane: connector first, label up.
      basis(c,[195,915],[710,0],[180,-445],[710,548],[-380,0],[-135,170]);
      env.image(c,'NES_CART');c.restore();
      // The fascia remains fixed. Only the cartridge outside the mouth can cover it.
      if(insert>.88){
        env.clipImage(c,'NES',[[272,610],[779,610],[779,648],[272,648]]);
      }
    }
    // The NES door has a narrow horizontal return and a vertical front face.
    // Both rigid faces share the rear hinge, so the logo stays on the outer face.
    const angle=door*80*Math.PI/180,sine=Math.sin(angle),cosine=Math.cos(angle);
    const topForward=[-32*cosine,31*cosine-70*sine];
    c.save();
    basis(c,[304,479],[492,0],[-32,31],[304,479],[492,0],topForward);
    env.clipImage(c,'NES',[[304,479],[796,479],[779,510],[272,510]]);c.restore();
    c.save();
    basis(c,[272,510],[507,0],[0,105],[304+topForward[0],479+topForward[1]],
      [507,0],[-48*sine,105*cosine+46*sine]);
    env.clipImage(c,'NES',[[272,510],[779,510],[779,615],[579,615],[579,627],[488,627],[488,615],[272,615]]);
    c.restore();
  }
  function render(c,key,time,env) {
    c.save();c.imageSmoothingEnabled=false;
    if(lids[key])discLid(c,key,time,env);
    else if(key==='SEGACD')segaCD(c,time,env);
    else if(key==='NES')nes(c,time,env);
    c.restore();
  }
  window.ZyperArtOverrides ||= [];
  window.ZyperArtOverrides.push({id:'console-lids',assets,extraImages,render});
})();
