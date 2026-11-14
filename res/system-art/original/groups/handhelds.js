/* Original hardware sources stay fixed. LCD content is reconstructed in code.
 * Sources and visual-reconstruction limits are recorded in handhelds.json.
 */
(() => {
  'use strict';
  const clamp=v=>Math.max(0,Math.min(1,v));
  const assets=[
    {key:'GBC',name:'Game Boy Color',rect:[451,306,343,280],native:[160,144],description:'Original startup · color sweep'},
    {key:'GG',name:'Game Gear',rect:[470,501,314,241],native:[160,144],description:'Launch-model display · backlight power'},
    {key:'GW',name:'Game & Watch',rect:[414,530,424,266],native:[160,100],description:'Ball clock · segmented LCD'},
    {key:'LYNX',name:'Atari Lynx',rect:[428,526,386,264],native:[160,102],description:'Lynx II · backlight control'},
    {key:'NDS',name:'Nintendo DS',rect:[417,230,420,319],native:[256,192],description:'Original startup · both LCD screens'},
    {key:'NGP',name:'Neo Geo Pocket',rect:[439,487,374,335],native:[160,152],description:'Original startup · monochrome',duration:7000},
    {key:'NGPC',name:'Neo Geo Pocket Color',rect:[426,467,401,356],native:[160,152],description:'Original startup · color',duration:7000},
    {key:'PKMINI',name:'Pokémon mini',rect:[431,414,391,238],native:[96,64],description:'Original startup · monochrome LCD'},
    {key:'WS',name:'WonderSwan',rect:[410,487,497,282],native:[224,144],description:'Original startup · Bandai mark'},
    {key:'WSC',name:'WonderSwan Color',rect:[428,499,460,261],native:[224,144],description:'Original startup · Bandai mark'},
    {key:'J2ME',name:'Java ME',rect:[467,268,320,374],native:[176,208],description:'Nokia 6600 · display backlight'},
    {key:'PSP',name:'PlayStation Portable',rect:[382,356,684,352],native:[480,272],description:'Original startup · Sony wave',width:1448,height:1086}
  ].map(a=>{
    const screen={rect:a.rect,native:a.native,bezel:'#252732'};
    if(a.key==='GG')screen.displayAspect=4/3;
    const out={...a,file:`${a.key}.png`,width:a.width||1254,height:a.height||1254,duration:a.duration||6000,motionBounds:[...a.rect],screen};
    delete out.rect;delete out.native;
    if(a.key==='NDS'){
      out.screens=[screen,{rect:[420,756,412,294],native:[256,192],bezel:'#252732'}];
      delete out.screen;out.motionBounds=[417,230,420,820];
    }
    return out;
  });
  const cache={};
  const glyphs={
    'A':['01110','10001','10001','11111','10001','10001','10001'],
    'B':['11110','10001','10001','11110','10001','10001','11110'],
    'C':['01111','10000','10000','10000','10000','10000','01111'],
    'D':['11110','10001','10001','10001','10001','10001','11110'],
    'E':['11111','10000','10000','11110','10000','10000','11111'],
    'F':['11111','10000','10000','11110','10000','10000','10000'],
    'G':['01111','10000','10000','10111','10001','10001','01111'],
    'H':['10001','10001','10001','11111','10001','10001','10001'],
    'I':['111','010','010','010','010','010','111'],
    'J':['00111','00010','00010','00010','10010','10010','01100'],
    'K':['10001','10010','10100','11000','10100','10010','10001'],
    'L':['10000','10000','10000','10000','10000','10000','11111'],
    'M':['10001','11011','10101','10101','10001','10001','10001'],
    'N':['10001','11001','11001','10101','10011','10011','10001'],
    'O':['01110','10001','10001','10001','10001','10001','01110'],
    'P':['11110','10001','10001','11110','10000','10000','10000'],
    'Q':['01110','10001','10001','10001','10101','10010','01101'],
    'R':['11110','10001','10001','11110','10100','10010','10001'],
    'S':['01111','10000','10000','01110','00001','00001','11110'],
    'T':['11111','00100','00100','00100','00100','00100','00100'],
    'U':['10001','10001','10001','10001','10001','10001','01110'],
    'V':['10001','10001','10001','10001','10001','01010','00100'],
    'W':['10001','10001','10001','10101','10101','11011','10001'],
    'X':['10001','10001','01010','00100','01010','10001','10001'],
    'Y':['10001','10001','01010','00100','00100','00100','00100'],
    'Z':['11111','00001','00010','00100','01000','10000','11111'],
    '0':['01110','10001','10011','10101','11001','10001','01110'],
    '1':['010','110','010','010','010','010','111'],
    '2':['01110','10001','00001','00010','00100','01000','11111'],
    '3':['11110','00001','00001','01110','00001','00001','11110'],
    '4':['00010','00110','01010','10010','11111','00010','00010'],
    '5':['11111','10000','10000','11110','00001','00001','11110'],
    '6':['01110','10000','10000','11110','10001','10001','01110'],
    '7':['11111','00001','00010','00100','01000','01000','01000'],
    '8':['01110','10001','10001','01110','10001','10001','01110'],
    '9':['01110','10001','10001','01111','00001','00001','01110'],
    ':':['0','1','1','0','1','1','0'],'.':['0','0','0','0','0','1','1'],
    '-':['000','000','000','111','000','000','000'],
    '/':['00001','00001','00010','00100','01000','10000','10000'],
    '&':['01100','10010','10100','01000','10101','10010','01101'],
    ' ':['000','000','000','000','000','000','000']
  };
  function text(c,word,x,y,s=1,color='#15191c'){
    c.fillStyle=color;let px=x;
    for(const ch of word.toUpperCase()){
      const rows=glyphs[ch]||glyphs[' '];
      rows.forEach((row,ry)=>{for(let rx=0;rx<row.length;rx++)if(row[rx]==='1')c.fillRect(Math.round(px+rx*s),Math.round(y+ry*s),Math.max(1,s),Math.max(1,s));});
      px+=(rows[0].length+1)*s;
    }
  }
  function width(word,s=1){return [...word.toUpperCase()].reduce((n,ch)=>n+((glyphs[ch]||glyphs[' '])[0].length+1)*s,0)-s;}
  function centered(c,word,y,s,color){text(c,word,(c.canvas.width-width(word,s))/2,y,s,color);}
  function clear(c,color){c.fillStyle=color;c.fillRect(0,0,c.canvas.width,c.canvas.height);}
  function fade(c,t,end,duration,bg){if(t>end){c.save();c.globalAlpha=clamp((t-end)/(duration-end-100));clear(c,bg);c.restore();}}
  // One-bit geometry traced from the original marks. These rows include no
  // captured background, photographic case pixels, video frames, or firmware.
  const marks = {"gbc":["00000000000000000000000000000000","01ffc07c03e01e0fff01ffc01ff3e03e","01ffc07c03e01e0fff01ffc01ff3e03e","0ffff0fc07e03e0fff01fff07ffdf07c","0ffff0fc07e03e0fff01fff07ffdf07c","3fc0f1fe07f07e1f0001e1f9f83ef9f8","3fc0f1fe07f07e1f0001e1f9f83ef9f8","7e0003fe0ff0fe1f0003e0fbe01e7fe0","7e0003fe0ff0fe1f0003e0fbe01e7fe0","f800079e0ff1fe1ff803fff3c01f3f80","f800079e0ff1fe1ff803fff3c01f3f80","f0000f1f1efbde3ff803ffe7c01f1f00","f0000f1f1efbde3ff803ffe7c01f1f00","f07f9fff3e7fdf3e0007c1f7c01e3e00","f07f9fff3e7fdf3e0007c1f7c01e3e00","f87fbfff3c7f9f3c0007c0f3c03e3e00","f87fbfff3c7f9f3c0007c0f3c03e3e00","7c0f7c1f7c3f0f7c000781f3e0fc3e00","7c0f7c1f7c3f0f7c000781f3e0fc3e00","1ffff80ff83e0ffff80fffe1fff07c00","1ffff80ff83e0ffff80fffe1fff07c00","07fef00ff81c0ffff80fff807fc07c00","07fef00ff81c0ffff80fff807fc07c00","00000000000000000000000000000000","00000000000000000000000000000000"],"pokemon":["0000000000000000000","0000000003800000000","0000000006c00000000","000000000c400000000","0000001c99c3e000000","00e071c77ff32198000","038011863063138fc00","070019c46723138e600","070388c04b201f84000","03834fc0ce603004400","01a15000c0f00620400","01f1a65060304fa0000","007027d030644720000","003863921fc76040000","0038f02387f798d1000","00187863e43f8f91000","000c2f7e3c03fff0000","000e207c00007bf2100","0006e0000000003e010","0003c003fffe003c310","0003007ffffff0003c0","000003ffffffff00180","00000fe019833f80000","00001fe49f83ffc0000","00001fe499933fc0000","00000fe499933f80000","00000fe499933f80000","000001fffffffe00000","00000003ffff0000000","0000000000000000000","0000000000000000000","0000007800000000000","0000208400000000000","1c630142ec00e000600","0c6b5572dcb6fef9e68","0c63494addb6dcdb66c","0c633484cdb6c648e38","0000007800000000000"],"neo":["000000000000000000000000000000000000000","0003f80f001c07f81e0000000e003e180dfeffe","301bf83f807f05f0778000003bc077981b000c0","301a0070c0e18c00c1c0000060e1c1d833000c0","381a00c06180cc0180400000c031807863000c0","2c1a008033006c010060000180330018c3000c0","261a018013000403002000018012001983000c0","221bf9001a0007f200300003001a001b03fe0c0","231bf9000a1fe7f200300003000a001f03fc0c0","218a03001a1ff40200300003001a000f03000c0","20ca01801b002403002000018013000983000c0","204a0080330064010060000080330068e3000c0","207a00c03180c40180c00000c06180c873000c0","203a0070e0c18400e380000071c0e38839000c0","201bf83fc07707f87f0000001f003e081dfe0c0","201bf80e001e07f01c0000000e00000001fc000","000000000000000000000000000000000000000","000000000000000000000000000000000000000","000000000000000000000000000000000000fa2","000000000000000000000000000000000000f66","000000000000000000000000000000000000676","00000000000000000000000000000000000067e","00000000000000000000000000000000000066e","000000000000000000000000000000000000000","000000000000000000000000000000000000000"],"neoSpiral":["00000000000","00000000000","00000f80000","00001f80000","00000f87000","00000f0fc00","00000f0fc00","00000f1f800","00000f1f000","00000e3e0c0","00000e3e0e0","00000e7c1f0","000000783f0","000000707f0","00000020fc0","0001f001f80","0003fc03f00","003fff03c00","00ffffc1800","03fe1fe0000","0ff807e0000","3fe0fbf0000","3f87f9f0000","1f07f9f0000","1f07f9f0000","0f8fb1f0000","0f8f81f0000","07cf01f0000","07df01f0000","03ff83e0000","01efe7e0000","01f7ffc0000","01f1ff88000","00f8ff10000","00f87e20000","007c3c08000","007c0040000","003e0000000","003e0000000","001f0000000","001f0000000","000f8000000","000f8000000","0007c000000","0007c000000","0003e000000","0003c000000","00010000000"],"snk":["000000000","1fe600018","3ff70ee7c","3ff78eef0","3807ceff0","3fe7fefe0","1ff7fefe0","00f77eef0","3ff73ee78","3fe71ee3c","000000000"],"bandaiBox":["0ffffffffff0","3ffffffffff8","7ffffffffffc","7ffffffffffc","ffffffffffff","ffffffffffff","ffffffffffff","ffffffffffff","ffffffffffff","f801fc3f1f3f","f801fc3e1f3f","f801fc3e0f3f","f8f8f89f0f3f","f8fcf88e073f","f8f9f98f073f","f8c1f1c7033f","f8c1f1c7023f","f8c1f1c7303f","f8f8e3e6383f","f8fc63e7383f","f8f863e33c3f","f80040733c3f","f800c0733e3f","f803c0733e3f","ffffffffffff","f801fc3e3fff","f801fc3f3fff","f800fc3f3fff","f8f8789f3fff","f8fc799e3fff","f8fc718f3fff","f8fc71c73fff","f8fc71c73fff","f8fc71c73fff","f8fc63e63fff","f8fc63e73fff","f8f8e3e33fff","f801c0723fff","f801c0723fff","fcc3e0fbffff","ffffffffffff","ffffffffffff","ffffffffffff","ffffffffffff","7ffffffffffe","7ffffffffffc","3ffffffffff8","0fffffffffe0"],"bandaiLetters":["f0000000000f","c00000000007","800000000003","800000000003","000000000001","000000000001","000000000001","000000000001","000000000001","07fe03c1e0c1","07ff03c1e0c1","07ff03c1f0c1","070787f1f0c1","07038671f8c1","07070671f8c1","073f0e39fec1","073e0e39dec1","073f0e39dfc1","07079c19c7c1","07039c19c7c1","07079c19c3c1","07ffbfccc7c1","07ff3fccc1c1","07fe3f8cc1c1","000000000001","07fe03c1c001","07ff03c1c001","07ff03c0c001","07078670c001","07038671c001","07038e71c001","07038e39c001","07038e39c001","07038e39c001","07039c19c001","07039c19c001","07079c1dc001","07ff3fcdc000","07fe3fccc001","07fc3f8cc000","000000000001","000000000000","000000000001","000000000001","800000000003","800000000003","c00000000007","f0000000001f"],"wscName":["611f10df0fe7c3e21861847c3c601f3f0","619fb8df8fefe3f318f18cfc7e603f3f8","6bb1bcd8cc0e36135999ccc6c36071b1c","6bb1bed8ec0e76035b0decc0c36071b1c","6bb1b3d8efcfe3e35b8dbcc0c36071bf8","7fb1b1d8ec0ec03bfbfd9cc0c36071b60","3731b0d9cc0e6639b30d8cc6c36031b18","231f30df0fee33e1130d8c7c7e7f1f31c","000000000000000000000000000000000"],"psp":["000000000000000000000000000000000000000000000000000000","000000000000000000000000000000000000000000000000000000","1f000000007c00000000000000007f000000000006000000000000","2180000000c7000000000000000062000000008000000000000000","208000000183000000000018000040001800018000000000000060","300fcfcc31003f3fbcfe61bc7cf8607f3efcffe7e6de7f78f8fcf8","1f184c64210021b1c6c36198c6407e631886c18426c2638d8cc660","07d86c26610061b086c36198c64060611186c08026c3618d8c8660","00d82c26618060b086c36198de40406110f8c08fa6c3610d848660","60d86c23c18261b086c36198c0c040619080c08c36c3610d808620","3f8ccc2180ff333086c7338cc6c07e619cccc0cc26c3610c888630","0f030021803c180080cc180c1c403e010c70006300000100108010","000000010000000000c00000000000000000000000000000000000"],"nds":["000000000000000000000000000000000000000000000000","000000000000000000000000000000000000000000000000","380618e01cfffcfff1c038fff03ffe3fffffc00007fff800","3c0738f01cfffcfff1e038fff87fff3ffffff8003ffff800","3e0738f81c7ff8fff1f038fffcffff3ffffffe00fffff800","3f0738fc1c0380e001f838e01ce0073fffffff81fffff800","3f8738ee1c0380e001fc38e00ce0073fe007ffc3fe003800","3bc738e71c0380ffc1ce38e00ee0073fe000ffe3fc000000","39e738e39c0380ffc1c738e00ee0073fe0007fe3fc000000","38f738e1dc0380e001c3b8e00ee0073fe0003ff3fe000000","387f38e0fc0380e001c1f8e00ce0073fe0001ffbff000000","383f38e07c0380e001c0f8e01ce0073fe0001ff9ffe00000","381f38e03c0380fff1c078fff87fff3fe0000ff9fff80000","380f38e01c0380fff1c038fff87ffe3fe0000ff87fff0000","300618600c0300fff0c018ffe03ffc3fe0000ff83fffc000","0000000000000000000000000000003fe0000ff80ffff000","000000000000000000000000003ffc3fe0000ff801fffc00","000000000000000000000000007ffe3fe0000ff8007ffe00","0000000000000000000000000060033fe0001ff8000ffe00","0000000000000000000000000060033fe0001ff80003ff00","0000000000000000000000000060033fe0003ff00001ff00","0000000000000000000000000060033fe0007ff00001ff00","0000000000000000000000000060033fe000ffe00001ff00","0000000000000000000000000060033fe003ffc38003fe00","0000000000000000000000000060033fffffff83fffffe74","0000000000000000000000000060033ffffffe03fffffc5c","000000000000000000000000007ffe3ffffffc03fffff05e","000000000000000000000000003ffe3fffffe003ffffc000","0000000000000000000000000000001fffe0000007e00000","000000000000000000000000000000000000000000000000"]};
  const markPixels = Object.fromEntries(Object.entries(marks).map(([key,rows])=>[key,rows.flatMap((row,y)=>[...row].flatMap((hex,j)=>{
    const n=parseInt(hex,16);return [0,1,2,3].filter(b=>n&(8>>b)).map(b=>[j*4+b,y]);
  }))]));
  markPixels.bandaiLetters=markPixels.bandaiLetters.filter(([x,y])=>x>=4&&x<=44&&y>=7&&y<=42);
  function mark(c,key,x,y,color,scale=1){c.fillStyle=color;for(const [px,py] of markPixels[key])c.fillRect(Math.round(x+px*scale),Math.round(y+py*scale),scale,scale);}
  function gbc(c,t,env){
    clear(c,'#eeeef0');if(t<300||t>5700)return;
    env.nintendo(c,56,104,1,'#141525');
    const logo=color=>mark(c,'gbc',16,47,color);
    if(t<1700){const cover=clamp((t-300)/1400)*166;
      c.save();c.beginPath();c.rect(0,0,cover,144);c.clip();logo('#1820aa');c.restore();
    }else logo('#1820aa');
    if(t<3800){const x=(t-900)/1900*210-40;
      const colors=['#d92841','#edbd18','#29a53e','#0fabc7','#3536a7'];
      colors.forEach((color,i)=>{c.save();c.beginPath();c.rect(x+i*12,45,12,40);c.clip();logo(color);c.restore();});
    }
    fade(c,t,5300,6000,'#eeeef0');
  }
  function backlight(c,t){
    // Neither selected launch-model Game Gear nor Lynx II runs a universal
    // animated brand screen before a cartridge. This is the LCD/backlight only.
    const on=t>=500&&t<5100;
    clear(c,on?'#181d2a':'#444752');
    if(on){const g=c.createLinearGradient(0,0,0,c.canvas.height);g.addColorStop(0,'#1a2030');g.addColorStop(1,'#222a3b');c.fillStyle=g;c.fillRect(0,0,c.canvas.width,c.canvas.height);}
  }
  function seven(c,digit,x,y,s){
    const on={0:[0,1,2,4,5,6],1:[2,5],2:[0,2,3,4,6],3:[0,2,3,5,6],4:[1,2,3,5],5:[0,1,3,5,6],6:[0,1,3,4,5,6],7:[0,2,5],8:[0,1,2,3,4,5,6],9:[0,1,2,3,5,6]}[digit];
    const shapes=[[2,0,10,2],[0,2,2,10],[12,2,2,10],[2,12,10,2],[0,14,2,10],[12,14,2,10],[2,24,10,2]];
    for(let i=0;i<7;i++){c.fillStyle=on.includes(i)?'#303c29':'#a1aa86';const r=shapes[i];c.fillRect(x+r[0]*s,y+r[1]*s,r[2]*s,r[3]*s);}
  }
  function watch(c,t){
    clear(c,'#abb58e');[1,2,0,0].forEach((d,i)=>seven(c,d,31+i*23+(i>1?4:0),19,1.15));
    c.fillStyle=Math.floor(t/500)%2?'#303c29':'#a1aa86';c.fillRect(80,27,3,3);c.fillRect(80,40,3,3);
    // Ball's watch mode keeps the segmented character at the bottom.
    c.fillStyle='#303c29';c.fillRect(74,66,10,8);c.fillRect(70,74,18,5);c.fillRect(74,79,10,9);c.fillRect(67,88,10,3);c.fillRect(82,88,10,3);
    c.fillRect(65,76,6,10);c.fillRect(88,76,6,10);
  }
  function nds(c,t,index,env){
    clear(c,'#fcfcfc');if(t<300||t>5800)return;
    if(index===0){
      c.save();c.globalAlpha=clamp((t-300)/500);
      mark(c,'nds',32,77,'#25272a');
      env.nintendo(c,104,143,1,'#25272a');
      c.strokeStyle='#25272a';c.lineWidth=1;c.beginPath();c.roundRect(99,140,61,14,7);c.stroke();c.restore();
    }else{
      c.fillStyle='#1c1e21';c.beginPath();c.moveTo(17,20);c.lineTo(26,4);c.lineTo(35,20);c.fill();
      c.fillStyle='#fff';c.fillRect(25,9,2,6);c.fillRect(25,17,2,2);
      centered(c,'WARNING - HEALTH AND SAFETY',26,1,'#202226');
      ['BEFORE PLAYING, READ THE HEALTH','AND SAFETY PRECAUTIONS BOOKLET','FOR IMPORTANT INFORMATION','ABOUT YOUR HEALTH AND SAFETY.'].forEach((s,i)=>centered(c,s,52+i*12,1,'#202226'));
      centered(c,'WWW.NINTENDO.COM/HEALTHSAFETY/',113,1,'#202226');
      if(t>1200&&Math.floor(t/650)%2===0){centered(c,'TOUCH THE TOUCH SCREEN',151,1,'#202226');centered(c,'TO CONTINUE.',163,1,'#202226');}
    }
    fade(c,t,5500,6000,'#fcfcfc');
  }
  function neo(c,t,color){
    const bg=color?'#f5f5f3':'#b5bca1',ink=color?'#121318':'#293b30';
    clear(c,t<2700?'#111414':bg);if(t<350||t>6750)return;
    if(t<2700){
      const spans=[[0,12],[12,23],[23,38],[38,52],[52,64],[64,78]];
      for(let i=0;i<6;i++){
        const p=clamp((t-350-i*230)/750);if(p<=0)continue;
        c.save();c.translate(40+i*14,74);c.rotate((1-p)*Math.PI*1.5);c.scale(.4+.6*p,.4+.6*p);
        c.fillStyle=color?'#e9eaeb':'#acb6a2';
        for(const [x,y] of markPixels.neo)if(y<17&&x>=spans[i][0]&&x<spans[i][1])c.fillRect(x-spans[i][0]-6,y-8,1,1);
        c.restore();
      }
    }else{
      mark(c,'neo',2,65,ink);mark(c,'neoSpiral',71,35,color?'#d42938':'#7d8d6d');
      mark(c,'snk',62,111,color?'#144aa5':ink);
      if(t<2880){c.save();c.globalAlpha=1-(t-2700)/180;clear(c,'#fff');c.restore();}
    }
    fade(c,t,6350,7000,bg);
  }
  function pokemon(c,t){
    clear(c,'#a1b29c');if(t<500||t>5700)return;
    c.save();c.globalAlpha=clamp((t-500)/350);mark(c,'pokemon',10,10,'#253a29');c.restore();
    fade(c,t,5200,6000,'#a1b29c');
  }
  function bandai(c,t,color){
    const bg=color?'#f3f3f3':'#a8b49b',ink=color?'#151515':'#2e3b2b';
    if(color&&t<1900){const shade=Math.round(clamp((t-1200)/650)*243);clear(c,`rgb(${shade},${shade},${shade})`);}
    else clear(c,bg);if(t<350||t>5750)return;
    if(color&&t<1300){
      const letter=Math.floor((t-350)/150);
      const boxes=[[4,8,14,17],[18,8,13,17],[31,8,13,17],[4,25,14,17],[18,25,13,17],[31,25,8,17]];
      const r=boxes[Math.min(5,letter)];
      c.save();c.beginPath();c.rect(88+r[0],29+r[1],r[2],r[3]);c.clip();
      mark(c,'bandaiLetters',88,29,['#14a521','#d8b147','#23bea9','#d00043','#202d92','#a72c91'][Math.min(5,letter)]);c.restore();
    }else{
      c.save();c.globalAlpha=color?clamp((t-1200)/500):clamp((t-350)/700);
      mark(c,'bandaiBox',88,29,color?'#cc1436':ink);
      mark(c,'bandaiLetters',88,29,bg);
      c.restore();
      if(t>1500){
        if(color)mark(c,'wscName',46,103,ink);
        else{c.save();c.beginPath();c.rect(69,103,86,9);c.clip();mark(c,'wscName',69,103,ink);c.restore();}
      }
    }
    fade(c,t,5350,6000,bg);
  }
  function phone(c,t){
    // Java ME specifies no hardware startup. The representative Nokia 6600
    // keeps its S60 idle display while its backlight turns on, then dims.
    const light=t>=500&&t<4700;clear(c,light?'#d7e3ea':'#8f9c9f');
    c.fillStyle='#234253';for(let i=0;i<4;i++)c.fillRect(9+i*4,20-i*4,3,4+i*4);
    c.strokeStyle='#234253';c.lineWidth=2;c.strokeRect(146,7,20,10);c.fillRect(167,10,3,4);c.fillRect(149,10,13,4);
    centered(c,'12:00',42,3,'#234253');centered(c,'01.01.2004',72,1,'#234253');
    c.fillStyle='#c6d8e1';c.fillRect(14,104,148,60);c.strokeStyle='#8cabbc';c.strokeRect(14,104,148,60);
    centered(c,'MENU',130,2,'#234253');text(c,'OPTIONS',5,194,1,'#234253');text(c,'CONTACTS',124,194,1,'#234253');
  }
  function psp(c,t){
    clear(c,'#020204');if(t<250||t>5700)return;
    if(t<2500){
      const y=141;
      for(let i=0;i<4;i++){c.strokeStyle=`rgba(133,111,181,${.12+i*.04})`;c.lineWidth=1;c.beginPath();c.moveTo(0,y+i);c.bezierCurveTo(140,y-7+i,300,y+5+i,480,y-3+i);c.stroke();}
      c.save();c.globalAlpha=clamp((t-250)/400)*clamp((2350-t)/350);mark(c,'psp',133,128,'#f4f3f8');c.restore();
    }else{
      const g=c.createLinearGradient(0,272,480,0);g.addColorStop(0,'#080078');g.addColorStop(.52,'#5721e8');g.addColorStop(1,'#f7baf7');c.fillStyle=g;c.fillRect(0,0,480,272);
      for(let n=0;n<6;n++){
        c.beginPath();c.moveTo(0,272);for(let x=0;x<=480;x+=3)c.lineTo(x,182+Math.sin(x/125-t/1800+n*.055)*25+n*4);c.lineTo(480,272);c.closePath();
        c.fillStyle=`rgba(${170+n*10},${115+n*14},255,${.12-n*.01})`;c.fill();
      }
    }
    fade(c,t,5300,6000,'#020204');
  }
  function getScreen(key,time,env,index=0){
    const asset=assets.find(a=>a.key===key);if(!asset)return null;
    const spec=asset.screens?asset.screens[index]:asset.screen;
    const id=key+':'+index;
    const target=cache[id]||(cache[id]=env.canvas(...spec.native));
    const c=target.getContext('2d');c.setTransform(1,0,0,1,0,0);c.globalAlpha=1;c.globalCompositeOperation='source-over';c.imageSmoothingEnabled=false;
    const t=((time%asset.duration)+asset.duration)%asset.duration;
    if(key==='GBC')gbc(c,t,env);
    else if(key==='GG'||key==='LYNX')backlight(c,t);
    else if(key==='GW')watch(c,t);
    else if(key==='NDS')nds(c,t,index,env);
    else if(key==='NGP'||key==='NGPC')neo(c,t,key==='NGPC');
    else if(key==='PKMINI')pokemon(c,t,env);
    else if(key==='WS'||key==='WSC')bandai(c,t,key==='WSC');
    else if(key==='J2ME')phone(c,t);
    else if(key==='PSP')psp(c,t);
    return target;
  }
  window.ZyperOriginalGroups ||= [];
  window.ZyperOriginalGroups.push({id:'handhelds',assets,extraImages:[],render(){},getScreen});
})();
