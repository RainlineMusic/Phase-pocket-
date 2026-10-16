#pragma once
#include <JuceHeader.h>
#include "PhaseOptimizer.h"
#include "License.h"
struct PocketTrace {float input=0,output=0,saving=0,angle=0;double time=0;};
class PhasePocketAudioProcessor final:public juce::AudioProcessor {
public:
    PhasePocketAudioProcessor();
    ~PhasePocketAudioProcessor() override=default;
    void prepareToPlay(double,int) override;
    void releaseResources() override {engine.stop();}
    void reset() override {engine.reset();transportKnown=false;}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override {process(b,true);}
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioProcessorParameter* getBypassParameter() const override {return parameters.getParameter("bypass");}
    bool hasEditor() const override {return true;}
    const juce::String getName() const override {return JucePlugin_Name;}
    bool acceptsMidi() const override {return false;}bool producesMidi() const override {return false;}bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return getLatencySamples()/juce::jmax(1.,getSampleRate());}
    int getNumPrograms() override {return 1;}int getCurrentProgram() override {return 0;}void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return {}; }void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    bool popTrace(PocketTrace&);
    bool isActivated() const noexcept {return !license||(*license)->active.load(std::memory_order_acquire);}
    juce::String licenseDeviceCode() const {return license?(*license)->deviceCode:juce::String();}
    bool importLicenseFile(const juce::File& f,juce::String& e){return license&&(*license)->importFile(f,e);}
    bool startOnlineActivation(const juce::String& k,juce::String& e){return license&&(*license)->startOnline(k,e);}
    bool onlineActivationBusy() const {return license&&(*license)->onlineBusy();}
    juce::String onlineActivationMessage() const {return license?(*license)->onlineMessage():juce::String();}
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<bool> editorOpen{false},displayBypass{false};
    std::atomic<int> editorWidth{0};
    std::atomic<float> inputTP{-120},outputTP{-120},lastAngle{0},lastRmsDelta{0},lastSpectralError{0};
    std::atomic<std::uint64_t> accepted{0},skipped{0},late{0};
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::unique_ptr<juce::SharedResourcePointer<pocket::LicenseState>> license;
    phase::Engine engine;
    std::array<std::atomic<float>*,7> values{};
    juce::AbstractFifo fifo{8192};std::array<PocketTrace,8192> traces{};
    PocketTrace capture;double time=0,rate=48000.;int captured=0,decimation=96;
    bool transportKnown=false,transportPlaying=false;
    std::int64_t expectedTransportSample=0;
    void process(juce::AudioBuffer<float>&,bool);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhasePocketAudioProcessor)
};
