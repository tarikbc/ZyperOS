/* Render the approved utility drawings at their actual UI sizes. */
(() => {
  const canvas=(w,h)=>{const c=document.createElement('canvas');c.width=w;c.height=h;return c;};
  const record=(file,c)=>({file,width:c.width,height:c.height,png:c.toDataURL('image/png')});
  function cover(icon,dark){
    const c=canvas(512,656),x=c.getContext('2d');x.imageSmoothingEnabled=false;
    x.beginPath();x.roundRect(0,0,512,656,22);x.clip();
    x.fillStyle=dark?'#171b18':'#edece4';x.fillRect(0,0,512,656);
    x.fillStyle='#ff6fae';x.fillRect(0,0,512,6);
    if(dark){
      x.fillStyle='#d9e1dd';x.fillRect(399,52,40,8);x.fillRect(391,60,48,32);x.fillRect(399,92,40,8);
      x.fillStyle='#171b18';x.fillRect(415,52,24,32);
      x.fillStyle='#95aaa4';for(const [a,b]of [[62,62],[225,40],[313,80],[459,147]])x.fillRect(a,b,4,4);
    }else{
      x.fillStyle='#f6cf69';x.fillRect(392,52,48,48);x.fillRect(384,60,64,32);x.fillRect(400,44,32,64);
      x.fillStyle='#ffe399';x.fillRect(400,60,32,32);
    }
    x.fillStyle=dark?'#183e4c':'#3a83a7';x.fillRect(0,510,512,146);
    x.fillStyle=dark?'#2b5867':'#6bacc3';
    for(const [a,b,w]of [[24,542,56],[166,568,64],[346,547,48],[426,603,58],[70,621,70],[258,632,42]])x.fillRect(a,b,w,6);
    x.drawImage(icon,0,48,512,512);
    return c;
  }
  async function exportAll(){
    const art=window.ZyperSystemArt;
    await art.load('source/',['PORTMASTER','FOLDER','FOLDER_SMALL']);
    const images=[];
    const icon=canvas(256,256);art.render(icon,'PORTMASTER',0,true);
    images.push(record('portmaster-icon.png',icon));
    images.push(record('portmaster-cover-light.png',cover(icon,false)));
    images.push(record('portmaster-cover-dark.png',cover(icon,true)));
    const folder=canvas(256,256);art.render(folder,'FOLDER',0,true);
    images.push(record('folder-large.png',folder));
    for(const size of [24,32,40,48,64]){
      const small=canvas(size,size);art.render(small,'FOLDER_SMALL',0,true);
      images.push(record(`folder-small-${size}.png`,small));
    }
    return {schema:1,images,sourceFiles:['source/PORTMASTER.png','source/FOLDER.png','source/FOLDER_SMALL.png'],
      rendererFiles:['animation.js','groups/utility.js','utility-export.js'],
      folderLabelAt256:{x:78,y:128,width:88,height:27}};
  }
  window.ZyperUtilityExporter={exportAll};
})();
