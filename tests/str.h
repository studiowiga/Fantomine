// corde pincée « réaliste » : harmoniques en 1/k avec position de pincement, aigus qui s'éteignent plus vite,
// légère inharmonicité, clic de médiator (bruit aigu 3 ms)
struct Str {
  static constexpr int K=40; double ph[K]{}, w[K]{}, a[K]{}, dec[K]{}; int nk=0; bool on=false; double sr=44100; double click=0; std::mt19937 rr{3};
  void pluck(double f,double sr_,double amp,std::mt19937&r){ sr=sr_; on=true; nk=0; std::uniform_real_distribution<double>u(0,1);
    double B = 1.5e-4; double pos = 0.12 + 0.05*u(r);
    for(int k=1;k<=K;++k){ double fk=k*f*std::sqrt(1+B*k*k); if(fk>8000) break; w[nk]=2*M_PI*fk/sr; ph[nk]=0; a[nk]=amp*std::abs(std::sin(M_PI*k*pos))/k*1.6;
      double tau=2.5/(1+std::pow(fk/350.0,1.3)); dec[nk]=std::exp(-1.0/(tau*sr)); nk++; }
    click = amp*0.6; }
  void mute(){ for(int k=0;k<nk;++k) dec[k]=std::exp(-1.0/(0.01*sr)); }
  double tick(){ if(!on) return 0; double x=0; for(int k=0;k<nk;++k){ ph[k]+=w[k]; x+=a[k]*std::sin(ph[k]); a[k]*=dec[k]; }
    if(click>1e-5){ std::uniform_real_distribution<double>u(-1,1); static double hp=0; double n=u(rr)*click; x+=n-hp; hp=n; click*=std::exp(-1.0/(0.002*sr)); }
    return x; }
};
