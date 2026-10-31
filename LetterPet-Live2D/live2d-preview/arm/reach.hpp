#pragma once
struct ReachPoint {float x=0,y=0;};
struct ReachPose {std::array<float,5> q{};ReachPoint p;float error=0;};
class ReachSolver {
 CubismModel* model=nullptr;CubismIdHandle ids[5];int fingerVertex=0;
 struct Edge{int mesh,a,b;float length;};struct Tri{int mesh,a,b,c;float area;};
 std::vector<Edge> edges;std::vector<Tri> triangles;
 std::vector<ReachPose> samples;
 float limits[5]={3,2,8,2,8};
 static float square(float x){return x*x;}
public:
 std::vector<ReachPose> rest,down;std::vector<ReachPoint> targets;
 ReachPoint point()const{auto p=model->GetDrawableVertexPositions(9)[fingerVertex];return {p.X,p.Y};}
 ReachPoint atlas(float x,float y)const{
  auto p=model->GetDrawableVertexPositions(0);auto u=model->GetDrawableVertexUvs(0);
  float a=(x/2048-u[0].X)/(u[1].X-u[0].X),b=((1-y/2048)-u[0].Y)/(u[2].Y-u[0].Y);
  return {p[0].X+a*(p[1].X-p[0].X)+b*(p[2].X-p[0].X),p[0].Y+a*(p[1].Y-p[0].Y)+b*(p[2].Y-p[0].Y)};
 }
 float errorPixels(ReachPoint a,ReachPoint b)const{auto s=atlas(1,0),z=atlas(0,0);return std::hypot(a.x-b.x,a.y-b.y)/std::abs(s.x-z.x);}
 void apply(const std::array<float,5>& q){for(int i=0;i<5;++i)model->SetParameterValue(ids[i],q[i]);model->Update();}
 bool valid()const{
  for(auto e:edges){auto p=model->GetDrawableVertexPositions(e.mesh);float ratio=std::hypot(p[e.a].X-p[e.b].X,p[e.a].Y-p[e.b].Y)/e.length;if(ratio<.87f||ratio>1.12f)return false;}
  for(auto t:triangles){auto p=model->GetDrawableVertexPositions(t.mesh);float a=(p[t.b].X-p[t.a].X)*(p[t.c].Y-p[t.a].Y)-(p[t.b].Y-p[t.a].Y)*(p[t.c].X-p[t.a].X);if(a/t.area<=.05f)return false;}return true;
 }
 ReachPose solve(ReachPoint target){
  ReachPose best;float cost=1e9f;
  auto test=[&](std::array<float,5> q){apply(q);if(!valid())return;auto p=point();float d=square(p.x-target.x)+square(p.y-target.y);float reg=0;for(int i=0;i<5;++i)reg+=square(q[i]/limits[i]);d+=reg*1e-9f;if(d<cost){cost=d;best.q=q;best.p=p;}};
  // Coarse global seeds, then bounded coordinate refinement on actual Cubism deformation.
  for(auto s:samples){float d=square(s.p.x-target.x)+square(s.p.y-target.y);if(d<cost){cost=d;best=s;}}
  auto step=std::array<float,5>{1.5f,1.f,4.f,1.f,4.f};
  for(int pass=0;pass<12;++pass){auto previous=best.q;for(int i=0;i<5;++i)for(int sign:{-1,1}){auto q=best.q;q[i]=std::clamp(q[i]+sign*step[i],-limits[i],limits[i]);test(q);}if(previous==best.q)for(auto& v:step)v*=.5f;}
  best.error=errorPixels(best.p,target);return best;
 }
 void initialize(CubismModel* m){
  model=m;const char* names[]={"ParamKeyboardShoulder","ParamKeyboardUpperAssist","ParamKeyboardElbow","ParamKeyboardForearmAssist","ParamKeyboardWrist"};for(int i=0;i<5;++i)ids[i]=CubismFramework::GetIdManager()->GetId(names[i]);apply({});
  // Fixed point on the keyboard hand's left/front finger contour; marked cyan in preview.
  auto anchor=atlas(181,565);float closest=1e9f;auto v=m->GetDrawableVertexPositions(9);
  for(int j=0;j<m->GetDrawableVertexCount(9);++j){float d=square(v[j].X-anchor.x)+square(v[j].Y-anchor.y);if(d<closest){closest=d;fingerVertex=j;}}
  for(int mesh=5;mesh<=9;++mesh){auto p=m->GetDrawableVertexPositions(mesh);auto idx=m->GetDrawableVertexIndices(mesh);for(int k=0;k<m->GetDrawableVertexIndexCount(mesh);k+=3){int a=idx[k],b=idx[k+1],c=idx[k+2];float area=(p[b].X-p[a].X)*(p[c].Y-p[a].Y)-(p[b].Y-p[a].Y)*(p[c].X-p[a].X);if(std::abs(area)>1e-8f)triangles.push_back({mesh,a,b,c,area});int x[]={a,b,c,a};for(int j=0;j<3;++j){float len=std::hypot(p[x[j]].X-p[x[j+1]].X,p[x[j]].Y-p[x[j+1]].Y);if(len>.001f)edges.push_back({mesh,x[j],x[j+1],len});}}}
  for(int a=-1;a<=1;++a)for(int b=-1;b<=1;++b)for(int c=-1;c<=1;++c)for(int d=-1;d<=1;++d)for(int e=-1;e<=1;++e){std::array<float,5> q={a*limits[0],b*limits[1],c*limits[2],d*limits[3],e*limits[4]};apply(q);if(valid())samples.push_back({q,point(),0});}
  if(samples.empty())throw std::runtime_error("No safe arm poses");
  std::ofstream report(root/L"reach-map.csv");report<<"key,targetAtlasX,targetAtlasY,nearestErrorAtlasPx,reachable\n";
  for(auto k:keyRegions){float x=k.x+k.w*.5f,y=k.y+k.h*.5f+(x-117)*.03f;float bx=(x-260)*.85f+233,by=(y-592)*.85f+584.8f;auto target=atlas(bx,by);targets.push_back(target);rest.push_back(solve(target));down.push_back(solve(atlas(bx,by+2.2f*.85f)));report<<k.name<<','<<bx<<','<<by<<','<<rest.back().error<<','<<(rest.back().error<=3)<<'\n';}
  apply({});log("Per-key reach map ready; actual mesh contact, bounded existing joint shapes");
 }
};
