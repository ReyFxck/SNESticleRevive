#include "pch.h"
#include "Utilities/Audio/HermiteResampler.h"
#include "OriginalHermiteResampler.h"
#include "NES/NesMixerLookup.h"
#include <random>
#include <array>
static void lookup() {
 for(int s=0;s<=30;++s) for(int d=0;d<128;++d) for(int t=0;t<16;++t) for(int n=0;n<16;++n) {
  double square=(double)s, tnd=d+2.7516713261*t+1.8493587125*n;
  uint16_t sv=s? (uint16_t)((95.88*5000.0)/(8128.0/square+100.0)):0;
  uint16_t tv=tnd? (uint16_t)((159.79*5000.0)/(22638.0/tnd+100.0)):0;
  int16_t c[11]={(int16_t)std::min(s,15),(int16_t)std::max(s-15,0),(int16_t)t,(int16_t)n,(int16_t)d};
  assert(MesenceNativeMixer(c)==sv+tv);
 }
 std::mt19937 rng(45901);
 for(int i=0;i<100000;++i) {
  int16_t c[11]; c[0]=rng()%16;c[1]=rng()%16;c[2]=rng()%16;c[3]=rng()%16;c[4]=rng()%128;
  for(int j=5;j<11;++j)c[j]=(int16_t)rng();
  double sq=c[0]+c[1], tnd=c[4]+2.7516713261*c[2]+1.8493587125*c[3];
  uint16_t sv=sq?(uint16_t)((95.88*5000.0)/(8128.0/sq+100.0)):0;
  uint16_t tv=tnd?(uint16_t)((159.79*5000.0)/(22638.0/tnd+100.0)):0;
  double total=sv+tv+c[5]*20.0+c[6]*43.0+c[9]*20.0+c[10]*15.0+c[7]*5.0+c[8];
  assert(MesenceNativeMixer(c)==total);
 }
 puts("NES mixer: 1015808 base combinations and 100000 expansion sums match original double formula");
}
template<bool add> static void resample() {
 std::mt19937 rng(67219);
 for(double volume:{0.0,0.5,1.0,1.5}) for(double ratio:{2.0,1.0,0.6666666666666666,1.5,2.25}) {
  HermiteResampler got; OriginalHermiteResampler ref;
  got.SetVolume(volume);ref.SetVolume(volume);
  got.SetSampleRates(48000*ratio,48000);ref.SetSampleRates(48000*ratio,48000);
  for(int call=0;call<1000;++call) {
   if(call%97==0) {got.Reset();ref.Reset();}
   if(call%113==0) {double next=call%226?2.0:ratio;got.SetSampleRates(48000*next,48000);ref.SetSampleRates(48000*next,48000);}
   unsigned n=call%17==0?0:1+rng()%513, capacity=1+rng()%512;
   std::vector<int16_t> in(n*2), a(capacity*2+8), b;
   for(auto &s:in)s=(int16_t)rng();
   for(auto &s:a)s=(int16_t)rng();b=a;
   bool fill=call%13==0;
   auto na=got.Resample<add>(in.data(),n,a.data(),capacity,fill);
   auto nb=ref.Resample<add>(in.data(),n,b.data(),capacity,fill);
   assert(na==nb && a==b && got.GetPendingCount()==ref.GetPendingCount());
  }
 }
}
int main() {
 lookup();resample<false>();resample<true>();
 puts("NES Hermite: 40000 chunk/rate/volume/pending/reset/add/fill calls, identical PCM and guards");
}
