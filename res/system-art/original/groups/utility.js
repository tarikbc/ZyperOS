/* New utility artwork. The folder sources have separate large/list designs. */
(() => {
  const assets=[
    {key:'PORTMASTER',name:'PortMaster',file:'PORTMASTER.png',width:1254,height:1254,
      description:'Crane lowers a game cartridge',duration:800,poseCount:2,kind:'utility',motionBounds:[600,350,290,392]},
    {key:'FOLDER',name:'Folder · large',file:'FOLDER.png',width:1254,height:1254,
      description:'Large folder with space for the game count',duration:1000,poseCount:1,static:true,kind:'utility',motionBounds:[0,0,1254,1254]},
    {key:'FOLDER_SMALL',name:'Folder · list',file:'FOLDER_SMALL.png',width:1261,height:1247,
      description:'Simple folder for list rows',duration:1000,poseCount:1,static:true,kind:'utility',motionBounds:[0,0,1261,1247]}
  ];
  function part(c,image,sx,sy,sw,sh,dx,dy,dw,dh){
    c.drawImage(image,sx/1254*image.width,sy/1254*image.height,
      sw/1254*image.width,sh/1254*image.height,dx,dy,dw,dh);
  }
  window.ZyperOriginalGroups ||= [];
  window.ZyperOriginalGroups.push({id:'utility',assets,extraImages:[],
    render(c,key,time,env){
      if(key!=='PORTMASTER'||Math.floor(time/400)%2===0)return;
      c.clearRect(600,350,290,392);
      // Extend the rope along its fixed vertical axis, then lower the load.
      part(c,env.images.PORTMASTER,735,350,26,105,735,350,26,135);
      part(c,env.images.PORTMASTER,609,451,271,260,609,481,271,260);
    },getScreen(){return null;}
  });
})();
