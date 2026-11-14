/* Mechanical cartridge overrides. Sources and measured axes: console-cartridges.json. */
(() => {
  'use strict';
  // crop is measured in the immutable generated layer. Position and travel use
  // the approved 1254-pixel console canvas. The layer never changes size in motion.
  const mechanisms = {
    '32X': {crop:[55,300,1138,681],width:310,at:[589,34],travel:[0,150],lip:[[551,278],[930,278]]},
    A2600: {crop:[248,202,759,891],width:178,at:[579,220],travel:[0,132],lip:[[568,509],[770,509]]},
    A5200: {crop:[138,250,976,791],width:250,at:[583,145],travel:[0,151],shearY:.055,lip:[[553,437],[846,459]]},
    A7800: {crop:[247,260,753,819],width:278,at:[549,28],travel:[0,125],shearY:.012,lip:[[521,368],[846,373]]},
    GENESIS: {crop:[107,352,1042,625],width:300,at:[622,122],travel:[0,149],lip:[[609,404],[941,404]]},
    N64: {crop:[154,348,946,570],width:334,at:[539,78],travel:[0,135],lip:[[520,358],[882,358]]},
    // A fixed viewBox gives the tall AES cartridge enough headroom for full
    // removal. The seated position and the cartridge's rigid size stay fixed.
    NEOGEO: {crop:[138,208,976,855],width:490,at:[474,-105],travel:[0,235],lip:[[438,344],[1000,344]]},
    PCE: {crop:[202,375,862,531],width:235,at:[658,690],travel:[68,-145],shearX:.23,mode:'card',lip:[[745,615],[925,623]]},
    SMS: {crop:[188,322,878,802],width:300,at:[693,117],travel:[0,150],lip:[[672,479],[1015,479]]},
    VB: {crop:[155,443,947,382],width:250,at:[406,631],travel:[56,-114],shearX:-.14,mode:'underside'},
    COLECO: {crop:[231,229,791,829],width:191,at:[785,199],travel:[0,137],lip:[[780,487],[984,487]],slot:[[785,474],[978,474],[978,487],[785,487]]},
    MSX2: {crop:[150,401,956,590],width:235,at:[793,207],travel:[0,158],lip:[[775,501],[1060,501]],mode:'flap'}
  };
  const bounds = {
    '32X':[575,25,340,260],A2600:[570,210,200,305],
    A5200:[575,135,270,330],A7800:[540,20,300,360],
    GENESIS:[615,115,315,295],N64:[530,70,350,295],
    NEOGEO:[465,-115,510,465],PCE:[650,605,315,245],
    SMS:[685,110,320,375],VB:[380,485,355,255],
    COLECO:[775,190,215,305],MSX2:[755,195,320,320]
  };
  const descriptions={PCE:'TurboChip slides into the front card channel',VB:'Game Pak slides into the underside slot',MSX2:'Slot cover opens and cartridge loads',NEOGEO:'AES cartridge fully clears and loads into the top slot'};
  const assets=Object.keys(mechanisms).map(key=>({key,description:descriptions[key]||'Cartridge lowers into the top slot',duration:6000,motionBounds:bounds[key],...(key==='NEOGEO'?{viewBox:[-70,-140,1394,1394]}:{})}));
  const extraImages=assets.map(({key})=>({key:key+'_CARTRIDGE',file:key+(key==='32X'?'-cartridge-v2.png':'-cartridge.png'),width:1254,height:1254}));
  const smooth = x => { x=Math.max(0,Math.min(1,x));return x*x*(3-2*x); };
  const phase = time => ((time%6000)+6000)%6000;
  function insertion(time) {
    const t=phase(time);
    if(t<800)return 0;
    if(t<1900)return smooth((t-800)/1100);
    if(t<3600)return 1;
    if(t<4700)return 1-smooth((t-3600)/1100);
    return 0;
  }
  function flapAmount(time){
    const t=phase(time);
    if(t<180)return 0;
    if(t<760)return smooth((t-180)/580);
    if(t<4750)return 1;
    if(t<5350)return 1-smooth((t-4750)/600);
    return 0;
  }
  function slotClip(c,lip,below,env,top=0){
    const [[x0,y0],[x1,y1]]=lip,dy=(y1-y0)/(x1-x0);
    const left=y0-dy*x0,right=y0+dy*(1254-x0);
    env.polygon(c,below?[[0,left],[1254,right],[1254,1254],[0,1254]]:[[0,top],[1254,top],[1254,right],[0,left]]);c.clip();
  }
  function drawCartridge(c,key,m,amount,env){
    const [sx,sy,sw,sh]=m.crop,s=m.width/sw;
    c.save();c.translate(m.at[0]+m.travel[0]*amount,m.at[1]+m.travel[1]*amount);
    c.transform(1,m.shearY||0,m.shearX||0,1,0,0);
    c.drawImage(env.images[key+'_CARTRIDGE'],sx,sy,sw,sh,0,0,m.width,sh*s);c.restore();
  }
  const flap=[[779,442],[1055,442],[1055,499],[765,499],[765,461]];
  function msxFlap(c,amount,env){
    if(amount===0)return;
    // The original top-right cover hinges along its rear horizontal edge.
    env.polygon(c,flap);c.fillStyle='#11131c';c.fill();
    const angle=amount*78*Math.PI/180;
    const d=Math.cos(angle)-1.25*Math.sin(angle);
    const shear=14*(1-Math.cos(angle))/57;
    c.save();c.transform(1,0,shear,d,-shear*442,442*(1-d));
    if(d>=0)env.clipImage(c,'MSX2',flap);
    else {env.polygon(c,flap);c.fillStyle='#303340';c.fill();c.strokeStyle='#676976';c.lineWidth=7;c.stroke();}
    c.restore();
  }
  function render(c,key,time,env){
    const m=mechanisms[key];if(!m)return;
    c.save();c.imageSmoothingEnabled=false;
    c.beginPath();c.rect(...bounds[key]);c.clip();
    if(m.slot){env.polygon(c,m.slot);c.fillStyle='#11131c';c.fill();}
    if(m.mode==='flap')msxFlap(c,flapAmount(time),env);
    c.save();
    if(m.lip)slotClip(c,m.lip,m.mode==='card',env,key==='NEOGEO'?-140:0);
    drawCartridge(c,key,m,insertion(time),env);
    c.restore();
    if(m.mode==='underside'){
      // The real slot is below the red shell. Restore its near edge and the
      // stand from the approved source; the card passes behind both.
      env.clipImage(c,key,[[280,120],[1140,120],[1140,570],[850,603],[610,595],[430,590],[280,520]]);
      env.clipImage(c,key,[[610,590],[850,590],[1150,1150],[800,1200],[600,770]]);
    }
    c.restore();
  }
  window.ZyperArtOverrides ||= [];
  window.ZyperArtOverrides.push({id:'console-cartridges',assets,extraImages,render});
})();
