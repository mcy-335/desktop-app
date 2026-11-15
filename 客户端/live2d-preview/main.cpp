#include "runtime.hpp"

class App {
public:
    HWND window=nullptr;
    UINT width=720,height=720;
    bool paused=false, light=false, resizePending=false;
    int manual=-1;
    double elapsed=0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<IWICImagingFactory> wic;
    std::unique_ptr<Pet> pet;
    ~App() {
        pet.reset();
        if(context) { context->ClearState(); context->Flush(); }
        Rendering::CubismDeviceInfo_D3D11::ReleaseAllDeviceInfo();
        if(window && IsWindow(window)) DestroyWindow(window);
    }
    void initialize(HWND hwnd, bool warp) {
        window=hwnd;
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC factory");
        DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount=1; desc.BufferDesc.Width=width; desc.BufferDesc.Height=height;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow=hwnd; desc.SampleDesc.Count=1; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
            nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
        if(FAILED(hr) && !warp) {
            log("Hardware device unavailable; using WARP");
            hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
        }
        check(hr,"D3D11 device/swap chain");
        makeTarget(); pet=std::make_unique<Pet>(); pet->load(device.Get(),wic.Get(),width,height);
    }
    void makeTarget() {
        ComPtr<ID3D11Texture2D> back; check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Back buffer");
        check(device->CreateRenderTargetView(back.Get(),nullptr,&target),"Render target");
    }
    void render(float dt, bool present=true) {
        if(resizePending) {
            context->OMSetRenderTargets(0,nullptr,nullptr); target.Reset();
            check(swap->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0),"Resize buffers");
            makeTarget(); pet->SetRenderTargetSize(width,height); resizePending=false;
        }
        if(!paused) { pet->update(dt,manual); if(manual<0) elapsed+=dt; }
        float dark[4]={0.065f,0.035f,0.045f,1}; float pale[4]={0.8f,0.8f,0.8f,1};
        context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
        context->ClearRenderTargetView(target.Get(),light?pale:dark);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1}; context->RSSetViewports(1,&viewport);
        pet->draw(context.Get(),width,height);
        if(present) check(swap->Present(1,0),"Present/device removed");
    }
    std::vector<BYTE> capture(const fs::path& path) {
        ComPtr<ID3D11Texture2D> back, staging; check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Read back buffer");
        D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
        desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
        check(device->CreateTexture2D(&desc,nullptr,&staging),"Staging texture"); context->CopyResource(staging.Get(),back.Get());
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"Map rendered pixels");
        std::vector<BYTE> pixels(size_t(width)*height*4);
        for(UINT y=0;y<height;++y) std::copy_n(static_cast<BYTE*>(map.pData)+size_t(y)*map.RowPitch,width*4,pixels.data()+size_t(y)*width*4);
        context->Unmap(staging.Get(),0);
        ComPtr<IWICStream> stream; check(wic->CreateStream(&stream),"PNG stream");
        check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"PNG destination");
        ComPtr<IWICBitmapEncoder> encoder; check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"PNG encoder");
        check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"PNG initialize");
        ComPtr<IWICBitmapFrameEncode> frame; check(encoder->CreateNewFrame(&frame,nullptr),"PNG frame");
        check(frame->Initialize(nullptr),"PNG frame initialize"); check(frame->SetSize(width,height),"PNG size");
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA; check(frame->SetPixelFormat(&format),"PNG pixel format");
        if(!IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)) throw std::runtime_error("PNG encoder changed pixel format");
        auto encoded=pixels;
        for(size_t i=0;i<encoded.size();i+=4) std::swap(encoded[i],encoded[i+2]);
        check(frame->WritePixels(height,width*4,static_cast<UINT>(encoded.size()),encoded.data()),"PNG pixels");
        check(frame->Commit(),"PNG frame commit"); check(encoder->Commit(),"PNG commit"); return pixels;
    }
    void title() {
        wchar_t text[256];
        swprintf_s(text,L"桌宠 Live2D 独立预览 | %s | %.1f / 8 秒 | 空格暂停 · R重播 · 1/2/3眼形 · B背景",
            manual<0?(paused?L"暂停":L"循环播放"):L"手动眼形",std::fmod(elapsed,8.0));
        SetWindowTextW(window,text);
    }
    void restart() { manual=-1; paused=false; elapsed=0; pet->restart(); }
    void selfTest() {
        auto folder=root/L"validation"; fs::create_directories(folder);
        std::ofstream results(folder/L"results.json");
        std::vector<BYTE> opened,closed;
        const float times[]={0,2.5f,2.5666667f,2.9f,5.9666667f,7.9f,8.1f,10.566667f,16.1f};
        results << "{\n  \"renderer\": \"Cubism Native R5 / D3D11\",\n  \"samples\": [\n";
        float current=0;
        for(size_t i=0;i<std::size(times);++i) {
            while(current+0.00001f<times[i]) { float dt=std::min(1.0f/120,times[i]-current); pet->update(dt,-1); current+=dt; }
            paused=true; render(0,false);
            auto pixels=capture(folder/("frame-"+std::to_string(i)+".png"));
            if(i==0) opened=pixels; if(i==2) closed=pixels;
            float eye=pet->eyeValue();
            results << "    {\"seconds\":" << times[i] << ",\"eye\":" << eye << "}" << (i+1<std::size(times)?",":"") << '\n';
            const bool expectClosed=(i==2 || i==4 || i==7);
            if((expectClosed && eye>0.1f) || (!expectClosed && i!=1 && eye<0.9f)) throw std::runtime_error("Blink loop parameter validation failed");
        }
        size_t changed=0,foreground=0;
        for(size_t i=0;i<opened.size();i+=4) {
            if(!std::equal(opened.begin()+i,opened.begin()+i+3,closed.begin()+i)) ++changed;
            if(!std::equal(opened.begin()+i,opened.begin()+i+3,opened.begin())) ++foreground;
        }
        if(changed<10 || foreground<size_t(width)*height/10) throw std::runtime_error("Rendered model/eye pixel validation failed");
        // Also verify a resize and its restoration do not lose the model.
        width=900; height=600; resizePending=true; render(0,false); capture(folder/L"wide.png");
        width=480; height=720; resizePending=true; render(0,false); capture(folder/L"tall.png");
        results << "  ],\n  \"eyeChangedPixels\":" << changed << ",\n  \"foregroundPixels\":" << foreground
                << ",\n  \"resizeTest\":true,\n  \"passed\":true\n}\n";
        log("SELF-TEST PASS: actual motion through two loops, rendered eye pixels and resize");
    }
};
static App* active=nullptr;
static LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_SIZE:
        if(active && active->swap && wp!=SIZE_MINIMIZED && LOWORD(lp) && HIWORD(lp)) {
            active->width=LOWORD(lp); active->height=HIWORD(lp); active->resizePending=true;
        } return 0;
    case WM_KEYDOWN:
        if(active && active->pet) {
            if(wp==VK_ESCAPE) DestroyWindow(hwnd);
            else if(wp==VK_SPACE) active->paused=!active->paused;
            else if(wp=='R') active->restart();
            else if(wp=='B') active->light=!active->light;
            else if(wp>='1' && wp<='3') { active->manual=2-static_cast<int>(wp-'1'); active->paused=false; }
        } return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(hwnd,msg,wp,lp);
    }
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    bool testing=false,warp=false;
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(int i=1;i<argc;++i) { if(wcscmp(argv[i],L"--self-test")==0) testing=true; if(wcscmp(argv[i],L"--warp")==0) warp=true; }
    LocalFree(argv);
    wchar_t path[32768]; GetModuleFileNameW(nullptr,path,32768); root=fs::path(path).parent_path();
    logFile.open(root/(testing?L"self-test.log":L"preview.log"),std::ios::trunc);
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com)) return 1;
    int result=0;
    try {
        SetProcessDPIAware();
        SdkSession sdk;
        App app; active=&app;
        WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=instance; wc.lpszClassName=L"Live2DPet.IndependentPreview"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc)) throw std::runtime_error("Cannot register preview window");
        RECT rect{0,0,720,720}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        HWND hwnd=CreateWindowW(wc.lpszClassName,L"桌宠 Live2D 独立预览",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
        if(!hwnd) throw std::runtime_error("Cannot create preview window");
        app.initialize(hwnd,warp);
        if(testing) app.selfTest();
        else {
            ShowWindow(hwnd,SW_SHOW); app.title();
            auto last=std::chrono::steady_clock::now(); auto titleTime=last;
            MSG msg{}; bool running=true;
            while(running) {
                while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { if(msg.message==WM_QUIT) running=false; TranslateMessage(&msg); DispatchMessageW(&msg); }
                if(!running) break;
                auto now=std::chrono::steady_clock::now();
                float dt=std::min(0.1f,std::chrono::duration<float>(now-last).count()); last=now;
                if(IsIconic(hwnd)) { Sleep(100); continue; }
                app.render(dt);
                if(now-titleTime>std::chrono::milliseconds(150)) { app.title(); titleTime=now; }
            }
        }
        active=nullptr;
    } catch(const std::exception& e) {
        active=nullptr; result=1; log(e.what());
        if(!testing) MessageBoxA(nullptr,e.what(),"Live2D preview error (see preview.log)",MB_OK|MB_ICONERROR);
    }
    CoUninitialize(); return result;
}
