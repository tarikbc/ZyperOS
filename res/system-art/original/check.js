// Run through the Playwright browser_run_code tool against index.html.
async page => {
  await page.evaluate(() => window.artReview.ready);
  await page.evaluate(() => ZyperSystemArt.load('source/'));
  const frames = await page.evaluate(() => {
    const allowed = {
      GB:[94,78,153,127], GBA:[83,100,173,153], PS1:[90,4,205,133],
      '3DO':[62,115,164,150], SNES:[110,22,195,123]
    };
    return ZyperSystemArt.keys.map(key => {
      const canvas=document.createElement('canvas');canvas.width=canvas.height=256;
      const context=canvas.getContext('2d',{willReadFrequently:true});
      const pixels=time=>{ZyperSystemArt.render(canvas,key,time,true);return context.getImageData(0,0,256,256).data;};
      const first=pixels(0),duration=ZyperSystemArt.durationFor(key);
      let changed=0,outside=0,border=0;
      let bounds=allowed[key];
      if(!bounds){
        const [x,y,w,h]=ZyperSystemArt.motionRegion(key);
        bounds=[x,y,x+w,y+h];
      }
      for(const fraction of [.1,.2,.3,.45,.8,.85]){
        const current=pixels(duration*fraction),[x0,y0,x1,y1]=bounds;
        for(let p=0;p<256*256;p++){
          const x=p%256,y=Math.floor(p/256);
          if([0,1,2,3].some(k=>first[4*p+k]!==current[4*p+k])){
            changed++;
            if(!(x>=x0&&x<x1&&y>=y0&&y<y1))outside++;
          }
          if((x===0||x===255||y===0||y===255)&&current[4*p+3]>127)border++;
        }
      }
      const last=pixels(duration),closedLoop=first.every((value,i)=>value===last[i]);
      const isStatic=ZyperSystemArt.profiles[key]?.static;
      if((!changed&&!isStatic)||(changed&&isStatic)||outside||border||!closedLoop)throw new Error(`${key}: ${JSON.stringify({changed,outside,border,closedLoop})}`);
      return {key,changedPixels:changed,changedOutsideMotion:outside,clippedPixels:border,closedLoop};
    });
  });
  for(const key of ['GB','3DO','SNES','GBA','PS1']){
    await page.locator('[data-system]').selectOption(key);
    const label=await page.locator('[data-stage]').getAttribute('aria-label');
    if(!label.includes(await page.locator('[data-system] option:checked').textContent()))throw new Error('Selection label did not update');
  }
  await page.evaluate(()=>window.artReview.show('PS1',2700));
  const before=await page.locator('[data-stage]').evaluate(c=>c.toDataURL());
  await page.waitForTimeout(150);
  const after=await page.locator('[data-stage]').evaluate(c=>c.toDataURL());
  if(before!==after)throw new Error('Paused preview moved');
  await page.getByRole('button',{name:'Play',exact:true}).click();
  if(await page.locator('[data-play]').textContent()!=='Pause')throw new Error('Play control failed');
  await page.locator('[data-dark]').check();
  if(!await page.locator('#system-art-review').evaluate(n=>n.classList.contains('art-dark')))throw new Error('Dark background control failed');
  await page.setViewportSize({width:350,height:800});
  const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);
  if(overflow)throw new Error('Narrow preview overflows');
  await page.emulateMedia({reducedMotion:'reduce'});
  await page.reload();await page.evaluate(()=>window.artReview.ready);
  if(await page.locator('[data-play]').textContent()!=='Play')throw new Error('Reduced motion did not pause');
  await page.emulateMedia({reducedMotion:'no-preference'});
  await page.setViewportSize({width:800,height:800});
  await page.reload();await page.evaluate(()=>window.artReview.ready);
  return {frames,controls:'passed',narrowLayout:'passed',reducedMotion:'passed'};
}
