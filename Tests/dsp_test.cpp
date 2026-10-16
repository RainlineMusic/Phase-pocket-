#include "PhaseOptimizer.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <new>
#include <stdexcept>
thread_local bool audioAllocationProbe=false;
thread_local unsigned audioAllocations=0;
void* operator new(std::size_t size){if(audioAllocationProbe)++audioAllocations;if(void* p=std::malloc(size))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace phase;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void initialise(Planner& p,Job& j,Plan& out,double rate){p.prepare(rate);for(auto& x:j.input)x.assign(size_t(p.snapshotSize()),0.f);for(auto& x:out.output)x.assign(size_t(p.frameSize()),0.f);}
static void testNoOp(double rate){
    Engine e;e.prepare(rate);Settings s;s.threshold=-1;e.configure(s);const int count=int(rate*.24)+e.latency();std::vector<Stereo> in(size_t(count),Stereo{});
    for(int i=0;i<count-e.latency();++i)in[size_t(i)]={.1f*float(std::sin(2*pi*331*i/rate)),.1f*float(std::cos(2*pi*713*i/rate))};
    for(int i=0;i<count;++i){const auto v=e.process(in[size_t(i)],true);const auto x=i>=e.latency()?in[size_t(i-e.latency())]:Stereo{};require(v.out.l==x.l&&v.out.r==x.r,"below-threshold null/PDC");}
    require(e.accepted==0,"false processing below threshold");
    e.reset();for(int i=0;i<e.latency()+100;++i){const auto v=e.process({},true);require(v.out.l==0&&v.out.r==0,"reset stale audio");}
    e.configure(Settings{0,90,15,.1f,false,true,true});for(int i=0;i<count;++i){const auto v=e.process(in[size_t(i)],true);require(v.out.l==v.dry.l&&v.out.r==v.dry.r,"bypass null");}
    auto v=e.process({std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()},true);require(std::isfinite(v.out.l)&&std::isfinite(v.out.r),"nonfinite input");
}
static int testCandidates(){
    Planner p;Job j;Plan out;initialise(p,j,out,48000.);const int first=p.guardSize(),n=p.frameSize();j.settings.partial=true;j.settings.threshold=-3.f;j.settings.toleranceDb=.1f;
    int accepted=0;double worst=0;const auto started=std::chrono::steady_clock::now();
    for(int fixture=0;fixture<12;++fixture){
        for(int i=0;i<p.snapshotSize();++i){const double t=double(i-first)/48000.;const double env=std::exp(-std::pow((t-.04)/.012,4.));const double frequency=90+fixture*19;
            const double a=2*pi*frequency*t;
            const float x=float(.75*env*(.80*std::cos(a)+.30*std::cos(2*a+.2*fixture)+.16*std::cos(3*a-.1*fixture)));
            j.input[0][size_t(i)]=x;j.input[1][size_t(i)]=x*(fixture%2?.7f:1.f);
        }
        p.render(j,out);if(out.report.decision==Decision::accepted){++accepted;std::cout<<"fixture="<<fixture<<" saving="<<toDb(out.report.inPeak)-toDb(out.report.outPeak)<<" dB angle="<<out.report.angle<<"\n";require(out.report.outPeak<=out.report.inPeak*dbToGain(-.2f)+1.e-6f,"peak improvement failed");require(std::abs(out.report.rmsDelta)<=j.settings.toleranceDb,"energy guard");
            const float threshold=toDb(out.report.outPeak)+.12f;j.settings.threshold=threshold;j.settings.partial=false;p.render(j,out);require(out.report.decision==Decision::accepted,"strict candidate lost");require(out.report.outPeak<=dbToGain(threshold-.1f)+1.e-6f,"strict threshold violated");
            for(int i=0;i<PeakKernel::half*2+8;++i)for(int c=0;c<2;++c){require(out.output[size_t(c)][size_t(i)]==j.input[size_t(c)][size_t(first+i)],"left boundary changed");require(out.output[size_t(c)][size_t(n-1-i)]==j.input[size_t(c)][size_t(first+n-1-i)],"right boundary changed");}
            if(accepted==1){std::ofstream f("phase_fixture.f32",std::ios::binary);for(int i=0;i<n;++i){const float pair[]{j.input[0][size_t(first+i)],out.output[0][size_t(i)]};f.write(reinterpret_cast<const char*>(pair),sizeof(pair));}}
        }
        j.settings.partial=true;j.settings.threshold=-3;worst=std::max(worst,double(out.report.spectralError));
    }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    std::cout<<"harmonic fixtures accepted="<<accepted<<"/12 spectral="<<worst<<" dB time="<<ms<<" ms\n";
    require(accepted>0,"optimizer never accepts useful candidates");
    j.settings.partial=false;j.settings.threshold=-12;for(auto& x:j.input)std::fill(x.begin(),x.end(),1.f);p.render(j,out);require(out.report.decision==Decision::rejected,"DC falsely accepted");
    for(auto& x:j.input)for(int i=0;i<p.snapshotSize();++i)x[size_t(i)]=i%2?-.99f:.99f;p.render(j,out);require(out.report.decision==Decision::rejected,"Nyquist falsely accepted");
    return accepted;
}
static void testStreaming(){
    const double sr=48000.;Engine engine;engine.prepare(sr);Settings settings;settings.threshold=-2;engine.configure(settings);
    const int count=3*engine.frameSize()+engine.latency();std::vector<Stereo> input(size_t(count),Stereo{}),output(size_t(count),Stereo{});
    for(int i=0;i<3*engine.frameSize();++i){const double t=double(i%engine.frameSize())/sr,a=2*pi*(90+9*19)*t,env=std::exp(-std::pow((t-.04)/.012,4.));const float x=float(.75*env*(.8*std::cos(a)+.3*std::cos(2*a+1.8)+.16*std::cos(3*a-.9)));input[size_t(i)]={x,x};}
    for(int i=0;i<count;++i){const auto v=engine.process(input[size_t(i)],true);output[size_t(i)]=v.out;require(v.out.l==v.out.r,"L=R stereo identity");}
    require(engine.accepted>0,"streaming planner never delivered accepted plan");
    std::cout<<"streaming accepted="<<engine.accepted<<" skipped="<<engine.skipped<<" late="<<engine.late<<" PDC="<<engine.latency()<<" samples\n";
    engine.reset();engine.configure(settings);
    for(int i=0;i<count;++i){const auto v=engine.process(input[size_t(i)],true);require(v.out.l==output[size_t(i)].l,"offline output nondeterministic across reset");}
    for(int polarity:{-1,0}){engine.reset();for(int i=0;i<count;++i){const auto v=engine.process({input[size_t(i)].l,float(polarity)*input[size_t(i)].l},true);require(v.out.r==float(polarity)*v.out.l,"M/S or silent-channel invariance");}}
    std::ofstream f("phase_stream.f32",std::ios::binary);for(int i=engine.latency();i<count;++i){const float pair[]{input[size_t(i-engine.latency())].l,output[size_t(i)].l};f.write(reinterpret_cast<const char*>(pair),sizeof(pair));}
}
// A transient exactly at the former fixed 80 ms join must not be dry-locked.
static void testBoundaryPeaks(){
    for(int offset:{-48,0,48,503}){
        Engine e;e.prepare(48000);Settings settings;settings.threshold=-2;e.configure(settings);
        const int n=12000,centre=3840+offset;std::vector<Stereo> input(size_t(n),Stereo{});
        double dry=0,wet=0;
        for(int i=0;i<n;++i){const double t=double(i-centre)/48000.,a=2*pi*261*t,env=std::exp(-std::pow(t/.012,4.));
            const float x=float(.75*env*(.8*std::cos(a)+.3*std::cos(2*a+1.8)+.16*std::cos(3*a-.9)));input[size_t(i)]={x,x};dry=std::max(dry,double(std::abs(x)));}
        std::ofstream fixture;if(offset==0)fixture.open("phase_boundary.f32",std::ios::binary);
        for(int i=0;i<n+e.latency();++i){const auto y=e.process(i<n?input[size_t(i)]:Stereo{},true);
            if(i>=e.latency()){const int at=i-e.latency();wet=std::max(wet,double(std::abs(y.out.l)));
                if(fixture.is_open()){const float pair[]{input[size_t(at)].l,y.out.l};fixture.write(reinterpret_cast<const char*>(pair),sizeof(pair));}}}
        require(e.accepted>0&&e.late==0,"boundary event skipped or late");require(wet<dry*dbToGain(-.2f),"boundary peak remains dry-locked");
        std::cout<<"boundary offset="<<offset<<" saved="<<toDb(dry)-toDb(wet)<<" dB\n";
    }
}
static void testRealtimeFallbackAndAllocations(){
    Engine e;e.prepare(48000);Settings s;s.threshold=-4;e.configure(s);audioAllocations=0;
    bool fallbackExact=true,finite=true;audioAllocationProbe=true;
    for(int i=0;i<80000;++i){const auto v=e.process({float(.8*std::sin(i*.13)),float(.6*std::cos(i*.077))},false);
        if(v.report.decision==Decision::late)fallbackExact&=v.out.l==v.dry.l&&v.out.r==v.dry.r;
        finite&=std::isfinite(v.out.l)&&std::isfinite(v.out.r);
    }
    audioAllocationProbe=false;require(audioAllocations==0,"audio thread heap allocation");require(fallbackExact,"late plan changed dry output");require(finite,"nonfinite realtime output");
    std::cout<<"realtime allocation probe=0; safe late tiles="<<e.late<<"\n";
}
int main(){try{
    PeakKernel k;std::vector<float> dc(512,1.f);require(std::abs(k.peak(dc,40,470)-1.)<1.e-6,"TP DC calibration");
    for(double rate:{44100.,48000.,96000.,192000.}){testNoOp(rate);std::cout<<"null/bypass/reset @ "<<rate<<" PASS\n";}
    testCandidates();testStreaming();testBoundaryPeaks();testRealtimeFallbackAndAllocations();std::cout<<"PASS\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
