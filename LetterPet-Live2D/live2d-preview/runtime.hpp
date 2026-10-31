#pragma once
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <CubismFramework.hpp>
#include <ICubismAllocator.hpp>
#include <CubismModelSettingJson.hpp>
#include <Id/CubismIdManager.hpp>
#include <Model/CubismUserModel.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <Rendering/D3D11/CubismRenderer_D3D11.hpp>
#include <Rendering/D3D11/CubismDeviceInfo_D3D11.hpp>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using namespace Live2D::Cubism::Framework;
using Renderer = Rendering::CubismRenderer_D3D11;
static fs::path root;
static std::ofstream logFile;
static void log(const char* message) { logFile << message << '\n'; logFile.flush(); }
static void check(HRESULT hr, const char* message) {
    if (FAILED(hr)) throw std::runtime_error(std::string(message) + " (HRESULT " + std::to_string(hr) + ")");
}
static std::vector<csmByte> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open: " + path.u8string());
    auto size = in.tellg();
    if (size <= 0 || size > 128 * 1024 * 1024) throw std::runtime_error("Invalid file size: " + path.u8string());
    std::vector<csmByte> data(static_cast<size_t>(size));
    in.seekg(0); in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) throw std::runtime_error("Cannot read: " + path.u8string());
    return data;
}
static csmByte* loadShaderFile(const std::string path, csmSizeInt* size) {
    *size = 0;
    try {
        auto data = readFile(root / fs::u8path(path));
        auto* result = new csmByte[data.size()];
        std::copy(data.begin(), data.end(), result); *size = static_cast<csmSizeInt>(data.size()); return result;
    } catch (const std::exception& e) { log(e.what()); return nullptr; }
}
class Allocator final : public ICubismAllocator {
public:
    void* Allocate(csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void* p) override { std::free(p); }
    void* AllocateAligned(csmSizeType size, csmUint32 alignment) override { return _aligned_malloc(size, alignment); }
    void DeallocateAligned(void* p) override { _aligned_free(p); }
};
struct SdkSession {
    Allocator allocator;
    CubismFramework::Option options{};
    SdkSession() {
        options.LogFunction = log;
        options.LoggingLevel = CubismFramework::Option::LogLevel_Info;
        options.LoadFileFunction = loadShaderFile;
        options.ReleaseBytesFunction = [](csmByte* p) { delete[] p; };
        if (!CubismFramework::StartUp(&allocator, &options)) throw std::runtime_error("Cubism startup failed");
        CubismFramework::Initialize();
    }
    ~SdkSession() { CubismFramework::Dispose(); CubismFramework::CleanUp(); }
};

class Pet final : public CubismUserModel {
    ACubismMotion* blink = nullptr;
public:
    CubismIdHandle eye = nullptr;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
    ~Pet() override { _motionManager->StopAllMotions(); ACubismMotion::Delete(blink); DeleteRenderer(); }
    void load(ID3D11Device* device, IWICImagingFactory* wic, UINT width, UINT height,
              const fs::path& folder = root / L"Resources/pet") {
        auto json = readFile(folder / L"pet.model3.json");
        CubismModelSettingJson settings(json.data(), static_cast<csmSizeInt>(json.size()));
        auto moc = readFile(folder / fs::u8path(settings.GetModelFileName()));
        LoadModel(moc.data(), static_cast<csmSizeInt>(moc.size()), true);
        if (!GetModel()) throw std::runtime_error("MOC3 model failed consistency/load check");
        eye = CubismFramework::GetIdManager()->GetId("ParamEyeLOpen");
        bool found = false;
        for (int i=0;i<_model->GetParameterCount();++i) if (_model->GetParameterId(i)==eye) found=true;
        if (!found) throw std::runtime_error("ParamEyeLOpen missing from model");
        Renderer::SetConstantSettings(1, device);
        CreateRenderer(width, height);
        auto* renderer = GetRenderer<Renderer>();
        renderer->IsPremultipliedAlpha(false);
        for (int i=0;i<settings.GetTextureCount();++i) {
            ComPtr<IWICBitmapDecoder> decoder;
            const auto path = folder / fs::u8path(settings.GetTextureFileName(i));
            check(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder), "Decode texture");
            ComPtr<IWICBitmapFrameDecode> frame; check(decoder->GetFrame(0, &frame), "Texture frame");
            ComPtr<IWICFormatConverter> converter; check(wic->CreateFormatConverter(&converter), "Texture converter");
            check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert RGBA");
            UINT w=0,h=0; check(converter->GetSize(&w,&h), "Texture size");
            if(!w || !h || w>16384 || h>16384) throw std::runtime_error("Unsupported texture dimensions");
            std::vector<BYTE> pixels(size_t(w)*h*4);
            check(converter->CopyPixels(nullptr, w*4, static_cast<UINT>(pixels.size()), pixels.data()), "Texture pixels");
            D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_IMMUTABLE;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA source{pixels.data(),w*4,0};
            ComPtr<ID3D11Texture2D> tex; check(device->CreateTexture2D(&desc,&source,&tex), "GPU texture");
            ComPtr<ID3D11ShaderResourceView> srv; check(device->CreateShaderResourceView(tex.Get(),nullptr,&srv), "Texture view");
            renderer->BindTexture(i,srv.Get()); textures.push_back(srv);
        }
        if(settings.GetMotionCount("Idle")<1) throw std::runtime_error("Idle motion missing");
        auto motion = readFile(folder / fs::u8path(settings.GetMotionFileName("Idle",0)));
        blink = LoadMotion(motion.data(),static_cast<csmSizeInt>(motion.size()),"blink");
        if(!blink) throw std::runtime_error("Blink motion load failed");
        // Native motions do not automatically adopt the JSON Meta.Loop flag.
        blink->SetLoop(true);
        blink->SetLoopFadeIn(false);
        blink->SetFadeInTime(0);
        blink->SetFadeOutTime(0);
        restart();
        log("Model + atlas + Idle motion loaded successfully");
    }
    void restart() { _motionManager->StopAllMotions(); _model->LoadParameters(); _motionManager->StartMotionPriority(blink,false,3); _motionManager->UpdateMotion(_model,0); _model->Update(); }
    void update(float delta, int manual) {
        if(manual>=0) _model->SetParameterValue(eye,manual*0.5f);
        else _motionManager->UpdateMotion(_model,delta);
        _model->Update();
    }
    float eyeValue() const { return _model->GetParameterValue(eye); }
    void draw(ID3D11DeviceContext* context, UINT width, UINT height) {
        auto* renderer = GetRenderer<Renderer>();
        CubismMatrix44 projection;
        projection.Scale(0.92f*std::min(1.0f,float(height)/width),0.92f*std::min(1.0f,float(width)/height));
        projection.MultiplyByMatrix(_modelMatrix);
        renderer->SetMvpMatrix(&projection);
        renderer->StartFrame(context); renderer->DrawModel(); renderer->EndFrame();
    }
};
