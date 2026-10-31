#pragma once
#include <d3dcompiler.h>
static ComPtr<ID3D11VertexShader> keyboardVs;
static ComPtr<ID3D11PixelShader> keyboardPs;
static ComPtr<ID3D11Buffer> keyboardKeysBuffer;
static ID3D11Device* keyboardShaderDevice=nullptr;
// Release device-owned resources before FreeLibrary enters the loader lock.
static void releaseKeyboardLayout(){keyboardVs.Reset();keyboardPs.Reset();keyboardKeysBuffer.Reset();keyboardShaderDevice=nullptr;}
// Preview-only UV remapping. The stored atlas and every model vertex remain unchanged.
static ComPtr<ID3D11ShaderResourceView> keyboardLayout(ID3D11Device* dev,ID3D11ShaderResourceView* original,UINT w,UINT h) {
 std::string shaderText=R"(
 Texture2D tex:register(t0); SamplerState samp:register(s0);
 cbuffer PressedKeys:register(b0){float4 keyRects[64];float4 keyStates[64];float4 keyCount;};
 struct V {float4 p:SV_Position;};
 V vs(uint id:SV_VertexID){V o;float2 p=float2((id<<1)&2,id&2);o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;}
 float cross2(float2 a,float2 b){return a.x*b.y-a.y*b.x;}
 bool inside(float2 p){float2 a=float2(110,534),b=float2(440,544),c=float2(432,650),d=float2(79,642);return cross2(b-a,p-a)>=0&&cross2(c-b,p-b)>=0&&cross2(d-c,p-c)>=0&&cross2(a-d,p-d)>=0;}
 float4 ps(V i):SV_Target{
 float2 p=i.p.xy;
 float2 originalPos=(p-float2(260,592)-float2(-27,-7.2))/.85+float2(260,592);
 if(inside(originalPos)){
 float4 col=tex.Sample(samp,originalPos/2048);
 float2 k=float2(originalPos.x,originalPos.y-(originalPos.x-117)*.030);
 // A single continuous inverse warp replaces the old cropped face overlay.
 // Include cap bevels; fade displacement outside the cap, without mixing in
 // an unmoved copy of its edges or artwork. Every pixel is sampled once.
 float depression=0;float signal=0;
 for(int n=0;n<(int)keyCount.x;n++){
 float4 r=keyRects[n];float depth=keyStates[n].x;
 float2 outside=max((r.xy-float2(4,2))-k,k-(r.zw+float2(4,5)));
 float distance=max(outside.x,outside.y);
 float influence=1-smoothstep(0,3.5,max(0,distance));
 depression=max(depression,depth*influence);if(k.x>r.x&&k.x<r.z&&k.y>r.y&&k.y<r.w)signal=max(signal,keyStates[n].y);
 }
 // 2.2 px travel / 3.5 px falloff keeps the inverse map monotone even at
 // maximum press (max smoothstep slope 1.5), preventing folds and cut lines.
 float travel=2.2*depression;
 float2 source=originalPos-float2(0,travel);
 col=tex.Sample(samp,source/2048);
 col.rgb*=1-.10*depression;col.rgb=lerp(col.rgb,float3(.95,.66,.26),.20*signal);
 return col;}
 if(inside((p-float2(260,592))/1.10+float2(260,592))){
 // Clean tabletop samples exclude the brass trim and old keyboard shadow.
 float2 samplePos=float2(463+5*sin(p.x*.018),clamp(p.y,542,650));
 float4 fill=tex.Sample(samp,samplePos/2048);
 float4 neighbor=tex.Sample(samp,(samplePos+float2(4,0))/2048);
 fill=lerp(fill,neighbor,.5);
 float2 q=(p-float2(260,592))/1.10+float2(260,592);
 float dist=min(min(cross2(float2(330,10),q-float2(110,534))/330,cross2(float2(-8,106),q-float2(440,544))/106),min(cross2(float2(-353,-8),q-float2(432,650))/353,cross2(float2(31,-108),q-float2(79,642))/112));
 return lerp(tex.Sample(samp,p/2048),fill,smoothstep(0,3,dist));}
 return tex.Sample(samp,p/2048);
 })";
 const char* shader=shaderText.c_str();
 auto& vs=keyboardVs;auto& ps=keyboardPs;auto& keysBuffer=keyboardKeysBuffer;
 if(keyboardShaderDevice!=dev){releaseKeyboardLayout();keyboardShaderDevice=dev;}
 if(!vs){ComPtr<ID3DBlob> vsb,psb,error;
 check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vsb,&error),"Layout vertex shader");
 check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&psb,&error),"Layout pixel shader");
 check(dev->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs),"Layout VS");
 check(dev->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps),"Layout PS");
 D3D11_BUFFER_DESC cb{};cb.ByteWidth=129*16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(dev->CreateBuffer(&cb,nullptr,&keysBuffer),"Input constants");}
 D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 ComPtr<ID3D11Texture2D> texture;check(dev->CreateTexture2D(&td,nullptr,&texture),"Layout texture");
 ComPtr<ID3D11RenderTargetView> rt;check(dev->CreateRenderTargetView(texture.Get(),nullptr,&rt),"Layout target");
 ComPtr<ID3D11ShaderResourceView> result;check(dev->CreateShaderResourceView(texture.Get(),nullptr,&result),"Layout resource");
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
 ComPtr<ID3D11SamplerState> ss;check(dev->CreateSamplerState(&sd,&ss),"Layout sampler");
 ComPtr<ID3D11DeviceContext> ctx;dev->GetImmediateContext(&ctx);ctx->ClearState();
 float keys[129][4]={};int count=0;
 for(auto& k:keyRegions){keys[count][0]=k.x;keys[count][1]=k.y;keys[count][2]=k.x+k.w;keys[count][3]=k.y+k.h;keys[64+count][0]=keyDepth[k.vk];keys[64+count][1]=(held[k.vk]||keyImpulse[k.vk]>0)?1.f:0.f;++count;}
 keys[128][0]=float(count);ctx->UpdateSubresource(keysBuffer.Get(),0,nullptr,keys,0,0);ctx->PSSetConstantBuffers(0,1,keysBuffer.GetAddressOf());
 ctx->OMSetRenderTargets(1,rt.GetAddressOf(),nullptr);D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};ctx->RSSetViewports(1,&vp);
 ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->PSSetSamplers(0,1,ss.GetAddressOf());ctx->PSSetShaderResources(0,1,&original);ctx->Draw(3,0);
 ctx->ClearState();return result;
}
