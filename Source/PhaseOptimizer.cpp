#include "PhaseOptimizer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace phase {
namespace {
double i0(double x){double sum=1,v=1;for(int k=1;k<40;++k){v*=x*x/(4.*k*k);sum+=v;if(v<sum*1.e-15)break;}return sum;}
double kaiser(double u,double beta){return i0(beta*std::sqrt(std::max(0.,1-u*u)))/i0(beta);}
double energyDb(double a,double b){return 10.*std::log10(std::max(a,1.e-20)/std::max(b,1.e-20));}
}
PeakKernel::PeakKernel(){
    for(int p=0;p<phases;++p){double sum=0;for(int k=0;k<taps;++k){const double t=double(k-half+1)-double(p)/phases;
        const double v=p==0?(k==half-1?1.:0.):std::sin(pi*t)/(pi*t)*kaiser(t/half,9.);
        h[size_t(p)][size_t(k)]=v;sum+=v;
    }for(auto& v:h[size_t(p)])v/=sum;}
}
float PeakKernel::interpolate(const std::vector<float>& x,int i,int p) const noexcept {
    double sum=0;for(int k=0;k<taps;++k)sum+=h[size_t(p)][size_t(k)]*x[size_t(i+k-half+1)];return float(sum);
}
double PeakKernel::peak(const std::vector<float>& x,int first,int last,int stride) const noexcept {
    double result=0;for(int i=first;i<last;++i)for(int p=0;p<phases;p+=stride)result=std::max(result,double(std::abs(interpolate(x,i,p))));return result;
}
void Planner::fft(std::vector<std::complex<double>>& x,bool inverse){
    const size_t n=x.size();for(size_t i=1,j=0;i<n;++i){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[i],x[j]);}
    for(size_t len=2;len<=n;len<<=1){const auto root=std::polar(1.,(inverse?2.:-2.)*pi/double(len));for(size_t i=0;i<n;i+=len){std::complex<double> w=1.;for(size_t j=0;j<len/2;++j){const auto u=x[i+j],v=x[i+j+len/2]*w;x[i+j]=u+v;x[i+j+len/2]=u-v;w*=root;}}}
    if(inverse)for(auto& v:x)v/=double(n);
}
void Planner::prepare(double rate){
    sr=std::isfinite(rate)?std::clamp(rate,8000.,384000.):48000.;
    nominalN=frameN=int(std::ceil(sr*.080));maximumN=int(std::ceil(sr*.120));
    radius=std::max(128,int(std::ceil(sr*.040))); // 80 ms FIR support: accurate bass quadrature
    guard=radius+PeakKernel::half*2+64;
    fftN=1;while(fftN<snapshotSize()+2*radius)fftN*=2;
    kernel.assign(size_t(fftN),{});fftWork=kernel;
    for(int k=-radius;k<=radius;++k)if(k!=0&&std::abs(k)%2==1)kernel[size_t(k+radius)]=2./(pi*k)*kaiser(double(k)/radius,8.6);
    fft(kernel,false);
    for(auto* set:{&q,&a,&candidate})for(auto& v:*set)v.assign(size_t(snapshotSize()),0.f);
    weight.assign(size_t(snapshotSize()),0.f);
}
void Planner::spectrum(const std::array<std::vector<float>,2>& x,std::array<double,bands>& out){
    out.fill(0.);
    for(int c=0;c<2;++c){std::fill(fftWork.begin(),fftWork.end(),std::complex<double>{});
        // Include unchanged context: a short rectangular crop of a rotated
        // bass cycle has a different spectrum even for an ideal allpass.
        for(int i=0;i<snapshotSize();++i)fftWork[size_t(i)]=x[size_t(c)][size_t(i)];
        fft(fftWork,false);
        for(int k=1;k<fftN/2;++k){const double hz=double(k)*sr/fftN;
            // A 40–120 ms event cannot resolve narrow bass bands reliably.
            // Pool sub-120 Hz energy; retain logarithmic detail above it.
            const int band=hz<120.?0:std::clamp(1+int(std::log2(hz/120.)*(bands-1)/std::log2(sr*.5/120.)),1,bands-1);
            out[size_t(band)]+=std::norm(fftWork[size_t(k)]);
        }
    }
}
bool Planner::guards(const Job& job,Report& report){
    double ex=0,ey=0,mx=0,my=0,sx=0,sy=0,llx=0,rrx=0,lly=0,rry=0,xyx=0,xyy=0,tx=0,ty=0;
    int peakIndex=0;double peak=0;
    for(int i=0;i<frameN;++i){const int n=guard+i;const double l=job.input[0][size_t(n)],r=job.input[1][size_t(n)],u=candidate[0][size_t(n)],v=candidate[1][size_t(n)];
        const double x=l*l+r*r,y=u*u+v*v;ex+=x;ey+=y;tx+=i*x;ty+=i*y;
        mx+=(l+r)*(l+r);my+=(u+v)*(u+v);sx+=(l-r)*(l-r);sy+=(u-v)*(u-v);
        llx+=l*l;rrx+=r*r;lly+=u*u;rry+=v*v;xyx+=l*r;xyy+=u*v;
        if(std::max(std::abs(l),std::abs(r))>peak){peak=std::max(std::abs(l),std::abs(r));peakIndex=i;}
    }
    if(ex<1.e-18)return false;
    const double tolerance=job.settings.toleranceDb,factor=tolerance/.1;
    report.rmsDelta=float(energyDb(ey,ex));
    if(std::abs(report.rmsDelta)>tolerance)return false;
    for(auto pair:{std::pair<double,double>{my,mx},{sy,sx},{lly,llx},{rry,rrx}}){
        // A previously silent channel/component must remain silent.
        if(pair.second<ex*1.e-10){if(pair.first>ex*1.e-9)return false;}
        else if(std::abs(energyDb(pair.first,pair.second))>tolerance)return false;
    }
    const double cx=xyx/std::sqrt(std::max(1.e-30,llx*rrx)),cy=xyy/std::sqrt(std::max(1.e-30,lly*rry));
    report.correlationDelta=float(std::abs(cx-cy));if(report.correlationDelta>.02*factor)return false;
    report.centroidMs=float(std::abs(ty/std::max(ey,1.e-20)-tx/ex)*1000./sr);
    if(report.centroidMs>.5*factor)return false;
    // Local energy before/around/after the input's highest transient.
    const int edges[]{peakIndex-int(sr*.020),peakIndex-int(sr*.005),peakIndex,peakIndex+int(sr*.005),peakIndex+int(sr*.020)};
    for(int k=0;k<4;++k){double x=0,y=0;for(int i=std::max(0,edges[k]);i<std::min(frameN,edges[k+1]);++i)for(int c=0;c<2;++c){const int n=guard+i;x+=double(job.input[size_t(c)][size_t(n)])*job.input[size_t(c)][size_t(n)];y+=double(candidate[size_t(c)][size_t(n)])*candidate[size_t(c)][size_t(n)];}
        if(x>ex*1.e-5&&std::abs(energyDb(y,x))>std::max(.6,6.*tolerance))return false;
    }
    std::array<double,bands> spectrumOut{};spectrum(candidate,spectrumOut);
    double total=0;for(auto v:drySpectrum)total+=v;
    report.spectralError=0;double weightedError=0;
    for(int k=0;k<bands;++k){const double x=drySpectrum[size_t(k)],y=spectrumOut[size_t(k)];
        if(x<total*1.e-7){if(y>total*1.e-6)return false;continue;}
        const double error=std::abs(energyDb(y,x));
        // Weak finite-window bands can change with phase alone. Bound both
        // the energy-weighted log error and any individual active band.
        if(error>std::max(3.,30.*tolerance))return false;
        weightedError+=x*error*error;
    }
    report.spectralError=float(std::sqrt(weightedError/std::max(total,1.e-20)));
    return report.spectralError<=std::max(.6,6.*tolerance);
}
void Planner::render(const Job& job,Plan& plan){
    frameN=job.length>0?std::clamp(job.length,int(std::ceil(sr*.040)),maximumN):nominalN;plan.length=frameN;
    plan.epoch=job.epoch;plan.revision=job.revision;plan.frame=job.frame;plan.report={};
    for(int c=0;c<2;++c)std::copy_n(job.input[size_t(c)].begin()+guard,frameN,plan.output[size_t(c)].begin());
    const int begin=guard-PeakKernel::half,end=guard+frameN+PeakKernel::half;
    const int stride=job.settings.precise?1:2;
    double dryPeak=0;for(int c=0;c<2;++c)dryPeak=std::max(dryPeak,tp.peak(job.input[size_t(c)],begin,end,stride));
    plan.report.inPeak=plan.report.outPeak=float(dryPeak);
    if(job.settings.bypass){plan.report.decision=Decision::bypassed;return;}
    const double threshold=dbToGain(job.settings.threshold);
    if(dryPeak<=threshold){plan.report.decision=Decision::below;return;}
    plan.report.decision=Decision::rejected;
    if(job.settings.maxAngle<.01f)return;
    const int fade=std::clamp(int(sr*job.settings.transitionMs*.001),1,frameN/3),pad=PeakKernel::half*2+8;
    std::fill(weight.begin(),weight.end(),0.f);
    for(int i=pad;i<frameN-pad;++i){const double u=std::min({1.,double(i-pad)/fade,double(frameN-pad-1-i)/fade});weight[size_t(guard+i)]=float(std::pow(std::sin(pi*.5*u),2.));}
    for(int c=0;c<2;++c){const auto& x=job.input[size_t(c)];double mean=0,nyquist=0;for(int i=0;i<snapshotSize();++i){mean+=x[size_t(i)];nyquist+=(i%2?-1.:1.)*x[size_t(i)];}mean/=snapshotSize();nyquist/=snapshotSize();
        std::fill(fftWork.begin(),fftWork.end(),std::complex<double>{});for(int i=0;i<snapshotSize();++i)fftWork[size_t(i)]=x[size_t(i)];
        fft(fftWork,false);for(int i=0;i<fftN;++i)fftWork[size_t(i)]*=kernel[size_t(i)];fft(fftWork,true);
        for(int i=0;i<snapshotSize();++i){q[size_t(c)][size_t(i)]=float(fftWork[size_t(i+radius)].real());a[size_t(c)][size_t(i)]=float(x[size_t(i)]-mean-nyquist*(i%2?-1.:1.));}
    }
    spectrum(job.input,drySpectrum);
    const double minimum=dbToGain(-.2f),target=threshold*dbToGain(-.1f);
    double best=dryPeak,searchBest=dryPeak;float bestAngle=0,searchCentre=0;Report bestReport=plan.report;
    auto synthesize=[&](float degrees){
        const double angle=degrees*pi/180.,plateauCos=std::cos(angle)-1.,plateauSin=-std::sin(angle);
        double nativePeak=0;
        for(int i=0;i<snapshotSize();++i){const double w=weight[size_t(i)];
            const double co=w==1.?plateauCos:std::cos(angle*w)-1.,si=w==1.?plateauSin:-std::sin(angle*w);
            for(int c=0;c<2;++c){const float x=job.input[size_t(c)][size_t(i)];
                // Interpolate the angle, not dry/wet amplitudes: the latter
                // cancels energy at transitions even for ideal quadrature.
                const float y=w==0.?x:float(x+co*a[size_t(c)][size_t(i)]+si*q[size_t(c)][size_t(i)]);
                candidate[size_t(c)][size_t(i)]=y;
                if(i>=begin&&i<end)nativePeak=std::max(nativePeak,double(std::abs(y)));
            }
        }
        return nativePeak;
    };
    auto trial=[&](float degrees){if(std::abs(degrees)<.01f)return;
        const double pk=synthesize(degrees);
        if(pk<searchBest){searchBest=pk;searchCentre=degrees;}
        // Native peak is only a lower bound. Every accepted candidate is
        // measured again with the reconstruction kernel on its float output.
        if(pk>=best||pk>dryPeak*minimum||(!job.settings.partial&&pk>target))return;
        Report report=plan.report;if(!guards(job,report))return;
        double actual=0;for(int c=0;c<2;++c)actual=std::max(actual,tp.peak(candidate[size_t(c)],begin,end,stride));
        if(actual>=best||actual>dryPeak*minimum||(!job.settings.partial&&actual>target))return;
        best=actual;bestAngle=degrees;bestReport=report;
    };
    const float maximum=std::clamp(job.settings.maxAngle,0.f,90.f);
    for(float angle=-maximum;angle<=maximum+.001f;angle+=5.f)trial(angle);
    trial(maximum);trial(-maximum);
    if(searchCentre!=0){const float centre=searchCentre;for(int k=-4;k<=4;++k)trial(std::clamp(centre+float(k),-maximum,maximum));}
    if(bestAngle==0)return;
    synthesize(bestAngle);
    for(int c=0;c<2;++c)std::copy_n(candidate[size_t(c)].begin()+guard,frameN,plan.output[size_t(c)].begin());
    plan.report=bestReport;plan.report.outPeak=float(best);plan.report.angle=bestAngle;plan.report.decision=Decision::accepted;
}
void Engine::stop(){running.store(false,std::memory_order_release);if(worker.joinable())worker.join();}
void Engine::prepare(double rate){
    stop();planner.prepare(rate);frameN=planner.frameSize();guard=planner.guardSize();maximumN=planner.maximumFrameSize();
    // Fixed PDC covers the longest adaptive span, FIR context and worker budget.
    const double sr=std::isfinite(rate)?std::clamp(rate,8000.,384000.):48000.;
    minimumN=int(std::ceil(sr*.040));probeN=int(std::ceil(sr*.027));
    latencyN=maximumN+2*guard+int(std::ceil(sr*.120));ringN=latencyN+maximumN+2*guard+1;
    delay.assign(size_t(ringN),{});
    for(auto& j:jobs)for(auto& x:j.input)x.assign(size_t(planner.snapshotSize()),0.f);
    for(auto& p:plans)for(auto& x:p.output)x.assign(size_t(maximumN),0.f);
    jobWrite=0;jobRead=0;planWrite=0;planRead=0;completedSerial=0;position=0;spanWrite=spanRead=nextStart=playbackStart=playbackEnd=0;++epoch;active=false;activeAccepted=false;current={};accepted=0;skipped=0;late=0;
    running=true;worker=std::thread([this]{run();});
}
void Engine::reset() noexcept {
    ++epoch;position=0;spanWrite=spanRead=nextStart=playbackStart=playbackEnd=0;
    // Release published slots; an old in-flight job is discarded by epoch.
    planRead.store(planWrite.load(std::memory_order_acquire),std::memory_order_release);
    active=false;activeAccepted=false;current={};
}
int Engine::boundaryLength() const noexcept {
    // Place joins in low-energy regions using already buffered context.
    // Rolling energy is O(maximum span), with no allocations or FFT on audio.
    const auto power=[&](int i){const auto x=inputAt(std::int64_t(nextStart)+i);return double(x.l)*x.l+double(x.r)*x.r;};
    const double samplesPerMs=frameN/80.;
    const int transitionMinimum=int(samplesPerMs*(2.*settings.transitionMs+5.))+2*(PeakKernel::half*2+8);
    const int first=std::max(minimumN,std::min(frameN,transitionMinimum));
    double energy=0;for(int i=first-probeN;i<first+probeN;++i)energy+=power(i);
    double best=std::numeric_limits<double>::infinity();int length=frameN;
    const int step=std::max(1,frameN/80);
    for(int n=first;n<=maximumN;++n){
        if(n==frameN||(n-first)%step==0||n==maximumN){
            const double distance=double(n-frameN)/frameN;
            // Tiny regularisation chooses the nominal length in silence.
            const double score=std::max(0.,energy)*(1.+.15*distance*distance)+1.e-16*distance*distance;
            if(score<best){best=score;length=n;}
        }
        energy+=power(n+probeN)-power(n-probeN);
    }
    return length;
}

void Engine::configure(const Settings& s) noexcept {if(!(s==settings)){settings=s;++revision;}}
Stereo Engine::inputAt(std::int64_t p) const noexcept {if(p<0)return {};return delay[size_t(std::uint64_t(p)%std::uint64_t(ringN))];}
void Engine::run(){
    while(running.load(std::memory_order_acquire)){
        const auto read=jobRead.load(std::memory_order_relaxed);
        if(read==jobWrite.load(std::memory_order_acquire)){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        auto& job=jobs[size_t(read%capacity)];const auto write=planWrite.load(std::memory_order_relaxed);
        if(write-planRead.load(std::memory_order_acquire)<capacity){auto& plan=plans[size_t(write%capacity)];
            planner.render(job,plan);planWrite.store(write+1,std::memory_order_release);
        }
        jobRead.store(read+1,std::memory_order_release);completedSerial.store(read+1,std::memory_order_release);
    }
}
void Engine::choose(std::uint64_t frame) noexcept {
    if(active){planRead.store(activeSlot+1,std::memory_order_release);active=false;}
    activeAccepted=false;current={};current.decision=settings.bypass?Decision::bypassed:Decision::late;
    for(;;){const auto read=planRead.load(std::memory_order_relaxed);if(read==planWrite.load(std::memory_order_acquire))break;
        const auto& p=plans[size_t(read%capacity)];
        if(p.epoch!=epoch||p.frame<frame||p.revision!=revision){planRead.store(read+1,std::memory_order_release);continue;}
        if(p.frame>frame)break;
        activeSlot=read;active=true;current=p.report;activeAccepted=p.report.decision==Decision::accepted&&!settings.bypass;
        if(activeAccepted)accepted.fetch_add(1,std::memory_order_relaxed);else if(p.report.decision==Decision::rejected)skipped.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    if(!settings.bypass){late.fetch_add(1,std::memory_order_relaxed);}
}
Sample Engine::process(Stereo x,bool offline) noexcept {
    if(delay.empty())return {x,x,{}};
    if(!std::isfinite(x.l))x.l=0;
    if(!std::isfinite(x.r))x.r=0;
    delay[size_t(position%std::uint64_t(ringN))]=x;
    // Each descriptor exists even if a worker queue is full. A missed plan
    // therefore keeps its entire span dry and is never injected halfway in.
    if(position+1>=nextStart+std::uint64_t(maximumN+guard)){
        const int length=boundaryLength();
        spans[size_t(spanWrite%spans.size())]={nextStart,length};++spanWrite;
        const auto write=jobWrite.load(std::memory_order_relaxed);
        if(write-jobRead.load(std::memory_order_acquire)<capacity){auto& j=jobs[size_t(write%capacity)];j.epoch=epoch;j.revision=revision;j.frame=nextStart;j.length=length;j.settings=settings;
            const auto first=std::int64_t(nextStart)-guard;
            for(int i=0;i<planner.snapshotSize();++i){const auto v=inputAt(first+i);j.input[0][size_t(i)]=v.l;j.input[1][size_t(i)]=v.r;}
            jobWrite.store(write+1,std::memory_order_release);
            if(offline)while(running.load(std::memory_order_acquire)&&completedSerial.load(std::memory_order_acquire)<write+1)std::this_thread::yield();
        }
        nextStart+=std::uint64_t(length);
    }
    const auto outPos=std::int64_t(position)-latencyN;const Stereo dry=inputAt(outPos);Stereo out=dry;
    if(outPos>=0){const auto at=std::uint64_t(outPos);
        if(at>=playbackEnd&&spanRead<spanWrite){const auto span=spans[size_t(spanRead%spans.size())];++spanRead;
            playbackStart=span.start;playbackEnd=span.start+std::uint64_t(span.length);choose(span.start);
        }
        if(activeAccepted&&at>=playbackStart&&at<playbackEnd){const auto& p=plans[size_t(activeSlot%capacity)];const auto index=size_t(at-playbackStart);out={p.output[0][index],p.output[1][index]};}
    }
    ++position;return {dry,out,current};
}
}
