// Dobles mínimos: el dispatch corre síncrono, y las partes de escena/física no participan en la
// fusión (el VROARWorldMesh del test se construye con physicsWorld nulo).
#include <functional>
#include <cstdio>
#include <memory>
#include <vector>
#include <string>
#include "VROVector3f.h"
#include "VROMatrix4f.h"

void VROPlatformDispatchAsyncBackground(std::function<void()> fcn) { fcn(); }
void VROPlatformDispatchAsyncRenderer(std::function<void()> fcn) { fcn(); }
void _pabort(const char *f, int l, const char *fn) { fprintf(stderr,"pabort %s:%d\n",f,l); abort(); }

double VROMathClamp(double v,double a,double b){return v<a?a:(v>b?b:v);} 
float VROMathNormalizeAnglePI(float r){return r;}
float VROMathFastSquareRoot(float x){return __builtin_sqrtf(x);}
void  VROMathFastSinCos(float x,float r[2]){r[0]=__builtin_sinf(x);r[1]=__builtin_cosf(x);}
bool  VROMathEquals(float a,float b,float t){return __builtin_fabsf(a-b)<=t;}
bool  VROMathIsZero(float a,float t){return __builtin_fabsf(a)<=t;}
float VROMathReciprocal(float x){return 1.0f/x;}
float VROMathReciprocalSquareRoot(float x){return 1.0f/__builtin_sqrtf(x);}
float clamp(float v,float a,float b){return v<a?a:(v>b?b:v);} 
void VROMathTransposeMatrix(const float*m,float*r){for(int c=0;c<4;c++)for(int w=0;w<4;w++)r[w*4+c]=m[c*4+w];}
void VROMathMultMatrices(const float*a,const float*b,float*r){
  for(int c=0;c<4;c++)for(int w=0;w<4;w++){float s=0;for(int k=0;k<4;k++)s+=a[k*4+w]*b[c*4+k];r[c*4+w]=s;}}
bool VROMathInvertMatrix(const float*m,float*inv){
  double a[4][8];
  for(int r=0;r<4;r++){for(int c=0;c<4;c++)a[r][c]=m[c*4+r];for(int c=0;c<4;c++)a[r][4+c]=(r==c)?1.0:0.0;}
  for(int c=0;c<4;c++){int p=c;for(int r=c+1;r<4;r++)if(__builtin_fabs(a[r][c])>__builtin_fabs(a[p][c]))p=r;
    if(__builtin_fabs(a[p][c])<1e-12)return false;
    for(int k=0;k<8;k++){double t=a[c][k];a[c][k]=a[p][k];a[p][k]=t;}
    double d=a[c][c];for(int k=0;k<8;k++)a[c][k]/=d;
    for(int r=0;r<4;r++)if(r!=c){double f=a[r][c];for(int k=0;k<8;k++)a[r][k]-=f*a[c][k];}}
  for(int r=0;r<4;r++)for(int c=0;c<4;c++)inv[c*4+r]=(float)a[r][4+c];
  return true;}
