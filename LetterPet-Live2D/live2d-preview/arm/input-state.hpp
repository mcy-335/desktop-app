#pragma once
#include <array>
#include <sstream>
struct KeyRegion {const char* name;UINT vk;float x,y,w,h;const char* legend;};
static std::vector<KeyRegion> keyRegions;
static std::array<bool,256> held{};
static std::array<float,256> keyDepth{};
static bool contactArrived=false;static int lastKey=-1;
static std::array<float,256> keyImpulse{};
static bool animateKeys(float dt){bool changed=false;for(auto& k:keyRegions){float& d=keyDepth[k.vk];float target=(held[k.vk]||keyImpulse[k.vk]>0)?1.f:0.f;if(lastKey>=0&&keyRegions[lastKey].vk==k.vk&&!contactArrived)target=0;if(keyImpulse[k.vk]>0)changed=true;keyImpulse[k.vk]=std::max(0.f,keyImpulse[k.vk]-dt);float next=d+(target-d)*(1-std::exp(-dt*(target>0?35.f:22.f)));if(std::abs(next-target)<.002f)next=target;if(std::abs(next-d)>.0001f)changed=true;d=next;}return changed;}
static bool mouseHeld[3]={};
static unsigned inputRevision=0;
static float follow[5]={};
static float tap=0,mouseTap=0,idleTime=10;
static bool interactiveMode=true;
static std::wstring lastInput=L"等待输入";
static unsigned keyboardEvents=0,mouseEvents=0;
static void keyEvent(UINT vk,bool down) {
 if(vk>=256)return;
 if(held[vk]==down)return;
 held[vk]=down;++inputRevision;
 if(down)keyImpulse[vk]=.045f;
 for(size_t i=0;i<keyRegions.size();++i)if(keyRegions[i].vk==vk){if(down){lastKey=int(i);contactArrived=false;tap=1;idleTime=0;}break;}
}
static void clearInput(){held.fill(false);keyDepth.fill(0);keyImpulse.fill(0);for(auto& x:mouseHeld)x=false;lastKey=-1;contactArrived=false;tap=mouseTap=0;idleTime=10;++inputRevision;}
static void mouseEvent(int button,bool down){mouseHeld[button]=down;if(down){mouseTap=1;++mouseEvents;lastInput=button==0?L"鼠标左键":button==1?L"鼠标右键":L"鼠标中键";}}
static std::string activeRects() {
 std::ostringstream o;
 for(const auto& k:keyRegions)if(held[k.vk])
 o<<"if(k.x>"<<k.x<<" && k.x<"<<k.x+k.w<<" && k.y>"<<k.y<<" && k.y<"<<k.y+k.h<<") col.rgb=lerp(col.rgb,float3(1,.64,.12),.50);\n";
 return o.str();
}
