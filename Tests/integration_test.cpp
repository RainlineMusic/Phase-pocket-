#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>
struct TestPlayHead:juce::AudioPlayHead {std::int64_t sample=0;bool playing=true;juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setTimeInSamples(sample);p.setIsPlaying(playing);return p;}};
struct PhaseUITestAccess {static void tick(PhasePocketAudioProcessorEditor& e){e.frameTick();}static void window(PhasePocketAudioProcessorEditor& e){e.window=2.;e.invalidateChrome();}};
static void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static void set(PhasePocketAudioProcessor& p,const char* id,float value){auto* parameter=p.parameters.getParameter(id);parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
int main(int argc,char** argv){
    juce::ScopedJuceInitialiser_GUI gui;
    try{
        PhasePocketAudioProcessor p;p.setNonRealtime(true);p.prepareToPlay(48000,64);juce::MidiBuffer midi;
        const int latency=p.getLatencySamples(),count=latency+16000;std::vector<float> original(size_t(count),0);
        for(int i=0;i<16000;++i)original[size_t(i)]=.1f*std::sin(float(i)*.02f);
        int position=0;const int blocks[]{1,16,64,127,512,4096};
        while(position<count){const int n=juce::jmin(blocks[(position/37)%6],count-position);juce::AudioBuffer<float> buffer(2,n);for(int i=0;i<n;++i)for(int c=0;c<2;++c)buffer.setSample(c,i,original[size_t(position+i)]);p.processBlock(buffer,midi);for(int i=0;i<n;++i)require(buffer.getSample(0,i)==(position+i>=latency?original[size_t(position+i-latency)]:0.f),"processor no-op/block changes");position+=n;}
        set(p,"threshold",-4.27f);set(p,"maxAngle",53);set(p,"partial",1);juce::MemoryBlock state;p.getStateInformation(state);set(p,"threshold",-1);p.setStateInformation(state.getData(),int(state.getSize()));require(std::abs(p.parameters.getRawParameterValue("threshold")->load()+4.27f)<.001f,"state threshold recall");require(p.parameters.getRawParameterValue("partial")->load()>.5f,"mode recall");
        set(p,"threshold",-1);set(p,"maxAngle",90);set(p,"partial",0);
        TestPlayHead playHead;p.setPlayHead(&playHead);
        for(int block=0;block<80;++block){juce::AudioBuffer<float> buffer(2,512);for(int c=0;c<2;++c)for(int n=0;n<512;++n)buffer.setSample(c,n,.2f);p.processBlock(buffer,midi);playHead.sample+=512;}
        playHead.sample=0;juce::AudioBuffer<float> afterSeek(2,256);afterSeek.clear();p.processBlock(afterSeek,midi);require(afterSeek.getMagnitude(0,256)==0,"stale delayed audio after seek");
        p.setPlayHead(nullptr);
        require(p.isActivated(),"prototype must process without a Duck key");
        std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());auto* editor=dynamic_cast<PhasePocketAudioProcessorEditor*>(base.get());require(editor!=nullptr,"editor create");
        // Feed actual DSP audio while the editor is open, then allow VBlank to drain.
        editor->addToDesktop(0);editor->setVisible(true);PhaseUITestAccess::window(*editor);p.reset();
        for(int b=0;b<210;++b){juce::AudioBuffer<float> buffer(2,512);for(int n=0;n<512;++n){const double time=double(b*512+n)/48000.,local=std::fmod(time,.08),a=2*phase::pi*261*local;const double amplitude=(.65+.20*std::sin(time*7.))*std::exp(-std::pow((local-.04)/.012,4.));const float v=float(amplitude*(.8*std::cos(a)+.3*std::cos(2*a+1.8)+.16*std::cos(3*a-.9)));buffer.setSample(0,n,v);buffer.setSample(1,n,v);}p.processBlock(buffer,midi);PhaseUITestAccess::tick(*editor);juce::MessageManager::getInstance()->runDispatchLoopUntil(2);}
        if(argc>1){const juce::File directory=juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);directory.createDirectory();const PocketTheme themes[]{PocketTheme::SolidDark,PocketTheme::Neon,PocketTheme::Amber};const char* names[]{"dark","neon","amber"};
            for(int i=0;i<3;++i){editor->setTheme(themes[i],false);for(int width:{560,800,1200}){editor->setSize(width,juce::roundToInt(width*700./800.));juce::MessageManager::getInstance()->runDispatchLoopUntil(45);PhaseUITestAccess::tick(*editor);auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,width==800?2.f:1.f);auto out=directory.getChildFile(juce::String(names[i])+"-"+juce::String(width)+".png").createOutputStream();require(out!=nullptr,"snapshot file");out->setPosition(0);out->truncate();juce::PNGImageFormat png;require(png.writeImageToStream(image,*out),"snapshot encode");}}
            set(p,"bypass",1);juce::MessageManager::getInstance()->runDispatchLoopUntil(80);PhaseUITestAccess::tick(*editor);auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.f);auto out=directory.getChildFile("bypass.png").createOutputStream();out->setPosition(0);out->truncate();juce::PNGImageFormat png;png.writeImageToStream(image,*out);
        }
        editor->removeFromDesktop();base.reset();p.releaseResources();std::cout<<"Processor PDC, block changes, state and editor PASS\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
