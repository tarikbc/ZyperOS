/* Game Boy Advance startup: native-grid wordmark geometry and observed motion.
 * Reference: https://tenor.com/view/gameboy-advance-game-boy-advance-intro-game-boy-advance-boot-up-game-boy-nintendo-gif-25292348
 * The 78-frame, 50 ms recording is reference material only. No captured frame,
 * font file, firmware, or sound is included. Coordinates use the 240x160 LCD.
 */
(() => {
  'use strict';
  const duration = 3900;
  const clamp = n => Math.max(0, Math.min(1, n));
  // Native-grid silhouette traces of the original marks. The one-bit rows
  // contain logo geometry only, with no captured background or color texture.
  const wordRows = ["0003fe00000000000000000000000000000000000", "001fffc0ff003f00ff07fff80fffc000ff07f00fe", "007fffc1ff007f00ff07fff80fffe003ffc7f01fc", "01ffffc1ff007f81ff07fff80ffff007ffe7f01fc", "03ffff81ff007f81ff0ffff80ffff80ffff3f83f8", "07ffff83ff00ff81ff0ffff80ffff81ffff3f83f8", "0fffff83ff80ff83ff0ffff81ffffc3ffff9fc7f0", "1ffc0187ff80ff83ff8fe0001fc3fc7fc3f9fcff0", "3ff00007ff81ff87ff8fc0001f81fc7f01fcfefe0", "3fe0000fff81ffc7ff8fc0001f81fcff00fcfffc0", "7f80000fff81ffcfff9fc0001f81fcfe00fe7ff80", "7f00001fff81ffcfff9fe0003f87f8fe007e7ff80", "7f00001f9fc3ffcfff9fffc03ffff1fc007e3ff00", "fe00003f9fc3ffdfff9fffc03ffff1fc007e3fe00", "fe00003f9fc3ffffff9fffc03fffe1fc007e1fe00", "fe0ffe7fffc7ffffbf9fffc03ffff1f8007e1fc00", "fe0ffe7fffc7efffbf9fff803ffff9f8007e1fc00", "fe0ffe7fffc7e7ff3f9fff803ffff9f800fe1f800", "fe1ffeffffefe7ff3fbf80007f07f9f800fc1f800", "fe1ffeffffefc7fe3fbf00007e01fdfc01fc1f800", "ff1fffffffffc7fe1fbf00007e01fdfc01fc3f800", "7f00fffc0fffc7fc1fbf00007e01fdfc03f83f800", "7f80fff80fff83fc1fff00007e03f8fe07f83f800", "3ffffff80fff83fc1fffff807ffff8ff9ff03f000", "3ffffff007ff83f81fffffc0fffff07ffff03f000", "0ffffff007ff03f81fffffc0fffff03fffe07f000", "07ffffe007ff01f01fffff80ffffe01fffc07f000", "03ffffe007ff01f01fffff80ffffc00fff007f000", "00ffcfc003fe01e01fffff80ffff0007fe007e000", "00000000000000000000000000000000f80000000"];
  const nintendoRows = ["00000000000000000000000000", "00000000000000000000000000", "781e70000000000000038001c0", "7c1ef8000780000000078003e0", "7e1e78000fc0000000078003f0", "7f1e78001fe0000000078003f0", "7f1e79ff9fe7f8ffc1ff8ff198", "7f9ef9ffefcf7cfff3ff9ff990", "7fde79f3e7df3cf9f3effc3ce0", "7ffef9f3f7df3ff8ffc7fc3c00", "79fef9f3f7fffff0ff87fc3c00", "78fef9f1e7bf3df0ff87fc3c00", "78fef9e1e79f1ff0ff87fc3c00", "787ef9e1e79f1ff0f7c7fc3c00", "783ef9e1e78fbff0f3ff9ef800", "783ef9e1f787f8f0f1ff8ff000", "00000000000000000000000000", "00000000000000000000000000"];
  function pixels(rows) {
    const out=[];rows.forEach((row,y)=>{[...row].forEach((hex,j)=>{
      const nibble=parseInt(hex,16);for(let b=0;b<4;b++)if(nibble&(8>>b))out.push([j*4+b,y]);
    });});return out;
  }
  const wordPixels=pixels(wordRows), nintendoPixels=pixels(nintendoRows);
  const xs=[39,63,83,111,135,158,180];
  const cuts=[25,47,74,95,119,142];
  // Slanted separators keep the tightly joined GAME letters together at rest.
  const letters=xs.map(()=>[]);
  for(const [x,y] of wordPixels){const unslanted=x+y*.16;let i=cuts.findIndex(c=>unslanted<c);if(i<0)i=6;letters[i].push([x-(xs[i]-39),y]);}
  function nintendo(c,sweep=null) {
    for(const [x,y] of nintendoPixels){
      const px=72+x;c.fillStyle=sweep!==null&&Math.abs(px-sweep)<9?'#f0c4ed':'#c828bf';c.fillRect(px,110+y,1,1);
    }
  }
  function letter(c,i,x,y,scale,color) {
    c.fillStyle=color;for(const [px,py] of letters[i])c.fillRect(x+px*scale,y+py*scale,scale,scale);
  }
  function wordmark(c,x,y,scale,color) {
    c.fillStyle=color;for(const [px,py] of wordPixels)c.fillRect(x+px*scale,y+py*scale,scale,scale);
  }
  function draw(target,time=0) {
    const c=target.getContext('2d');
    const t=((time%duration)+duration)%duration;
    c.save();c.setTransform(target.width/240,0,0,target.height/160,0,0);
    c.clearRect(0,0,240,160);c.fillStyle='#f5f5f4';c.fillRect(0,0,240,160);
    if(t<200||t>=3600){c.restore();return target;}
    nintendo(c);
    for(let i=0;i<7;i++){
      const start=360+i*93, p=clamp((t-start)/580);
      if(t<start)continue;
      const q=1-Math.pow(1-p,2),scale=1+(1-q)*2.2;
      const x=xs[i]+(1-q)*(-18+(i-3)*10);
      const y=49+(1-q)*(-65+Math.sin(i*1.4)*95);
      const color=p<1?`hsl(${(300+p*300)%360} 100% 48%)`:'#1712b8';
      letter(c,i,x,y,scale,color);
    }
    if(t>=1850&&t<2900){
      // The highlight crosses both marks, with the lower mark about 0.4 s later.
      const sweep=(t-1850)/850*210+20;
      c.save();c.beginPath();c.rect(sweep-16,45,32,38);c.clip();
      const glow=c.createLinearGradient(sweep-16,0,sweep+16,0);
      glow.addColorStop(0,'#a012cd');glow.addColorStop(.45,'#f0b7f5');glow.addColorStop(.55,'#f7e7f8');glow.addColorStop(1,'#a012cd');
      for(let i=0;i<7;i++)letter(c,i,xs[i],49,1,glow);
      c.restore();
      if(t>=2250)nintendo(c,(t-2250)/700*130+60);
    }
    if(t>3250){c.fillStyle=`rgba(245,245,244,${clamp((t-3250)/350)})`;c.fillRect(0,0,240,160);}
    c.restore();return target;
  }
  window.ZyperGBAStartup={draw,duration,wordmark};
})();
