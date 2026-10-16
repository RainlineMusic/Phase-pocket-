#include "PluginProcessor.h"
#include "PluginEditor.h"
PhasePocketAudioProcessor::PhasePocketAudioProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"PHASE_PARAMETERS",layout()){
    const char* ids[]{"threshold","maxAngle","transition","tolerance","partial","quality","bypass"};
    for(size_t i=0;i<values.size();++i)values[i]=parameters.getRawParameterValue(ids[i]);
#if PHASE_ENABLE_LICENSING
    license=std::make_unique<juce::SharedResourcePointer<pocket::LicenseState>>();(*license)->checkOnOpen();
#endif
}
juce::AudioProcessorValueTreeState::ParameterLayout PhasePocketAudioProcessor::layout(){
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"threshold",1},"Threshold",juce::NormalisableRange<float>{-12,0,.01f},-1.f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"maxAngle",1},"Max Phase",juce::NormalisableRange<float>{0,90,1},90.f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"transition",1},"Transition",juce::NormalisableRange<float>{5,25,.1f},15.f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"tolerance",1},"Tolerance",juce::NormalisableRange<float>{.02f,.30f,.01f},.1f));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"partial",1},"Partial improvement",false));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"quality",1},"Quality",juce::StringArray{"4x","8x"},1));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));return p;
}
void PhasePocketAudioProcessor::prepareToPlay(double sr,int){rate=std::isfinite(sr)?juce::jlimit(8000.,384000.,sr):48000.;engine.prepare(rate);transportKnown=false;setLatencySamples(engine.latency());decimation=juce::jmax(1,int(rate/500.));capture={};captured=0;}
bool PhasePocketAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const {return l.inputBuses.size()==1&&l.outputBuses.size()==1&&l.getMainInputChannelSet()==l.getMainOutputChannelSet()&&(l.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());}
void PhasePocketAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,false);}
void PhasePocketAudioProcessor::process(juce::AudioBuffer<float>& b,bool hostBypass){
    juce::ScopedNoDenormals denormals;if(b.getNumChannels()==0)return;
    // A host may seek/loop without calling reset(). Discard queued audio and
    // plans at a transport discontinuity; keep the editor timeline monotonic.
    if(auto* playHead=getPlayHead()){
        if(const auto position=playHead->getPosition()){
            if(const auto sample=position->getTimeInSamples()){
                const bool playing=position->getIsPlaying();
                if(transportKnown&&(playing!=transportPlaying||(playing&&std::abs(double(*sample)-double(expectedTransportSample))>2.)))engine.reset();
                transportKnown=true;transportPlaying=playing;expectedTransportSample=*sample+b.getNumSamples();
            }else transportKnown=false;
        }else transportKnown=false;
    }else transportKnown=false;
    phase::Settings s;s.threshold=values[0]->load();s.maxAngle=values[1]->load();s.transitionMs=values[2]->load();s.toleranceDb=values[3]->load();s.partial=values[4]->load()>.5f;s.precise=values[5]->load()>.5f;s.bypass=hostBypass||values[6]->load()>.5f||!isActivated();
    engine.configure(s);displayBypass.store(s.bypass,std::memory_order_relaxed);
    auto* l=b.getWritePointer(0);auto* r=b.getNumChannels()>1?b.getWritePointer(1):nullptr;
    const bool show=editorOpen.load(std::memory_order_relaxed),offline=isNonRealtime();phase::Report report;
    for(int n=0;n<b.getNumSamples();++n){const auto v=engine.process({l[n],r?r[n]:l[n]},offline);l[n]=v.out.l;if(r)r[n]=v.out.r;report=v.report;time+=1./rate;
        if(!show){captured=0;continue;}
        capture.input=juce::jmax(capture.input,std::abs(v.dry.l),std::abs(v.dry.r));capture.output=juce::jmax(capture.output,std::abs(v.out.l),std::abs(v.out.r));
        capture.saving=juce::jmax(0.f,phase::toDb(v.report.inPeak)-phase::toDb(v.report.outPeak));capture.angle=v.report.angle;capture.time=time;
        if(++captured>=decimation){int a,size,c,d;fifo.prepareToWrite(1,a,size,c,d);if(size){traces[size_t(a)]=capture;fifo.finishedWrite(1);}capture={};captured=0;}
    }
    inputTP.store(phase::toDb(report.inPeak),std::memory_order_relaxed);outputTP.store(phase::toDb(report.outPeak),std::memory_order_relaxed);lastAngle.store(report.angle,std::memory_order_relaxed);lastRmsDelta.store(report.rmsDelta,std::memory_order_relaxed);lastSpectralError.store(report.spectralError,std::memory_order_relaxed);
    accepted.store(engine.accepted.load(std::memory_order_relaxed));skipped.store(engine.skipped.load(std::memory_order_relaxed));late.store(engine.late.load(std::memory_order_relaxed));
}
bool PhasePocketAudioProcessor::popTrace(PocketTrace& t){int a,size,c,d;fifo.prepareToRead(1,a,size,c,d);if(!size)return false;t=traces[size_t(a)];fifo.finishedRead(1);return true;}
void PhasePocketAudioProcessor::getStateInformation(juce::MemoryBlock& d){auto state=parameters.copyState();state.setProperty("schemaVersion",1,nullptr);state.setProperty("uiWidth",editorWidth.load(),nullptr);if(auto xml=state.createXml())copyXmlToBinary(*xml,d);}
void PhasePocketAudioProcessor::setStateInformation(const void* data,int size){if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName(parameters.state.getType())){
    auto state=juce::ValueTree::fromXml(*xml);
    for(auto* p:getParameters())if(auto* parameter=dynamic_cast<juce::RangedAudioParameter*>(p)){
        auto node=state.getChildWithProperty("id",parameter->getParameterID());if(!node.isValid()){node=juce::ValueTree("PARAM");node.setProperty("id",parameter->getParameterID(),nullptr);node.setProperty("value",parameter->convertFrom0to1(parameter->getDefaultValue()),nullptr);state.appendChild(node,nullptr);}
        const float value=float(node.getProperty("value"));const auto range=parameter->getNormalisableRange();node.setProperty("value",std::isfinite(value)?range.snapToLegalValue(juce::jlimit(range.start,range.end,value)):parameter->convertFrom0to1(parameter->getDefaultValue()),nullptr);
    }
    editorWidth.store(juce::jlimit(0,1500,int(state.getProperty("uiWidth",0))));parameters.replaceState(state);
}}
juce::AudioProcessorEditor* PhasePocketAudioProcessor::createEditor(){if(license)(*license)->checkOnOpen();return new PhasePocketAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new PhasePocketAudioProcessor();}
