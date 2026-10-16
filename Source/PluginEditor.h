#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UIStyle.h"
#include "GlowRenderer.h"
#include "SoftwareGlow.h"


class PocketLook final:public juce::LookAndFeel_V4 {
public:
    PocketTheme theme=PocketTheme::SolidDark;
    PocketTokens tokens() const{return PocketTokens::forTheme(theme); }
    bool isDark() const{return theme!=PocketTheme::SolidWhite;}
    bool isNeon() const{return theme==PocketTheme::Neon;}
    bool isAmber() const{return theme==PocketTheme::Amber;}
    bool hasGlow() const{return true;}
    juce::Colour pick(juce::uint32 neon,juce::uint32 dark,juce::uint32 white) const;
    juce::Colour ink() const;
    juce::Colour muted() const;
    juce::Colour accent() const;
    juce::Colour accent2() const;
    juce::Colour themedAccent(juce::uint32 neon) const;
    juce::Font getTextButtonFont(juce::TextButton&,int) override;
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
    void drawLinearSlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider::SliderStyle,juce::Slider&) override;
};

class ResettableRangeSlider final:public juce::Slider {
public:
    // Separate per-handle callbacks: double-click (or alt-click) only resets
    // whichever thumb is nearer the click, not both ends of the range at once.
    std::function<void()> onResetMin,onResetMax;
    void mouseDoubleClick(const juce::MouseEvent& e) override {resetNearestThumb(e);}
    // Alt-click resets too (JUCE's built-in alt-click only handles a single value).
    void mouseDown(const juce::MouseEvent& e) override {
        altReset=!e.mods.isPopupMenu()&&e.mods.withoutMouseButtons()==juce::ModifierKeys(juce::ModifierKeys::altModifier);
        if(altReset){resetNearestThumb(e);return;}
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {if(!altReset)juce::Slider::mouseDrag(e);}
    void mouseUp(const juce::MouseEvent& e) override {if(altReset){altReset=false;return;}juce::Slider::mouseUp(e);}
    double valueToProportionOfLength(double value) override {return std::log(juce::jlimit(20.,20000.,value)/20.)/std::log(1000.);}
    double proportionOfLengthToValue(double proportion) override {return 20.*std::pow(1000.,juce::jlimit(0.,1.,proportion));}
private:
    bool altReset=false;
    // Compares the click x to each thumb's own pixel position (via this
    // slider's own value->proportion mapping, same one the LookAndFeel uses
    // to place the thumbs) and fires only the callback for the nearer one.
    void resetNearestThumb(const juce::MouseEvent& e){
        const float w=float(getWidth());
        if(w<=0.f)return;
        const float minX=float(valueToProportionOfLength(getMinValue()))*w;
        const float maxX=float(valueToProportionOfLength(getMaxValue()))*w;
        const bool nearMin=std::abs(e.position.x-minX)<=std::abs(e.position.x-maxX);
        if(nearMin){if(onResetMin)onResetMin();}else{if(onResetMax)onResetMax();}
    }
};

class ModernDial final:public juce::Slider,private juce::Timer {
public:
    // trailing infLabel overrides the "infinity" display text at full deflection
    // (e.g. "AUTO"); empty means keep the default infinity glyph.
    ModernDial(PocketLook&,juce::String,juce::String,juce::String,juce::uint32,bool=false,bool=false,bool=false,juce::String={});
    ~ModernDial() override {stopTimer();}
    bool isAutoValue() const {return infinity&&getValue()>=getMaximum();}
    float valueTextHeight(const juce::String& value) const {
        const float scale=float(getWidth())/(compact?132.f:240.f);
        const float preferred=(compact?14.f:22.f)*scale;
        const float diameter=(compact?60.f:112.f)*scale;
        const float width=juce::GlyphArrangement::getStringWidth(pocketFont(compact?14.f:22.f,true),value)*scale;
        return juce::jlimit(6.f*scale,preferred,preferred*diameter/juce::jmax(1.f,width+1.f));
    }
    void mouseEnter(const juce::MouseEvent& e) override {juce::Slider::mouseEnter(e);animate(.65f);}
    void mouseExit(const juce::MouseEvent& e) override {juce::Slider::mouseExit(e);animate(0);}
    void mouseDown(const juce::MouseEvent& e) override {juce::Slider::mouseDown(e);animate(1);}
    void mouseUp(const juce::MouseEvent& e) override {juce::Slider::mouseUp(e);animate(isMouseOver()?.65f:0);}
    void paint(juce::Graphics&) override;
    juce::String displayedValue() const {
        if(unit=="balance")return juce::String(juce::roundToInt(std::abs(getValue())*100.))+"%";
        if(unit=="dB")return juce::String(std::abs(getValue())<.005?0.:getValue(),2);
        if(unit=="deg")return juce::String(getValue(),0)+juce::String::fromUTF8("°");
        if(title=="Transition")return juce::String(getValue(),1);
        if(isAutoValue()&&unit=="ms")return "AUTO";
        return juce::String(getValue(),title=="Influence"?1:0)+(unit=="%"?"%":" ms");
    }
    juce::String balanceLabel() const {return getValue()<-.0001?"mid":(getValue()>.0001?"side":"M/S");}
    void setDurationMode(bool relative){unit=relative?"%":"ms";subtitle=relative?"Key length":"Legacy length";repaint();}
private:
    PocketLook& look;
    juce::String title,subtitle,unit;
    bool infinity,compact;
    juce::Image body,ringImage;
    std::unique_ptr<juce::Drawable> heading;
    double ringValue=std::numeric_limits<double>::quiet_NaN();
    PocketTheme bodyTheme=PocketTheme::Neon;
    float bodyScale=0,emphasis=0,targetEmphasis=0;
    void animate(float target){targetEmphasis=target;startTimerHz(60);}
    void timerCallback() override {emphasis+=(targetEmphasis-emphasis)*.3f;if(std::abs(targetEmphasis-emphasis)<.01f){emphasis=targetEmphasis;stopTimer();}repaint();}
};

// Numeric vertical-drag slider: standard JUCE attachment handles host gestures,
// automation and keyboard input without introducing a second parameter path.
class HeaderValue final:public juce::Slider {
public:
    HeaderValue(PocketLook& l,bool decibels):look(l),db(decibels){setSliderStyle(juce::Slider::RotaryVerticalDrag);setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);setMouseDragSensitivity(db?180:200);setDoubleClickReturnValue(true,db?0:100);setWantsKeyboardFocus(true);}
    juce::String displayedValue() const {return db?juce::String(std::abs(getValue())<.05?0.:getValue(),1)+" dB":juce::String(getValue(),0)+"%";}
    void paint(juce::Graphics& g) override {
        const auto t=look.tokens();const float scale=float(getHeight())/21.7f;auto r=getLocalBounds().toFloat().reduced(.75f*scale);
        g.setColour(isMouseOverOrDragging()?t.raised.brighter(.12f):t.raised);g.fillRoundedRectangle(r,2.5f*scale);
        g.setColour(t.out.withAlpha(isMouseOverOrDragging()?1.f:.75f));g.drawRoundedRectangle(r,2.5f*scale,juce::jmax(.6f,scale));
        g.setColour(t.ink);g.setFont(pocketFont(14.f*scale));g.drawText(displayedValue(),r,juce::Justification::centred);
    }
private:
    PocketLook& look;bool db;
};

class PhasePocketAudioProcessorEditor final:public juce::AudioProcessorEditor {
public:
    explicit PhasePocketAudioProcessorEditor(PhasePocketAudioProcessor&);
    ~PhasePocketAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void setTheme(PocketTheme,bool persist=true);
private:
    friend struct PhaseUITestAccess;
    PhasePocketAudioProcessor& audioProcessor;
    PocketLook look;
    struct ActivationPanel : juce::Component,juce::FileDragAndDropTarget {
        bool offline=false,dragging=false;
        juce::Image backdrop;
        juce::Rectangle<int> card;
        PocketTokens palette=PocketTokens::forTheme(PocketTheme::SolidDark);
        float uiScale=1.5f;
        std::function<void(const juce::File&)> onFile;
        bool isInterestedInFileDrag(const juce::StringArray& files) override {return offline&&files.size()==1;}
        void fileDragEnter(const juce::StringArray&,int,int) override {dragging=true;repaint();}
        void fileDragExit(const juce::StringArray&) override {dragging=false;repaint();}
        void filesDropped(const juce::StringArray& files,int,int) override {dragging=false;repaint();if(offline&&files.size()==1&&onFile)onFile(juce::File(files[0]));}
        void paint(juce::Graphics& g) override {
            g.fillAll(palette.chassis);
            if(backdrop.isValid())g.drawImage(backdrop,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
            g.setColour(juce::Colours::black.withAlpha(.38f));g.fillAll();
            auto r=card.toFloat();
            for(int i=3;i>0;--i){g.setColour(juce::Colours::black.withAlpha(.07f));g.fillRoundedRectangle(r.expanded(float(i*4)*uiScale).translated(0,float(i*2)*uiScale),(12.f+float(i*2))*uiScale);}
            g.setColour(palette.chassis.brighter(.08f));g.fillRoundedRectangle(r,12*uiScale);
            g.setColour(dragging?palette.out:palette.border.withAlpha(.8f));g.drawRoundedRectangle(r.reduced(.5f),12*uiScale,dragging?1.5f:1.f);
            g.setColour(palette.ink.withAlpha(.06f));g.drawHorizontalLine(card.getY()+1,float(card.getX()+12),float(card.getRight()-12));
        }
    } activationPanel;
    juce::Label activationTitle,activationMessage,activationStatus;
    juce::TextEditor licenseInput;
    juce::TextButton activateButton{"OK"},onlineButton{"Online (Recommended)"},offlineButton{"Offline"},copyDeviceButton{"Copy code"},chooseLicenseButton{"Choose file"};
    juce::TextEditor deviceCodeInput;
    juce::Label deviceCodeLabel;
    std::unique_ptr<juce::FileChooser> licenseChooser;
    bool offlineActivation=false;
    bool activationBackdropDirty=true;
    juce::String lastOnlineMessage;
    void captureActivationBackdrop();
    void updateActivationColours();
    void setActivationMode(bool offline);
    void importLicense(const juce::File&);

    ModernDial threshold{look,"Threshold","dBTP","dB",0};
    ModernDial rotation{look,"Max Phase","degrees","deg",0};
    ModernDial transition{look,"Transition","ms","ms",0,false,false,true};
    ModernDial tolerance{look,"Tolerance","dB","dB",0,false,false,true};
    juce::TextButton settingsButton{"settings"},bypassButton{"power"},freezeButton{"freeze"};
    using SliderAttachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    std::array<std::unique_ptr<SliderAttachment>,4> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
    std::unique_ptr<juce::PropertiesFile> preferences;
    std::unique_ptr<juce::VBlankAttachment> vblank;
#if DUCK_ENABLE_OPENGL
    std::unique_ptr<PocketGlowRenderer> glowRenderer;
    double glAttachTime=0;
    void setOpenGL(bool,bool persist=true);
#endif
    static constexpr int historyCapacity=8192;
    std::array<PocketTrace,historyCapacity> history{};
    std::vector<PocketTrace> frozen;
    std::array<std::vector<float>,3> buckets;
    int cursor=0,filled=0;
    double window=5.,displayTime=0,lastClock=0,lastPacketClock=0,lastLatest=0,lastSignalTime=-1,nextFrameMs=0,resizeStamp=0;
    bool frozenGraph=false,ready=false,capturingBlur=false,bypassTarget=false,chromeValid=false;
    float bypassMix=0;
    juce::Image chrome,blurredSnapshot;
    float chromeScale=1;
    std::uint64_t chromeRevision=0;
#if JUCE_WINDOWS
    juce::Image nativeChrome;
    std::uint64_t nativeChromeRevision=0;
    void setWindowsRenderer(const juce::String&,bool persist=true);
#endif
    juce::Rectangle<int> blurArea,plotArea;
    juce::Rectangle<int> scaled(float,float,float,float) const;
    void frameTick();
    void saveSize();
    void invalidateChrome();
    void showSettingsMenu();
    void paintChrome(juce::Graphics&);
    void graph(juce::Graphics&);
    void captureBlurSnapshot();
    void setParameter(const char*,float);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhasePocketAudioProcessorEditor)
};
