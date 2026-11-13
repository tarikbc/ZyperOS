/* Original hardware illustrations. See consoles.json for provenance and checks. */
(() => {
  const assets = [
  {
    "key": "32X",
    "name": "Sega 32X",
    "file": "32X.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      729,
      672,
      17,
      15
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        729,
        672,
        17,
        15
      ],
      "color": "#ff3434",
      "off": "#3d1821"
    }
  },
  {
    "key": "SATURN",
    "name": "Sega Saturn",
    "file": "SATURN.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      450,
      601,
      17,
      12
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        450,
        601,
        17,
        12
      ],
      "color": "#6fe877",
      "off": "#18351e"
    }
  },
  {
    "key": "A2600",
    "name": "Atari 2600",
    "file": "A2600.png",
    "width": 1254,
    "height": 1254,
    "description": "Power control",
    "duration": 6000,
    "motionBounds": [
      329,
      450,
      36,
      70
    ],
    "motion": {
      "type": "toggle",
      "rect": [
        329,
        450,
        36,
        70
      ],
      "offCenter": [
        344,
        501
      ],
      "onCenter": [
        350,
        465
      ],
      "cap": [
        22,
        18
      ]
    }
  },
  {
    "key": "A5200",
    "name": "Atari 5200",
    "file": "A5200.png",
    "width": 1254,
    "height": 1254,
    "description": "Power control",
    "duration": 6000,
    "motionBounds": [
      888,
      574,
      161,
      55
    ],
    "motion": {
      "type": "press",
      "polygon": [
        [
          900,
          574
        ],
        [
          1041,
          584
        ],
        [
          1024,
          621
        ],
        [
          888,
          610
        ]
      ],
      "offset": [
        0,
        6
      ]
    }
  },
  {
    "key": "A7800",
    "name": "Atari 7800",
    "file": "A7800.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      314,
      640,
      14,
      12
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        314,
        640,
        14,
        12
      ],
      "color": "#ff3636",
      "off": "#441522"
    }
  },
  {
    "key": "DC",
    "name": "Sega Dreamcast",
    "file": "DC.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      651,
      542,
      39,
      32
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        651,
        542,
        39,
        32
      ],
      "polygon": [
        [
          652,
          542
        ],
        [
          688,
          542
        ],
        [
          670,
          572
        ]
      ],
      "color": "#ff9824",
      "off": "#593313"
    }
  },
  {
    "key": "GENESIS",
    "name": "Sega Genesis",
    "file": "GENESIS.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      759,
      652,
      15,
      13
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        759,
        652,
        15,
        13
      ],
      "color": "#ff3838",
      "off": "#3d1821"
    }
  },
  {
    "key": "N64",
    "name": "Nintendo 64",
    "file": "N64.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      665,
      646,
      43,
      13
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        665,
        646,
        43,
        13
      ],
      "color": "#ff363c",
      "off": "#401622"
    }
  },
  {
    "key": "NEOCD",
    "name": "Neo Geo CD",
    "file": "NEOCD.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      621,
      687,
      11,
      10
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        621,
        687,
        11,
        10
      ],
      "color": "#ff3636",
      "off": "#3d1821"
    }
  },
  {
    "key": "NEOGEO",
    "name": "Neo Geo AES",
    "file": "NEOGEO.png",
    "width": 1254,
    "height": 1254,
    "description": "Power control",
    "duration": 6000,
    "motionBounds": [
      200,
      628,
      124,
      34
    ],
    "motion": {
      "type": "slide",
      "rect": [
        200,
        628,
        124,
        34
      ],
      "knob": [
        209,
        628,
        65,
        34
      ],
      "offOffset": [
        44,
        0
      ],
      "onOffset": [
        0,
        0
      ]
    }
  },
  {
    "key": "NES",
    "name": "Nintendo Entertainment System",
    "file": "NES.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      266,
      714,
      19,
      19
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        266,
        714,
        19,
        19
      ],
      "color": "#ff3238",
      "off": "#43151c"
    }
  },
  {
    "key": "PCE",
    "name": "TurboGrafx-16",
    "file": "PCE.png",
    "width": 1254,
    "height": 1254,
    "description": "Power control",
    "duration": 6000,
    "motionBounds": [
      501,
      737,
      111,
      43
    ],
    "motion": {
      "type": "slide",
      "rect": [
        501,
        737,
        111,
        43
      ],
      "knob": [
        503,
        738,
        42,
        40
      ],
      "offOffset": [
        0,
        0
      ],
      "onOffset": [
        64,
        0
      ],
      "slotColor": "#171a25"
    }
  },
  {
    "key": "SEGACD",
    "name": "Sega CD",
    "file": "SEGACD.png",
    "width": 1254,
    "height": 1254,
    "description": "Power and Ready indicators",
    "duration": 6000,
    "motionBounds": [
      715,
      500,
      72,
      286
    ],
    "motion": {
      "type": "indicators",
      "rect": [
        715,
        500,
        72,
        286
      ],
      "lights": [
        {
          "rect": [
            772,
            500,
            13,
            13
          ],
          "color": "#ff3434",
          "off": "#411a24",
          "start": 12
        },
        {
          "rect": [
            716,
            775,
            22,
            11
          ],
          "color": "#5de77e",
          "off": "#203c32",
          "start": 15
        }
      ]
    }
  },
  {
    "key": "SMS",
    "name": "Sega Master System",
    "file": "SMS.png",
    "width": 1254,
    "height": 1254,
    "description": "Power indicator",
    "duration": 6000,
    "motionBounds": [
      722,
      618,
      35,
      20
    ],
    "motion": {
      "type": "indicator",
      "rect": [
        722,
        618,
        35,
        20
      ],
      "polygon": [
        [
          725,
          618
        ],
        [
          756,
          626
        ],
        [
          722,
          637
        ]
      ],
      "color": "#53e773",
      "off": "#153c2b"
    }
  },
  {
    "key": "VB",
    "name": "Virtual Boy",
    "file": "VB.png",
    "width": 1254,
    "height": 1254,
    "description": "Power control",
    "duration": 6000,
    "motionBounds": [
      324,
      876,
      95,
      23
    ],
    "motion": {
      "type": "slide",
      "rect": [
        324,
        876,
        95,
        23
      ],
      "knob": [
        338,
        876,
        50,
        23
      ],
      "offOffset": [
        -12,
        0
      ],
      "onOffset": [
        28,
        0
      ],
      "slotColor": "#ba1e2b"
    }
  },
  {
    "key": "COLECO",
    "name": "ColecoVision",
    "file": "COLECO.png",
    "width": 1254,
    "height": 1254,
    "description": "Power control",
    "duration": 6000,
    "motionBounds": [
      671,
      502,
      39,
      51
    ],
    "motion": {
      "type": "slide",
      "rect": [
        671,
        502,
        39,
        51
      ],
      "knob": [
        671,
        530,
        37,
        22
      ],
      "offOffset": [
        0,
        0
      ],
      "onOffset": [
        2,
        -25
      ],
      "slotColor": "#171c28"
    }
  }
];
  const byKey = Object.fromEntries(assets.map(asset => [asset.key, asset]));
  const rect = (c, color, x, y, w, h) => { c.fillStyle = color; c.fillRect(x,y,w,h); };
  function render(c,key,time,env) {
    const m=byKey[key].motion;
    const frame=Math.floor((((time%6000)+6000)%6000)/6000*64);
    const on=frame>=12 && frame<50;
    c.save();
    c.imageSmoothingEnabled=false;
    if(m.type==='indicators'){
      for(const lamp of m.lights){const lit=frame>=lamp.start&&frame<50;const [x,y,w,h]=lamp.rect;rect(c,lit?lamp.color:lamp.off,x,y,w,h);if(lit)rect(c,'#ffe5bd',x+2,y,Math.max(2,Math.floor(w*.3)),2);}
    }else if(m.type==='indicator'){
      const [x,y,w,h]=m.rect;
      if(m.polygon){env.polygon(c,m.polygon);c.clip();}
      rect(c,on?m.color:m.off,x,y,w,h);
      if(on)rect(c,'#ffe5bd',x+Math.floor(w*.15),y,Math.max(2,Math.floor(w*.30)),Math.max(2,Math.floor(h*.3)));
    }else if(m.type==='toggle'){
      const [x,y,w,h]=m.rect;
      rect(c,'#151724',x,y,w,h);
      const [cx,cy]=on?m.onCenter:m.offCenter;
      const [cw,ch]=m.cap;
      rect(c,'#555967',cx-4,Math.min(cy,y+h/2),8,Math.abs(y+h/2-cy)+8);
      rect(c,'#aaa9ad',cx-cw/2,cy-ch/2,cw,ch);
      rect(c,'#f1ddbd',cx-cw/2,cy-ch/2,cw-5,5);
      rect(c,'#747988',cx+cw/2-5,cy-ch/2+5,5,ch-5);
    }else if(m.type==='slide'){
      const [x,y,w,h]=m.rect;rect(c,m.slotColor||'#171a25',x,y,w,h);
      const [kx,ky,kw,kh]=m.knob;
      const [dx,dy]=on?m.onOffset:m.offOffset;
      c.save();c.translate(dx,dy);env.clipImage(c,key,[[kx,ky],[kx+kw,ky],[kx+kw,ky+kh],[kx,ky+kh]]);c.restore();
    }else if(m.type==='press'){
      const amount=(frame>=10&&frame<=14)||(frame>=49&&frame<=53)?1:0;
      if(amount){
        env.polygon(c,m.polygon);c.fillStyle='#121420';c.fill();
        c.save();env.polygon(c,m.polygon);c.clip();
        c.translate(m.offset[0],m.offset[1]);env.image(c,key);c.restore();
      }
    }
    c.restore();
  }
  window.ZyperOriginalGroups ||= [];
  window.ZyperOriginalGroups.push({id:'consoles',assets,extraImages:[],render,getScreen(){return null;}});
})();
