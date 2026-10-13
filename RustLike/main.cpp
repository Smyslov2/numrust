// RustLike - survival-sandbox prototype in the spirit of Rust (Facepunch)
// Win32 + OpenGL 1.x, no external dependencies.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>

#define PI 3.14159265f
typedef BOOL (WINAPI *PFNSWAP)(int);

// ---------------------------------------------------------------- math
static float clampf(float v,float a,float b){return v<a?a:(v>b?b:v);}
static float lerpf(float a,float b,float t){return a+(b-a)*t;}
static float smooth(float a,float b,float x){float t=clampf((x-a)/(b-a),0,1);return t*t*(3-2*t);}
static float hash2(int x,int y){
    unsigned h=(unsigned)x*374761393u+(unsigned)y*668265263u;
    h=(h^(h>>13))*1274126177u; h^=h>>16; return (h&0xFFFFFF)/16777216.f;
}
static float vnoise(float x,float y){
    int ix=(int)floorf(x),iy=(int)floorf(y); float fx=x-ix,fy=y-iy;
    fx=fx*fx*(3-2*fx); fy=fy*fy*(3-2*fy);
    float a=hash2(ix,iy),b=hash2(ix+1,iy),c=hash2(ix,iy+1),d=hash2(ix+1,iy+1);
    return lerpf(lerpf(a,b,fx),lerpf(c,d,fx),fy);
}
static float fbm(float x,float y,int oct){
    float s=0,a=0.5f; for(int i=0;i<oct;i++){s+=vnoise(x,y)*a;x=x*2.03f+17.f;y=y*2.03f+5.f;a*=0.5f;} return s;
}
static float H(float x,float z){
    float n=fbm(x*0.0035f+100,z*0.0035f+40,5);
    float h=(n-0.40f)*85.f;
    h+=fbm(x*0.03f,z*0.03f,2)*2.5f;
    if(h<0) h*=0.6f;
    return h;
}

// ---------------------------------------------------------------- state
static int W=1280,Hh=720; static bool running=true,focused=true;
static HDC hdc; static unsigned fontBase=0, fontSmall=0;
static float px,py,pz,vx,vy,vz,yaw=0,pitch=0;
static bool grounded=false,swim=false;
static float health=100,hunger=100,thirst=100,tod=0.38f;
static int wood=0,stone=0,slot=1,bandages=2,meat=3;
static float swing=0; static bool pendingHit=false;
static char msg[128]=""; static float msgT=0; static float deadT=0;
static std::unordered_map<long long,int> hits;
static float animT=0;

static void say(const char*s){strncpy(msg,s,127);msgT=3.f;}

// ---------------------------------------------------------------- world objects
static const float OC=9.f;
struct Obj{int type;float x,y,z,s;long long key;};
static Obj getObj(int cx,int cz){
    Obj o; o.type=0; o.key=(long long)cx*100003LL+cz;
    float r=hash2(cx,cz);
    if(r>0.45f) return o;
    o.x=(cx+0.1f+0.8f*hash2(cx*3+1,cz))*OC; o.z=(cz+0.1f+0.8f*hash2(cx,cz*5+2))*OC;
    o.y=H(o.x,o.z); o.s=0.8f+0.7f*hash2(cx+77,cz-33);
    if(o.y<1.3f||o.y>34.f) return o;
    if(r<0.33f && o.y<26.f) o.type=1; else if(r>=0.36f) o.type=2;
    if(o.type && hits.count(o.key) && hits[o.key]>=(o.type==1?100:100)) o.type=0;
    return o;
}

// ---------------------------------------------------------------- drawing helpers
static void sky(float sunY,float*zen,float*hor,float&day){
    day=smooth(-0.12f,0.25f,sunY);
    float dz[3]={0.22f,0.43f,0.80f},dh[3]={0.66f,0.76f,0.86f};
    float nz[3]={0.006f,0.012f,0.04f},nh[3]={0.03f,0.045f,0.08f};
    float sh[3]={0.95f,0.55f,0.30f};
    float tw=(1-clampf(fabsf(sunY)/0.28f,0,1))*0.8f;
    for(int i=0;i<3;i++){ zen[i]=lerpf(nz[i],dz[i],day); hor[i]=lerpf(nh[i],dh[i],day); hor[i]=lerpf(hor[i],sh[i]*(0.35f+0.65f*day),tw); }
}
static void cone(float x,float y,float z,float r,float h,int n,float*c){
    glBegin(GL_TRIANGLES);
    for(int i=0;i<n;i++){
        float a0=2*PI*i/n,a1=2*PI*(i+1)/n;
        float k=r/h,l0=1/sqrtf(1+k*k);
        glColor3f(c[0],c[1],c[2]);
        glNormal3f(cosf(a0)*l0,k*l0,sinf(a0)*l0); glVertex3f(x+cosf(a0)*r,y,z+sinf(a0)*r);
        glNormal3f(cosf(a1)*l0,k*l0,sinf(a1)*l0); glVertex3f(x+cosf(a1)*r,y,z+sinf(a1)*r);
        glNormal3f(cosf((a0+a1)/2)*l0,k*l0,sinf((a0+a1)/2)*l0); glVertex3f(x,y+h,z);
    }
    glEnd();
}
static void trunk(float x,float y,float z,float r0,float r1,float h,int n){
    glBegin(GL_QUADS);
    for(int i=0;i<n;i++){
        float a0=2*PI*i/n,a1=2*PI*(i+1)/n;
        glColor3f(0.30f,0.20f,0.12f);
        glNormal3f(cosf(a0),0,sinf(a0)); glVertex3f(x+cosf(a0)*r0,y,z+sinf(a0)*r0); glVertex3f(x+cosf(a0)*r1,y+h,z+sinf(a0)*r1);
        glNormal3f(cosf(a1),0,sinf(a1)); glVertex3f(x+cosf(a1)*r1,y+h,z+sinf(a1)*r1); glVertex3f(x+cosf(a1)*r0,y,z+sinf(a1)*r0);
    }
    glEnd();
}
static void drawTree(const Obj&o){
    float s=o.s; float hp=hits.count(o.key)?hits[o.key]:0; float shake=hp>0?sinf(animT*40)*0.01f*std::min(hp,3.f):0;
    float x=o.x+shake,y=o.y-0.1f,z=o.z;
    trunk(x,y,z,0.34f*s,0.18f*s,6.2f*s,6);
    float v=hash2((int)o.key,3);
    for(int k=0;k<4;k++){
        float c[3]={0.07f+0.04f*v,0.20f+0.07f*v,0.08f+0.03f*v};
        float t=k/3.f; cone(x,y+(1.6f+k*1.55f)*s,z,(2.1f-t*1.2f)*s,2.7f*s,9,c);
    }
}
static void drawRock(const Obj&o){
    float s=o.s*1.1f; float c[3]={0.43f,0.41f,0.39f};
    const int LA=4,LO=8; float pts[LA+1][LO][3];
    for(int i=0;i<=LA;i++) for(int j=0;j<LO;j++){
        float la=PI*0.5f*i/LA, lo=2*PI*j/LO;
        float r=s*(0.85f+0.45f*hash2((int)o.key+i*13,j*7+1));
        pts[i][j][0]=o.x+cosf(la)*cosf(lo)*r*1.2f; pts[i][j][1]=o.y-0.2f+sinf(la)*r*0.95f; pts[i][j][2]=o.z+cosf(la)*sinf(lo)*r;
    }
    glBegin(GL_QUADS);
    for(int i=0;i<LA;i++) for(int j=0;j<LO;j++){
        int j2=(j+1)%LO; float*a=pts[i][j],*b=pts[i][j2],*d=pts[i+1][j2],*e=pts[i+1][j];
        float ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],wx=e[0]-a[0],wy=e[1]-a[1],wz=e[2]-a[2];
        float nx=uy*wz-uz*wy,ny=uz*wx-ux*wz,nz=ux*wy-uy*wx; float l=sqrtf(nx*nx+ny*ny+nz*nz)+1e-6f;
        if(ny<0){nx=-nx;ny=-ny;nz=-nz;}
        float sh=0.85f+0.3f*hash2(i*5+(int)o.key,j);
        glColor3f(c[0]*sh,c[1]*sh,c[2]*sh); glNormal3f(nx/l,ny/l,nz/l);
        glVertex3fv(a);glVertex3fv(b);glVertex3fv(d);glVertex3fv(e);
    }
    glEnd();
}
static const int R=60,N=2*R+1; static const float CELL=2.f;
static float hm[N+2][N+2];
static void tvert(int ox,int oz,int i,int j){
    float h=hm[i+1][j+1];
    float nx=(hm[i][j+1]-hm[i+2][j+1])/(2*CELL), nz=(hm[i+1][j]-hm[i+1][j+2])/(2*CELL);
    float l=sqrtf(nx*nx+1+nz*nz); nx/=l; float ny=1/l; nz/=l;
    float wx=(ox+i)*CELL,wz=(oz+j)*CELL;
    float v=0.82f+0.36f*fbm(wx*0.12f,wz*0.12f,2), v2=vnoise(wx*0.015f,wz*0.015f);
    float g[3]={0.30f+0.12f*v2,0.36f+0.05f*v2,0.14f}; float sand[3]={0.66f,0.60f,0.42f}, rock[3]={0.42f,0.40f,0.38f}, snow[3]={0.85f,0.87f,0.9f};
    float c[3]; for(int k=0;k<3;k++) c[k]=g[k]*v;
    float ts=smooth(1.8f,0.6f,h); for(int k=0;k<3;k++) c[k]=lerpf(c[k],sand[k]*v,ts);
    float tr=smooth(0.86f,0.68f,ny); for(int k=0;k<3;k++) c[k]=lerpf(c[k],rock[k]*(0.8f+0.4f*v2),tr);
    float tn=smooth(38.f,48.f,h); for(int k=0;k<3;k++) c[k]=lerpf(c[k],snow[k],tn);
    if(h<0){ for(int k=0;k<3;k++) c[k]=lerpf(c[k],sand[k]*0.55f,smooth(0,-3,h)); }
    glColor3f(c[0],c[1],c[2]); glNormal3f(nx,ny,nz); glVertex3f(wx,h,wz);
}
static void drawTerrain(){
    int ox=(int)floorf(px/CELL)-R, oz=(int)floorf(pz/CELL)-R;
    for(int i=0;i<N+2;i++) for(int j=0;j<N+2;j++) hm[i][j]=H((ox+i-1)*CELL,(oz+j-1)*CELL);
    for(int j=0;j<N-1;j++){ glBegin(GL_TRIANGLE_STRIP); for(int i=0;i<N;i++){ tvert(ox,oz,i,j+1); tvert(ox,oz,i,j);} glEnd(); }
}
static void box(float cx,float cy,float cz,float sx,float sy,float sz,float r,float g,float b){
    float x0=cx-sx,x1=cx+sx,y0=cy-sy,y1=cy+sy,z0=cz-sz,z1=cz+sz;
    glBegin(GL_QUADS);
    glColor3f(r,g,b);        glVertex3f(x0,y0,z1);glVertex3f(x1,y0,z1);glVertex3f(x1,y1,z1);glVertex3f(x0,y1,z1);
    glColor3f(r*.6f,g*.6f,b*.6f); glVertex3f(x1,y0,z0);glVertex3f(x0,y0,z0);glVertex3f(x0,y1,z0);glVertex3f(x1,y1,z0);
    glColor3f(r*.8f,g*.8f,b*.8f); glVertex3f(x0,y0,z0);glVertex3f(x0,y0,z1);glVertex3f(x0,y1,z1);glVertex3f(x0,y1,z0);
    glColor3f(r*.7f,g*.7f,b*.7f); glVertex3f(x1,y0,z1);glVertex3f(x1,y0,z0);glVertex3f(x1,y1,z0);glVertex3f(x1,y1,z1);
    glColor3f(r*1.1f,g*1.1f,b*1.1f); glVertex3f(x0,y1,z1);glVertex3f(x1,y1,z1);glVertex3f(x1,y1,z0);glVertex3f(x0,y1,z0);
    glColor3f(r*.5f,g*.5f,b*.5f); glVertex3f(x0,y0,z0);glVertex3f(x1,y0,z0);glVertex3f(x1,y0,z1);glVertex3f(x0,y0,z1);
    glEnd();
}
static void rect(float x,float y,float w,float h,float r,float g,float b,float a){
    glColor4f(r,g,b,a);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
}
static void text(float x,float y,const char*s,float r,float g,float b,float a=1,bool small=false){
    glListBase((small?fontSmall:fontBase)-32);
    glColor4f(0,0,0,a*0.8f); glRasterPos2f(x+2,y-2); glCallLists((GLsizei)strlen(s),GL_UNSIGNED_BYTE,s);
    glColor4f(r,g,b,a);      glRasterPos2f(x,y);     glCallLists((GLsizei)strlen(s),GL_UNSIGNED_BYTE,s);
}
static void bar(float x,float y,float w,float h,float v,float r,float g,float b,const char*label){
    rect(x,y,w,h,0,0,0,0.55f);
    rect(x+3,y+3,(w-6)*clampf(v/100.f,0,1),h-6,r,g,b,0.95f);
    rect(x+3,y+h*0.5f,(w-6)*clampf(v/100.f,0,1),(h-6)*0.5f,0,0,0,0.12f);
    char t[48]; sprintf(t,"%s  %d",label,(int)v); text(x+10,y+h*0.5f-6,t,1,1,1,1,true);
}

// ---------------------------------------------------------------- gameplay
static void forwardVec(float&fx,float&fy,float&fz){
    float cp=cosf(pitch),sp=sinf(pitch),sy=sinf(yaw),cy=cosf(yaw); fx=-cp*sy; fy=sp; fz=-cp*cy;
}
static void respawn(){
    px=0;pz=0; for(float r=0;r<2000;r+=8) { bool f=false; for(int a=0;a<16;a++){ float x=cosf(a*PI/8)*r,z=sinf(a*PI/8)*r,h=H(x,z); if(h>3&&h<9){px=x;pz=z;f=true;break;} } if(f)break; }
    py=H(px,pz)+0.5f; vx=vy=vz=0; health=100;hunger=80;thirst=80;
}
static void applyHit(){
    float fx,fy,fz; forwardVec(fx,fy,fz); float ex=px,ey=py+1.7f,ez=pz;
    int cx0=(int)floorf(px/OC),cz0=(int)floorf(pz/OC);
    Obj best; best.type=0; float bt=1e9f;
    for(int cx=cx0-1;cx<=cx0+1;cx++)for(int cz=cz0-1;cz<=cz0+1;cz++){
        Obj o=getObj(cx,cz); if(!o.type) continue;
        float cy=o.type==1?clampf(ey,o.y,o.y+5):o.y+0.5f*o.s;
        float dx=o.x-ex,dy=cy-ey,dz=o.z-ez; float t=dx*fx+dy*fy+dz*fz; if(t<0||t>3.4f) continue;
        float p2=dx*dx+dy*dy+dz*dz-t*t; float rad=o.type==1?0.7f*o.s:1.1f*o.s;
        if(p2<rad*rad && t<bt){bt=t;best=o;}
    }
    if(!best.type) return;
    bool axe=(slot==1); int &h=hits[best.key]; h++;
    if(best.type==1){ wood+=axe?18:6; if(h>=(axe?7:16)){h=100;say("Tree felled");} }
    else            { stone+=axe?6:12; if(h>=7){h=100;say("Rock depleted");} }
}
static void update(float dt){
    animT+=dt; tod+=dt/360.f; if(tod>=1)tod-=1; if(msgT>0)msgT-=dt;
    if(deadT>0){deadT-=dt;}
    float sy=sinf(yaw),cy=cosf(yaw);
    float fx=-sy,fz=-cy,rx=cy,rz=-sy; float mx=0,mz=0;
    if(focused){
        if(GetAsyncKeyState('W')&0x8000){mx+=fx;mz+=fz;} if(GetAsyncKeyState('S')&0x8000){mx-=fx;mz-=fz;}
        if(GetAsyncKeyState('D')&0x8000){mx+=rx;mz+=rz;} if(GetAsyncKeyState('A')&0x8000){mx-=rx;mz-=rz;}
    }
    float l=sqrtf(mx*mx+mz*mz); if(l>0){mx/=l;mz/=l;}
    float sp=4.4f; if(focused&&(GetAsyncKeyState(VK_SHIFT)&0x8000)) sp=7.2f; if(focused&&(GetAsyncKeyState(VK_CONTROL)&0x8000)) sp=2.2f;
    if(swim) sp=2.6f;
    float k=clampf(dt*(grounded||swim?12.f:2.5f),0,1);
    vx=lerpf(vx,mx*sp,k); vz=lerpf(vz,mz*sp,k);
    if(focused&&(GetAsyncKeyState(VK_SPACE)&0x8000)&&(grounded||swim)){vy=swim?3.0f:7.2f;grounded=false;}
    vy-=(swim?4.f:21.f)*dt; if(swim) vy*=0.96f;
    px+=vx*dt; pz+=vz*dt; py+=vy*dt;
    // tree / rock collision
    int cx0=(int)floorf(px/OC),cz0=(int)floorf(pz/OC);
    for(int cx=cx0-1;cx<=cx0+1;cx++)for(int cz=cz0-1;cz<=cz0+1;cz++){
        Obj o=getObj(cx,cz); if(!o.type)continue; float rad=(o.type==1?0.4f*o.s:1.0f*o.s)+0.35f;
        float dx=px-o.x,dz=pz-o.z,d=sqrtf(dx*dx+dz*dz);
        if(d<rad&&d>1e-4f&&py<o.y+(o.type==1?6.f:1.0f*o.s)){px=o.x+dx/d*rad;pz=o.z+dz/d*rad;}
    }
    float gh=H(px,pz); swim=gh<-0.9f;
    float floorY=swim?-1.1f:gh;
    if(py<=floorY){py=floorY;if(vy<0)vy=0;grounded=true;} else grounded=false;
    if(swim&&py<-1.1f){py=-1.1f;}
    // needs
    hunger=std::max(0.f,hunger-0.11f*dt); thirst=std::max(0.f,thirst-0.17f*dt);
    if(hunger<=0||thirst<=0) health-=1.2f*dt; else if(hunger>50&&thirst>50) health=std::min(100.f,health+0.6f*dt);
    if(health<=0){ say("YOU DIED"); deadT=3; respawn(); }
    // drinking
    if(focused&&(GetAsyncKeyState('E')&0x8000)){
        float fx2,fy2,fz2; forwardVec(fx2,fy2,fz2);
        for(float t=0.8f;t<3.2f;t+=0.4f){ float x=px+fx2*t,y=py+1.7f+fy2*t,z=pz+fz2*t; if(y<0.15f&&H(x,z)<0){thirst=std::min(100.f,thirst+30*dt);break;} }
    }
    // hotbar
    static bool prev[256]; 
    for(int i=0;i<6;i++){ int key='1'+i; bool d=focused&&(GetAsyncKeyState(key)&0x8000); if(d&&!prev[key]) slot=i; prev[key]=d; }
    bool lmb=focused&&(GetAsyncKeyState(VK_LBUTTON)&0x8000);
    if(swing>0){ swing-=dt; if(pendingHit&&swing<0.17f){pendingHit=false;applyHit();} }
    else if(lmb){
        if(slot==0||slot==1){swing=0.38f;pendingHit=true;}
        else if(slot==2&&bandages>0&&health<100){bandages--;health=std::min(100.f,health+25);swing=0.5f;say("Bandage used");}
        else if(slot==3&&meat>0&&hunger<100){meat--;hunger=std::min(100.f,hunger+35);swing=0.5f;say("Ate cooked meat");}
    }
}

// ---------------------------------------------------------------- render
static void render(){
    glViewport(0,0,W,Hh);
    float sunA=(tod-0.25f)*2*PI, sunX=cosf(sunA), sunY=sinf(sunA), sunZ=0.35f;
    float sl=sqrtf(sunX*sunX+sunY*sunY+sunZ*sunZ); sunX/=sl;sunY/=sl;sunZ/=sl;
    float zen[3],hor[3],day; sky(sunY,zen,hor,day);
    glClearColor(hor[0],hor[1],hor[2],1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    float fovy=72.f, aspect=(float)W/Hh, nr=0.1f, fr=600.f, top=nr*tanf(fovy*PI/360.f);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-top*aspect,top*aspect,-top,top,nr,fr);
    // sky gradient
    glDisable(GL_DEPTH_TEST);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(-1,1,-1,1,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    float yh=-tanf(pitch)/tanf(fovy*PI/360.f); float yt=yh+1.7f;
    glBegin(GL_QUADS);
    glColor3fv(hor);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,yh);glVertex2f(-1,yh);
    glColor3fv(hor);glVertex2f(-1,yh);glVertex2f(1,yh);glColor3fv(zen);glVertex2f(1,yt);glVertex2f(-1,yt);
    glColor3fv(zen);glVertex2f(-1,yt);glVertex2f(1,yt);glVertex2f(1,1.5f+std::max(0.f,yt));glVertex2f(-1,1.5f+std::max(0.f,yt));
    glEnd();
    glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
    // sun
    glLoadIdentity(); glRotatef(-pitch*180/PI,1,0,0); glRotatef(-yaw*180/PI,0,1,0);
    if(sunY>-0.15f){
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glEnable(GL_POINT_SMOOTH);
        glPointSize(170);glColor4f(1,0.7f,0.4f,0.10f);glBegin(GL_POINTS);glVertex3f(sunX*100,sunY*100,sunZ*100);glEnd();
        glPointSize(80);glColor4f(1,0.85f,0.6f,0.22f);glBegin(GL_POINTS);glVertex3f(sunX*100,sunY*100,sunZ*100);glEnd();
        glPointSize(34);glColor4f(1,0.97f,0.85f,1);glBegin(GL_POINTS);glVertex3f(sunX*100,sunY*100,sunZ*100);glEnd();
        glDisable(GL_BLEND);
    }
    // world
    float bob=(sqrtf(vx*vx+vz*vz)>1&&grounded)?sinf(animT*9)*0.03f:0;
    glLoadIdentity(); glRotatef(-pitch*180/PI,1,0,0); glRotatef(-yaw*180/PI,0,1,0); glTranslatef(-px,-(py+1.7f+bob),-pz);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG); glFogi(GL_FOG_MODE,GL_EXP2); glFogf(GL_FOG_DENSITY,0.0155f); glFogfv(GL_FOG_COLOR,hor);
    glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);glEnable(GL_COLOR_MATERIAL);glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_NORMALIZE);glDisable(GL_CULL_FACE);
    float lp[4]={sunX,sunY,sunZ,0}; float amb[4],dif[4];
    float sunUp=smooth(-0.05f,0.35f,sunY); float tw=(1-clampf(fabsf(sunY)/0.3f,0,1));
    for(int i=0;i<3;i++){ amb[i]=lerpf(0.05f,0.40f,day)*(i==2?1.0f:0.95f)+(i==2?0.02f:0); }
    dif[0]=sunUp*(1.0f+0.2f*tw);dif[1]=sunUp*(0.93f-0.25f*tw);dif[2]=sunUp*(0.80f-0.4f*tw);
    for(int i=0;i<3;i++) dif[i]=std::max(0.f,dif[i]);
    amb[3]=dif[3]=1;
    glLightfv(GL_LIGHT0,GL_POSITION,lp);glLightfv(GL_LIGHT0,GL_DIFFUSE,dif);glLightfv(GL_LIGHT0,GL_AMBIENT,amb);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT,amb);
    drawTerrain();
    int cx0=(int)floorf(px/OC),cz0=(int)floorf(pz/OC);
    for(int cx=cx0-14;cx<=cx0+14;cx++)for(int cz=cz0-14;cz<=cz0+14;cz++){
        Obj o=getObj(cx,cz); if(!o.type)continue;
        float dx=o.x-px,dz=o.z-pz; if(dx*dx+dz*dz>135*135)continue;
        if(o.type==1)drawTree(o);else drawRock(o);
    }
    // water
    glDisable(GL_LIGHTING);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
    float wm=0.25f+0.75f*day; float wc[3]={0.07f*wm+hor[0]*0.15f,0.24f*wm+hor[1]*0.15f,0.32f*wm+hor[2]*0.15f};
    float wy=0.02f*sinf(animT*1.3f); float ex=300;
    glBegin(GL_QUADS);
    glColor4f(wc[0],wc[1],wc[2],0.80f);
    glVertex3f(px-ex,wy,pz-ex);glVertex3f(px+ex,wy,pz-ex);glVertex3f(px+ex,wy,pz+ex);glVertex3f(px-ex,wy,pz+ex);
    glEnd();
    // specular sun glint on water
    glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glBegin(GL_QUADS);
    for(int i=0;i<30;i++){ float a=hash2(i,1)*2*PI,d=10+hash2(i,2)*120; float gx=px+cosf(a)*d,gz=pz+sinf(a)*d; if(H(gx,gz)>0)continue;
        float tw2=0.5f+0.5f*sinf(animT*3+i*2.f); float s=0.8f+tw2; glColor4f(1,0.95f,0.8f,0.25f*tw2*sunUp);
        glVertex3f(gx-s*2,wy+0.03f,gz-s*0.3f);glVertex3f(gx+s*2,wy+0.03f,gz-s*0.3f);glVertex3f(gx+s*2,wy+0.03f,gz+s*0.3f);glVertex3f(gx-s*2,wy+0.03f,gz+s*0.3f);}
    glEnd();
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_FOG);
    // held item
    glClear(GL_DEPTH_BUFFER_BIT); glLoadIdentity();
    float p=swing>0?1-swing/0.38f:0; float ang=sinf(p*PI)*65.f;
    glTranslatef(0.34f,-0.30f+bob,-0.62f); glRotatef(-ang,1,0,0); glRotatef(-12,0,0,1); glRotatef(18,0,1,0);
    if(slot==1){ box(0,0.12f,0,0.022f,0.17f,0.022f,0.45f,0.30f,0.17f); box(0.04f,0.30f,0,0.075f,0.045f,0.016f,0.55f,0.57f,0.60f); box(0.0f,0.30f,0,0.02f,0.05f,0.02f,0.35f,0.35f,0.38f); }
    else if(slot==0){ box(0,0.05f,0,0.07f,0.055f,0.06f,0.45f,0.43f,0.40f); box(0.02f,0.09f,0.01f,0.04f,0.03f,0.04f,0.52f,0.50f,0.47f); }
    else if(slot==2){ box(0,0.05f,0,0.06f,0.03f,0.04f,0.9f,0.9f,0.88f); box(0,0.05f,0,0.062f,0.012f,0.042f,0.75f,0.15f,0.15f); }
    else if(slot==3){ box(0,0.05f,0,0.07f,0.04f,0.05f,0.50f,0.25f,0.15f); box(0.0f,0.09f,0,0.05f,0.015f,0.035f,0.65f,0.35f,0.2f); }
    // HUD
    glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,0,Hh,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    float u=Hh/900.f; if(u<0.7f)u=0.7f;
    // vignette
    glBegin(GL_QUADS); glColor4f(0,0,0,0.0f);glVertex2f(W*0.12f,0);glVertex2f(W*0.88f,0);glColor4f(0,0,0,0.35f);glVertex2f(W,0);glVertex2f(0,0);glEnd();
    float cxm=W*0.5f,cym=Hh*0.5f;
    rect(cxm-2,cym-2,4,4,1,1,1,0.85f); // crosshair dot
    float bx=30*u,by=30*u,bw=250*u,bh=28*u;
    bar(bx,by+2*(bh+6*u),bw,bh,health,0.78f,0.20f,0.16f,"HEALTH");
    bar(bx,by+1*(bh+6*u),bw,bh,hunger,0.80f,0.52f,0.12f,"CALORIES");
    bar(bx,by+0*(bh+6*u),bw,bh,thirst,0.22f,0.52f,0.80f,"HYDRATION");
    // hotbar
    float ss=66*u,gap=6*u,hw=6*ss+5*gap,hx=cxm-hw/2,hy=24*u;
    const char* names[6]={"ROCK","HATCHET","BANDAGE","COOKED MEAT","",""}; 
    for(int i=0;i<6;i++){
        float x=hx+i*(ss+gap); bool sel=(i==slot);
        rect(x,hy,ss,ss,sel?0.55f:0.0f,sel?0.62f:0.0f,sel?0.2f:0.0f,sel?0.75f:0.55f);
        rect(x+2,hy+2,ss-4,ss-4,0.05f,0.05f,0.05f,0.55f);
        char t[16];sprintf(t,"%d",i+1); text(x+5,hy+ss-17,t,0.9f,0.9f,0.9f,0.8f,true);
        if(names[i][0]){ text(x+4,hy+ss*0.42f,i==3?"MEAT":names[i],1,1,1,0.95f,true); }
        if(i==2){sprintf(t,"x%d",bandages);text(x+ss-28,hy+6,t,1,1,0.6f,1,true);} if(i==3){sprintf(t,"x%d",meat);text(x+ss-28,hy+6,t,1,1,0.6f,1,true);} 
    }
    // resources
    char t[64]; sprintf(t,"WOOD  %d",wood); text(W-210*u,Hh-48*u,t,0.95f,0.82f,0.55f,1);
    sprintf(t,"STONE  %d",stone); text(W-210*u,Hh-80*u,t,0.8f,0.8f,0.8f,1);
    int hh=(int)(tod*24+6)%24,mm=(int)(fmodf(tod*24*60,60)); sprintf(t,"%02d:%02d",hh,mm); text(W-210*u,Hh-112*u,t,1,1,1,0.8f,true);
    text(30*u,Hh-40*u,"RUSTLIKE  -  PROTOTYPE",1,1,1,0.55f,true);
    text(30*u,Hh-64*u,"WASD move  Shift sprint  Ctrl crouch  Space jump  LMB use  E drink  1-4 items  Esc quit",1,1,1,0.45f,true);
    if(msgT>0){ float a=clampf(msgT,0,1); text(cxm-80*u,hy+ss+30*u,msg,1,0.9f,0.5f,a); }
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------- window
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
        case WM_CLOSE: case WM_DESTROY: running=false; return 0;
        case WM_SIZE: W=LOWORD(l); Hh=std::max(1,(int)HIWORD(l)); return 0;
        case WM_SETFOCUS: focused=true; return 0;
        case WM_KILLFOCUS: focused=false; return 0;
        case WM_KEYDOWN: if(w==VK_ESCAPE) running=false; return 0;
        case WM_SETCURSOR: SetCursor(NULL); return TRUE;
    }
    return DefWindowProcA(h,m,w,l);
}
int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int){
    SetProcessDPIAware();
    WNDCLASSA wc={}; wc.lpfnWndProc=WndProc;wc.hInstance=hi;wc.lpszClassName="RustLikeWnd";wc.hCursor=NULL; RegisterClassA(&wc);
    W=GetSystemMetrics(SM_CXSCREEN);Hh=GetSystemMetrics(SM_CYSCREEN);
    HWND hw=CreateWindowExA(0,"RustLikeWnd","RustLike",WS_POPUP|WS_VISIBLE,0,0,W,Hh,NULL,NULL,hi,NULL);
    hdc=GetDC(hw);
    PIXELFORMATDESCRIPTOR pfd={}; pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cDepthBits=24;
    SetPixelFormat(hdc,ChoosePixelFormat(hdc,&pfd),&pfd);
    HGLRC rc=wglCreateContext(hdc); wglMakeCurrent(hdc,rc);
    PFNSWAP swapI=(PFNSWAP)wglGetProcAddress("wglSwapIntervalEXT"); if(swapI)swapI(1);
    HFONT f1=CreateFontA(-26,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Arial");
    HFONT f2=CreateFontA(-17,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Arial");
    fontBase=glGenLists(96); SelectObject(hdc,f1); wglUseFontBitmapsA(hdc,32,96,fontBase);
    fontSmall=glGenLists(96); SelectObject(hdc,f2); wglUseFontBitmapsA(hdc,32,96,fontSmall);
    glHint(GL_FOG_HINT,GL_NICEST); glShadeModel(GL_SMOOTH);
    respawn(); say("Welcome to the island. Gather wood and stone.");
    LARGE_INTEGER fq,t0,t1; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
    ShowCursor(FALSE); POINT c={W/2,Hh/2}; SetCursorPos(c.x,c.y);
    while(running){
        MSG m; while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}
        QueryPerformanceCounter(&t1); float dt=(float)(t1.QuadPart-t0.QuadPart)/fq.QuadPart; t0=t1; dt=std::min(dt,0.05f);
        if(focused){
            POINT p; GetCursorPos(&p); float dx=(float)(p.x-c.x),dy=(float)(p.y-c.y); SetCursorPos(c.x,c.y);
            yaw-=dx*0.0022f; pitch=clampf(pitch-dy*0.0022f,-1.5f,1.5f);
        }
        update(dt); render(); SwapBuffers(hdc);
    }
    ShowCursor(TRUE); wglMakeCurrent(NULL,NULL); wglDeleteContext(rc); return 0;
}
