#include "../live2d-preview/arm/runtime.hpp"
#include "live2d_bridge.h"

namespace {
struct Runtime {
    bool comOwned=false;
    std::unique_ptr<SdkSession> sdk;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> color,staging;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<IWICImagingFactory> wic;
    std::unique_ptr<Pet> pet;
    int size=0;
    ~Runtime() {
        pet.reset();
        if(context) { context->ClearState(); context->Flush(); }
        releaseKeyboardLayout();
        Rendering::CubismDeviceInfo_D3D11::ReleaseAllDeviceInfo();
        sdk.reset();
        target.Reset(); staging.Reset(); color.Reset();
        context.Reset(); device.Reset(); wic.Reset();
        if(comOwned) CoUninitialize();
        logFile.close();
    }
    void resize(int n) {
        if(n==size) return;
        if(n<64 || n>2048) throw std::runtime_error("Invalid Live2D canvas size");
        context->OMSetRenderTargets(0,nullptr,nullptr);
        target.Reset(); staging.Reset(); color.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=desc.Height=n; desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&desc,nullptr,&color),"Offscreen texture");
        check(device->CreateRenderTargetView(color.Get(),nullptr,&target),"Offscreen target");
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&desc,nullptr,&staging),"Readback texture");
        if(pet) pet->SetRenderTargetSize(n,n);
        size=n;
    }
    void initialize(const wchar_t* resources,int n) {
        HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        comOwned=SUCCEEDED(hr);
        if(FAILED(hr) && hr!=RPC_E_CHANGED_MODE) check(hr,"COM initialize");
        sdk=std::make_unique<SdkSession>();
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC factory");
        hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(hr)) {
            log("Hardware unavailable, using WARP");
            hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        }
        check(hr,"D3D11 device"); resize(n);
        pet=std::make_unique<Pet>(); pet->load(device.Get(),wic.Get(),n,n,fs::path(resources));
        log("Desktop bridge ready: transparent premultiplied BGRA output");
    }
    void render(int n,float dt,int manual,uint32_t* output,int stride) {
        if(!output || stride<n*4) throw std::runtime_error("Invalid output buffer");
        resize(n); pet->update(std::clamp(dt,0.0f,0.1f),manual);
        context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
        const float clear[4]={0,0,0,0}; context->ClearRenderTargetView(target.Get(),clear);
        D3D11_VIEWPORT viewport{0,0,float(n),float(n),0,1}; context->RSSetViewports(1,&viewport);
        pet->draw(context.Get(),n,n);
        context->CopyResource(staging.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"Readback map");
        for(int y=0;y<n;++y) {
            const auto* src=static_cast<const BYTE*>(map.pData)+size_t(y)*map.RowPitch;
            auto* dst=reinterpret_cast<BYTE*>(output)+size_t(y)*stride;
            for(int x=0;x<n;++x) {
                // Cubism blends premultiplied color into a transparent target.
                // Swizzle RGBA to the premultiplied BGRA UpdateLayeredWindow wants.
                dst[x*4]=src[x*4+2]; dst[x*4+1]=src[x*4+1];
                dst[x*4+2]=src[x*4]; dst[x*4+3]=src[x*4+3];
            }
        }
        context->Unmap(staging.Get(),0);
    }
};
Runtime* current=nullptr;
void errorText(char* out,int capacity,const char* message) {
    if(out && capacity>0) { strncpy_s(out,capacity,message,_TRUNCATE); }
    log(message);
}
}
#define API extern "C" __declspec(dllexport)
API int pet_l2d_version() { return PET_LIVE2D_ABI; }
API void* pet_l2d_create(const wchar_t* runtimeFolder,const wchar_t* modelFolder,int size,char* error,int capacity) {
    try {
        if(current) throw std::runtime_error("Live2D bridge already initialized");
        if(!runtimeFolder || !modelFolder) throw std::runtime_error("Missing model/runtime folder");
        root=fs::path(runtimeFolder); logFile.clear(); logFile.open(root/L"live2d.log",std::ios::trunc);
        auto runtime=std::make_unique<Runtime>(); runtime->initialize(modelFolder,size);
        current=runtime.release(); return current;
    } catch(const std::exception& e) { errorText(error,capacity,e.what()); }
      catch(...) { errorText(error,capacity,"Unknown Live2D initialization failure"); }
    return nullptr;
}
API int pet_l2d_render(void* handle,int size,float dt,int manual,uint32_t* pixels,int stride,char* error,int capacity) {
    try {
        if(!handle || handle!=current) throw std::runtime_error("Invalid Live2D handle");
        current->render(size,dt,manual,pixels,stride); return 1;
    } catch(const std::exception& e) { errorText(error,capacity,e.what()); }
      catch(...) { errorText(error,capacity,"Unknown Live2D render failure"); }
    return 0;
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
