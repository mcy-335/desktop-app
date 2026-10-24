// Build-time asset baking only. The shipped C executable has no JS/Electron dependency.
const {app,BrowserWindow,ipcMain}=require('electron');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),out=path.resolve(__dirname,'../assets');
app.setPath('userData',path.resolve(__dirname,'../.build/asset-helper'));
ipcMain.handle('pet:get-state',()=>({}));
app.whenReady().then(async()=>{
 const w=new BrowserWindow({show:false,webPreferences:{preload:path.join(root,'src/preload.cjs'),backgroundThrottling:false}});
 try{
  await w.loadFile(path.join(root,'src/index.html'));
  await w.webContents.executeJavaScript('keyboardHandRig.ready');
  fs.mkdirSync(out,{recursive:true});
  const data=await w.webContents.executeJavaScript(`(async()=>{
   clearTimeout(blinkTimer);clearTimeout(expressionTimer);
   const load=src=>new Promise((res,rej)=>{const im=new Image();im.onload=()=>res(im);im.onerror=rej;im.src=src});
   const pack=c=>{const d=c.getContext('2d').getImageData(0,0,c.width,c.height).data;let s='';for(let i=0;i<d.length;i+=8192)s+=String.fromCharCode(...d.subarray(i,i+8192));return {w:c.width,h:c.height,rgba:btoa(s)}};
   const canvas=()=>{const c=document.createElement('canvas');c.width=c.height=1254;return c};
   const erase=new Path2D('M 548 550 L 575 630 L 555 730 L 530 800 L 458 825 L 710 825 L 710 1030 L 105 1030 L 105 810 L 235 800 L 235 720 L 340 670 L 430 630 L 510 570 Z M 730 540 L 830 540 L 900 730 L 975 825 L 985 1030 L 740 1030 L 740 815 L 735 745 L 700 650 Z');
   const result={};let index=0;
   for(const src of Object.values(MASTER_FRAMES)){
    const c=canvas(),ctx=c.getContext('2d');ctx.drawImage(await load(src),0,0,1254,1254);ctx.save();ctx.globalCompositeOperation='destination-out';ctx.fill(erase);ctx.restore();ctx.drawImage(document.querySelector('#keyboard-hand-cleanup'),0,0);result['scene'+index++]=pack(c);
   }
   for(const [name,src]of [['keyboard-arm','../assets/animations/hand-follow/keyboard-arm.png'],['mouse-arm','../assets/animations/mouse-ik-v1/right-mouse-arm.png']]){const c=canvas();c.getContext('2d').drawImage(await load(src),0,0,1254,1254);result[name]=pack(c)}
   const source=await load(MASTER_FRAMES.idle);
   const rects=[[.172,.247,.105,.132],[.85,.355,.092,.135],[.791,.682,.102,.135]];
   let j=0;
   for(const name of ['card-red','card-gray','toy']){
    const r=rects[j],c=document.createElement('canvas');c.width=Math.round(r[2]*1254);c.height=Math.round(r[3]*1254);const ctx=c.getContext('2d');ctx.drawImage(source,r[0]*1254,r[1]*1254,r[2]*1254,r[3]*1254,0,0,c.width,c.height);ctx.globalCompositeOperation='destination-in';ctx.drawImage(await load('../assets/effects/'+name+'-glow-mask.png'),0,0,c.width,c.height);result['object'+j]=pack(c);
    const glow=document.createElement('canvas');glow.width=c.width+40;glow.height=c.height+40;const g=glow.getContext('2d');g.shadowColor=['#ff7457','#b4d7ff','#ffcd5c'][j];g.shadowBlur=12;g.drawImage(c,20,20);result['glow'+j]=pack(glow);j++;
   }
   return result;
  })()`);
  for(const [name,v]of Object.entries(data)){
   const rgba=Buffer.from(v.rgba,'base64'),buf=Buffer.alloc(8+rgba.length);buf.writeUInt32LE(v.w,0);buf.writeUInt32LE(v.h,4);
   for(let i=0;i<rgba.length;i+=4){const a=rgba[i+3];buf[8+i]=Math.round(rgba[i+2]*a/255);buf[9+i]=Math.round(rgba[i+1]*a/255);buf[10+i]=Math.round(rgba[i]*a/255);buf[11+i]=a;}
   fs.writeFileSync(path.join(out,name+'.bgra'),buf);
  }
  console.log('Prepared '+Object.keys(data).length+' independent C-runtime assets.');app.exit(0);
 }catch(e){console.error(e);app.exit(1)}
});
