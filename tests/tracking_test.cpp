// Test hors ligne du suivi de hauteur (cordes qui sonnent encore) :
//   g++ -O2 -std=c++17 tests/tracking_test.cpp -o tt && ./tt 30
// % du temps où la voix est sur la dernière note jouée (à partir de 70 ms après l'attaque).
#include <cstdio>
#include <cstdlib>
#include "../FantomineDSP.h"
#include <random>
#include <cmath>
#include <string>
using namespace fantomine;
#include "str.h"
// séquences aléatoires : 3 cordes, notes dans un registre, intervalles 90..400 ms, étouffement parfois
int main(int argc,char**argv){
  double sr=44100; int seeds = argc>1? atoi(argv[1]) : 40;
  const char* names[3]={"grave","medium","aigu"}; double lo[3]={31,40,55}, hi[3]={45,57,74};
  double gTot=0; int gN=0;
  for(int reg=0;reg<3;++reg){ double rs=0, lat=0; int nl=0;
   for(int sd=0;sd<seeds;++sd){
    std::mt19937 r(1000*reg+sd); std::uniform_real_distribution<double>u(0,1);
    TheremEngine e; e.prepare(sr); Params p; p.blend=1; p.vibrato=0;
    Str s[3]; std::mt19937 rp(sd);
    double t=0.3, nextT=0.3; double expect=-1,lastOn=0; int good=0,tot=0; double dur=3.0; double reach=-1;
    float in[32],out[32];
    for(long n=0;n<(long)(dur*sr);n+=32){
      for(int j=0;j<32;++j){ double tt=(n+j)/sr; if(tt>=nextT && tt<dur-0.3){
          int st=(int)(u(r)*3); double m=std::round(lo[reg]+u(r)*(hi[reg]-lo[reg]));
          if(u(r)<0.25) for(auto&q:s) q.mute();      // parfois on étouffe tout
          s[st].pluck(midiToHz(m),sr,0.15+0.2*u(r),rp);
          if(reach>=0){ lat+=reach; nl++; } reach=-1;
          expect=m; lastOn=tt; nextT=tt+0.09+0.31*u(r); }
        double x=0; for(auto&q:s) x+=q.tick(); in[j]=(float)x; }
      e.process(in,out,32,p);
      double tt=n/sr;
      if(expect>0 && reach<0 && std::abs(e.displayMidi-expect)<0.5) reach=tt-lastOn;
      if(expect>0 && tt>lastOn+0.07 && tt<nextT && e.displayAmp>0.05){ tot++; if(std::abs(e.displayMidi-expect)<0.5) good++; }
    }
    rs += tot? 100.0*good/tot : 0; }
   printf("%-7s %5.1f %%  (atteinte moyenne %.0f ms)\n", names[reg], rs/seeds, nl? 1000*lat/nl:0); gTot+=rs/seeds; gN++; }
  printf("MOYENNE %.1f\n", gTot/gN);
}
