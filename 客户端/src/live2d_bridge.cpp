#include "../live2d-preview/arm/runtime.hpp"
#include "live2d_bridge.h"

namespace {
struct Runtime {
    struct FrameTarget {
        ComPtr<ID3D11Texture2D> color,staging;
        ComPtr<ID3D11RenderTargetView> view;
        int size=0;
    };
    bool comOwned=false;
    std::unique_ptr<SdkSession> sdk;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    FrameTarget frame,previewFrame;
    ComPtr<IWICImagingFactory> wic;
    std::unique_ptr<Pet> pet;
    std::unique_ptr<Pet> scenePets[2];
    fs::path scenePaths[2];
    ~Runtime() {
        for(auto& character:scenePets)character.reset();
        pet.reset();
        if(context) { context->ClearState(); context->Flush(); }
        releaseKeyboardLayout();
        Rendering::CubismDeviceInfo_D3D11::ReleaseAllDeviceInfo();
        sdk.reset();
        frame={};previewFrame={};
        context.Reset(); device.Reset(); wic.Reset();
        if(comOwned) CoUninitialize();
        logFile.close();
    }
    void resize(FrameTarget& target,int n) {
        if(n==target.size) return;
        if(n<64 || n>2048) throw std::runtime_error("Invalid Live2D canvas size");
        FrameTarget replacement;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=desc.Height=n; desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&desc,nullptr,&replacement.color),"Offscreen texture");
        check(device->CreateRenderTargetView(replacement.color.Get(),nullptr,&replacement.view),"Offscreen target");
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&desc,nullptr,&replacement.staging),"Readback texture");
        replacement.size=n;target=std::move(replacement);
    }
    void initialize(const wchar_t* resources,int n,bool software) {
        HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        comOwned=SUCCEEDED(hr);
        if(FAILED(hr) && hr!=RPC_E_CHANGED_MODE) check(hr,"COM initialize");
        sdk=std::make_unique<SdkSession>();
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC factory");
        if(!software)
            hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(software || FAILED(hr)) {
            log(software ? "Using WARP for Live2D self-test" : "Hardware unavailable, using WARP");
            hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        }
        check(hr,"D3D11 device"); resize(frame,n);
        pet=std::make_unique<Pet>(); pet->load(device.Get(),wic.Get(),n,n,fs::path(resources));
        log("Desktop bridge ready: transparent premultiplied BGRA output");
    }
    void beginFrame(FrameTarget& target,int n) {
        context->OMSetRenderTargets(1,target.view.GetAddressOf(),nullptr);
        const float clear[4]={0,0,0,0}; context->ClearRenderTargetView(target.view.Get(),clear);
        D3D11_VIEWPORT viewport{0,0,float(n),float(n),0,1}; context->RSSetViewports(1,&viewport);
    }
    void readFrame(FrameTarget& target,int n,uint32_t* output,int stride) {
        context->CopyResource(target.staging.Get(),target.color.Get());
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(target.staging.Get(),0,D3D11_MAP_READ,0,&map),"Readback map");
        for(int y=0;y<n;++y) {
            const auto* src=static_cast<const BYTE*>(map.pData)+size_t(y)*map.RowPitch;
            auto* dst=reinterpret_cast<BYTE*>(output)+size_t(y)*stride;
            for(int x=0;x<n;++x) {
                // Cubism blends premultiplied color into a transparent target.
                // Swizzle RGBA to the premultiplied BGRA UpdateLayeredWindow wants.
                dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];
                dst[x*4+2]=src[x*4];dst[x*4+3]=src[x*4+3];
            }
        }
        context->Unmap(target.staging.Get(),0);
    }
    void readSceneLayer(FrameTarget& target,int n,uint32_t* output,int first,int end,bool blend){
        context->CopyResource(target.staging.Get(),target.color.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        check(context->Map(target.staging.Get(),0,D3D11_MAP_READ,0,&map),"Character layer readback");
        for(int y=first;y<end;++y){
            auto* row=static_cast<const BYTE*>(map.pData)+size_t(y*2)*map.RowPitch;
            auto* next=row+map.RowPitch;
            for(int x=0;x<n;++x){
                auto* a=row+x*8;auto* b=next+x*8;
                unsigned rgba[4];
                for(int c=0;c<4;++c)rgba[c]=(unsigned(a[c])+a[c+4]+b[c]+b[c+4]+2)/4;
                uint32_t& dst=output[y*n+x];
                if(blend){
                    unsigned inverse=255-rgba[3];
                    rgba[0]+=(inverse*((dst>>16)&255)+127)/255;
                    rgba[1]+=(inverse*((dst>>8)&255)+127)/255;
                    rgba[2]+=(inverse*(dst&255)+127)/255;
                    rgba[3]+=(inverse*(dst>>24)+127)/255;
                }
                dst=rgba[2]|(rgba[1]<<8)|(rgba[0]<<16)|(rgba[3]<<24);
            }
        }
        context->Unmap(target.staging.Get(),0);
    }
    void render(int n,float dt,int manual,uint32_t* output,int stride,bool characterOnly=false) {
        if(!output || stride<n*4) throw std::runtime_error("Invalid output buffer");
        resize(frame,n);
        pet->SetRenderTargetSize(n,n);
        pet->update(std::clamp(dt,0.0f,0.1f),manual);
        beginFrame(frame,n);
        pet->draw(context.Get(),n,n,characterOnly);
        readFrame(frame,n,output,stride);
    }
    void renderScene(const fs::path& path,int n,const PetL2DCharacterPose& pose,uint32_t* output){
        if(!output||n<64||n>1024)throw std::runtime_error("Invalid character canvas");
        int renderSize=n*2;
        int slot=pose.preview?1:0;
        auto& target=pose.preview?previewFrame:frame;
        resize(target,renderSize);
        if(scenePaths[slot]!=path){
            auto candidate=std::make_unique<Pet>();
            candidate->load(device.Get(),wic.Get(),renderSize,renderSize,path.parent_path(),path);
            scenePaths[slot]=path;
            scenePets[slot]=std::move(candidate);
        }
        auto& character=*scenePets[slot];
        character.SetRenderTargetSize(renderSize,renderSize);
        character.updateScene(std::clamp(pose.dt,0.f,.1f),pose.blink!=0,pose.keyboard,pose.mouse,
                              pose.key_x,pose.key_y,pose.mouse_x,pose.mouse_y,pose.zoom,pose.offset_x,pose.offset_y);
        int cutoff=character.deskCutoff(n);
        for(int layer=0;layer<2;++layer){
            beginFrame(target,renderSize);
            character.drawSceneLayer(context.Get(),renderSize,layer,pose.zoom,pose.offset_x,pose.offset_y);
            readSceneLayer(target,n,output,layer?cutoff:0,layer?n:cutoff,false);
        }
        if(character.hasRaven()){
            beginFrame(target,renderSize);character.drawSceneLayer(context.Get(),renderSize,2,pose.zoom,pose.offset_x,pose.offset_y);
            readSceneLayer(target,n,output,0,n,true);
        }
    }
};
Runtime* current=nullptr;
void errorText(char* out,int capacity,const char* message) {
    if(out && capacity>0) { strncpy_s(out,capacity,message,_TRUNCATE); }
    log(message);
}
void* createModel(const wchar_t* runtimeFolder,const wchar_t* modelFolder,int size,char* error,int capacity,bool software) {
    try {
        if(current) throw std::runtime_error("Live2D bridge already initialized");
        if(!runtimeFolder || !modelFolder) throw std::runtime_error("Missing model/runtime folder");
        root=fs::path(runtimeFolder); logFile.clear(); logFile.open(root/L"live2d.log",std::ios::trunc);
        auto runtime=std::make_unique<Runtime>(); runtime->initialize(modelFolder,size,software);
        current=runtime.release(); return current;
    } catch(const std::exception& e) { errorText(error,capacity,e.what()); }
      catch(...) { errorText(error,capacity,"Unknown Live2D initialization failure"); }
    return nullptr;
}
}
#define API extern "C" __declspec(dllexport)
API int pet_l2d_version() { return PET_LIVE2D_ABI; }
API void* pet_l2d_create(const wchar_t* runtimeFolder,const wchar_t* modelFolder,int size,char* error,int capacity) {
    return createModel(runtimeFolder,modelFolder,size,error,capacity,false);
}
API void* pet_l2d_create_software(const wchar_t* runtimeFolder,const wchar_t* modelFolder,int size,char* error,int capacity) {
    return createModel(runtimeFolder,modelFolder,size,error,capacity,true);
}
API int pet_l2d_render(void* handle,int size,float dt,int manual,uint32_t* pixels,int stride,char* error,int capacity) {
    try {
        if(!handle || handle!=current) throw std::runtime_error("Invalid Live2D handle");
        current->render(size,dt,manual,pixels,stride); return 1;
    } catch(const std::exception& e) { errorText(error,capacity,e.what()); }
      catch(...) { errorText(error,capacity,"Unknown Live2D render failure"); }
    return 0;
}
API int pet_l2d_render_character(void* handle,int size,float dt,int manual,uint32_t* pixels,int stride,char* error,int capacity) {
    try {
        if(!handle || handle!=current || !current->pet->hasCharacterAtlas())return 0;
        current->render(size,dt,manual,pixels,stride,true); return 1;
    } catch(const std::exception& e) { errorText(error,capacity,e.what()); }
      catch(...) { errorText(error,capacity,"Unknown Live2D character render failure"); }
    return 0;
}
API int pet_l2d_render_scene_character(void* handle,const wchar_t* path,int size,const PetL2DCharacterPose* pose,uint32_t* pixels,char* error,int capacity) {
    try {
        if(!handle||handle!=current||!path||!pose)return 0;
        current->renderScene(path,size,*pose,pixels);return 1;
    } catch(const std::exception& e) { errorText(error,capacity,e.what()); }
      catch(...) { errorText(error,capacity,"Unknown theme-ready character failure"); }
    return 0;
}
API void pet_l2d_clear_preview(void* handle) {
    if(!handle||handle!=current)return;
    current->scenePets[1].reset();
    current->scenePaths[1].clear();
    current->previewFrame={};
}
API void pet_l2d_destroy(void* handle) { if(handle && handle==current) { delete current; current=nullptr; } }
API void pet_l2d_restart(void* handle) { if(handle && handle==current) current->pet->restart(); }
API float pet_l2d_eye(void* handle) { return handle && handle==current ? current->pet->eyeValue() : -1; }
API void pet_l2d_key(void* handle,unsigned vk,int down){
 if(!handle||handle!=current||vk>=256)return;
 bool mapped=false;for(auto k:keyRegions)if(k.vk==vk)mapped=true;
 if(down&&!mapped){lastKey=-1;contactArrived=false;idleTime=10;}
 keyEvent(vk,down!=0);
}
API void pet_l2d_mouse(void* handle,int button,int down){if(handle&&handle==current&&button>=0&&button<3)mouseEvent(button,down!=0);}
API void pet_l2d_clear(void* handle){if(handle&&handle==current)clearInput();}
API int pet_l2d_inspect(void* handle,PetL2DState* s,int bytes){
 if(!handle||handle!=current||!s||bytes!=sizeof(*s))return 0;
 *s={};s->visible_keys=int(keyRegions.size());for(auto k:keyRegions)if(held[k.vk])s->held_visible++;
 s->last_vk=lastKey>=0?keyRegions[lastKey].vk:0;s->arrived=contactArrived;
 for(int i=0;i<3;++i)if(mouseHeld[i])s->mouse_mask|=1<<i;
 for(int i=0;i<5;++i)s->joints[i]=current->pet->currentPose[i];s->eye=current->pet->eyeValue();
 s->nearest_error=lastKey>=0?current->pet->reach.rest[lastKey].error:0;s->key_depth=lastKey>=0?keyDepth[keyRegions[lastKey].vk]:0;return 1;
}
