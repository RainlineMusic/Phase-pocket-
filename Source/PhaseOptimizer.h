#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <complex>
#include <cstdint>
#include <thread>
#include <vector>

namespace phase {
constexpr double pi=3.14159265358979323846;
inline float dbToGain(float db){return std::pow(10.f,db*.05f);}
inline float toDb(double a){return float(20.*std::log10(std::max(1.e-12,a)));}
struct Stereo {float l=0,r=0;};
struct Settings {
    float threshold=-1.f,maxAngle=90.f,transitionMs=15.f,toleranceDb=.1f;
    bool partial=false,precise=true,bypass=false;
    bool operator==(const Settings& b) const noexcept {
        return threshold==b.threshold&&maxAngle==b.maxAngle&&transitionMs==b.transitionMs&&toleranceDb==b.toleranceDb&&partial==b.partial&&precise==b.precise&&bypass==b.bypass;
    }
};
enum class Decision {below,accepted,rejected,bypassed,late};
struct Report {
    float inPeak=0,outPeak=0,angle=0,rmsDelta=0,spectralError=0,correlationDelta=0,centroidMs=0;
    Decision decision=Decision::below;
};
// Windowed-sinc interpolation is a model of reconstructed peaks, not a
// certification of the continuous-time peak. Integer phases are exact samples.
class PeakKernel {
public:
    static constexpr int half=24,phases=8,taps=half*2;
    PeakKernel();
    float interpolate(const std::vector<float>& x,int i,int p) const noexcept;
    double peak(const std::vector<float>& x,int first,int last,int stride=1) const noexcept;
private:
    std::array<std::array<double,taps>,phases> h{};
};
struct Job {
    std::array<std::vector<float>,2> input;
    Settings settings;
    std::uint64_t epoch=0,revision=0,frame=0;
    int length=0;
};
struct Plan {
    std::array<std::vector<float>,2> output;
    Report report;
    std::uint64_t epoch=0,revision=0,frame=0;
    int length=0;
};
// All allocation is in prepare(). The planner is owned exclusively by the
// worker. Input includes FIR pre-roll/post-roll and interpolation support.
class Planner {
public:
    void prepare(double rate);
    void render(const Job&,Plan&);
    int frameSize() const noexcept {return nominalN;}
    int maximumFrameSize() const noexcept {return maximumN;}
    int guardSize() const noexcept {return guard;}
    int snapshotSize() const noexcept {return maximumN+2*guard;}
private:
    double sr=48000.;int frameN=3840,nominalN=3840,maximumN=5760,radius=512,guard=600,fftN=8192;
    PeakKernel tp;
    std::vector<std::complex<double>> kernel,fftWork;
    std::array<std::vector<float>,2> q,a,candidate;
    std::vector<float> weight;
    static constexpr int bands=18;
    std::array<double,bands> drySpectrum{};
    static void fft(std::vector<std::complex<double>>&,bool);
    void spectrum(const std::array<std::vector<float>,2>&,std::array<double,bands>&);
    bool guards(const Job&,Report&);
};
struct Sample {Stereo dry,out;Report report;};
// Two bounded SPSC queues; no locks, allocation, OS signalling or phase search
// in process(). Missed realtime deadlines produce exact delayed dry tiles.
class Engine {
public:
    Engine()=default;
    ~Engine(){stop();}
    Engine(const Engine&)=delete;Engine& operator=(const Engine&)=delete;
    void prepare(double);
    void stop();
    void reset() noexcept;
    void configure(const Settings&) noexcept;
    Sample process(Stereo,bool offline=false) noexcept;
    int latency() const noexcept {return latencyN;}
    int frameSize() const noexcept {return frameN;}
    std::atomic<std::uint64_t> accepted{0},skipped{0},late{0};
private:
    static constexpr std::uint64_t capacity=16;
    Planner planner;
    std::array<Job,capacity> jobs;
    std::array<Plan,capacity> plans;
    std::vector<Stereo> delay;
    std::thread worker;
    std::atomic<bool> running{false};
    alignas(64) std::atomic<std::uint64_t> jobWrite{0},jobRead{0};
    alignas(64) std::atomic<std::uint64_t> planWrite{0},planRead{0};
    std::atomic<std::uint64_t> completedSerial{0};
    std::uint64_t epoch=1,revision=1,position=0;
    std::uint64_t activeSlot=0;
    bool active=false,activeAccepted=false;
    int frameN=0,maximumN=0,minimumN=0,probeN=0,guard=0,latencyN=0,ringN=0;
    struct Span {std::uint64_t start=0;int length=0;};
    std::array<Span,32> spans{};
    std::uint64_t spanWrite=0,spanRead=0,nextStart=0,playbackStart=0,playbackEnd=0;
    int boundaryLength() const noexcept;
    Settings settings;
    Report current;
    Stereo inputAt(std::int64_t) const noexcept;
    void run();
    void choose(std::uint64_t) noexcept;
};
}
