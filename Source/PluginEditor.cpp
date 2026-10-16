#include "PluginEditor.h"
namespace {
juce::ColourGradient traceFade(juce::Colour colour){return {colour.withMultipliedAlpha(.08f),50.f,0.f,colour,750.f,0.f,false};}
juce::Font uiFont(float size){return pocketFont(size);}
// Written as raw UTF-8 bytes on purpose: a \u escape in a narrow literal gets
// transcoded to the compiler's execution charset (MSVC without /utf-8 turns it
// into '?'), which is exactly how the dials lost their infinity sign.
constexpr const char* kInfinity="\xe2\x88\x9e";
constexpr const char* kMinusInfinity="-\xe2\x88\x9e";
void text(juce::Graphics& g,const juce::String& s,juce::Rectangle<float> r,float size,juce::Colour c,int align=juce::Justification::centredLeft,float glow=0.f){
    g.setFont(uiFont(juce::jmax(13.2f,size)));
    if(glow>0.f){g.setColour(c.withAlpha(glow));const float o[8][2]={{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,1},{-1,1},{1,-1}};for(auto& d:o)g.drawText(s,r.translated(d[0],d[1]),align);}
    g.setColour(c);g.drawText(s,r,align);
}
void stroke(juce::Graphics& g,const juce::Path& p,juce::Colour c,float width){g.setColour(c);g.strokePath(p,juce::PathStrokeType(width,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));}
juce::String hz(double v){return v>=1000?juce::String(v/1000,1)+" kHz":juce::String(juce::roundToInt(v))+" Hz";}
juce::String timeLabel(double seconds){const int ms=juce::roundToInt(seconds*1000);return ms<1000?juce::String(ms)+" ms":juce::String(seconds,seconds==std::floor(seconds)?0:2)+" s";}
constexpr std::array<double,6> windows{{.1,.25,.5,1.,2.,5.}};
juce::Path smoothPath(const std::vector<juce::Point<float>>& points){
    juce::Path p;if(points.empty())return p;p.startNewSubPath(points.front());
    for(size_t i=1;i+1<points.size();++i){auto mid=(points[i]+points[i+1])*.5f;p.quadraticTo(points[i],mid);}
    if(points.size()>1){p.lineTo(points.back());}
    return p;
}
}
juce::Colour PocketLook::pick(juce::uint32 neon,juce::uint32 dark,juce::uint32 white) const {
    if(theme==PocketTheme::Neon)return juce::Colour(neon);
    if(theme==PocketTheme::SolidDark)return juce::Colour(dark);
    if(theme==PocketTheme::SolidWhite)return juce::Colour(white);
    // Amber: preserve the semantic brightness/alpha of the dark palette while
    // moving it onto the warm brown/orange ramp from the reference UI.
    const auto source=juce::Colour(dark?dark:neon);
    const float b=source.getPerceivedBrightness();
    const float sat=juce::jmap(b,0.f,1.f,.72f,.20f);
    const float value=juce::jlimit(.025f,1.f,b*.93f+.018f);
    return juce::Colour::fromHSV(.078f,sat,value,source.getFloatAlpha());
}
juce::Colour PocketLook::ink() const{return tokens().ink;}
juce::Colour PocketLook::muted() const{return tokens().muted;}
juce::Colour PocketLook::accent() const{return theme==PocketTheme::Amber?juce::Colour(0xffff7126):pick(0xff35d6dc,0xffe8e8e8,0xff2f74d0);}
juce::Colour PocketLook::accent2() const{return theme==PocketTheme::Amber?juce::Colour(0xffffd164):(isNeon()?juce::Colour(0xff35d6dc):accent());}
juce::Colour PocketLook::themedAccent(juce::uint32 neon) const {return isAmber()?(juce::Colour(neon).getHue()>.3f?juce::Colour(0xffffd164):juce::Colour(0xffff7126)):juce::Colour(neon);}
juce::Font PocketLook::getTextButtonFont(juce::TextButton&,int){return uiFont(15);}
void PocketLook::drawButtonBackground(juce::Graphics& g,juce::Button& button,const juce::Colour&,bool hover,bool down){
    if(bool(button.getProperties().getWithDefault("pocket.activationText",false))){
        const auto t=tokens();const auto r=button.getLocalBounds().toFloat().reduced(.5f);
        const bool selected=button.getToggleState()||button.getButtonText()=="Activate";
        const float scale=float(button.getProperties().getWithDefault("pocket.activationScale",1.5f));
        g.setColour(selected?t.out.withAlpha(down?.24f:(hover?.20f:.14f)):(hover?t.raised.brighter(.06f):t.glass));g.fillRoundedRectangle(r,6*scale);
        g.setColour((selected?t.out:t.border).withAlpha(selected?.7f:.8f));g.drawRoundedRectangle(r,6*scale,1);return;
    }
    if(button.getButtonText()=="expand")return;
    const auto t=tokens();auto r=button.getLocalBounds().toFloat().reduced(1);
    if(button.getButtonText()=="freeze"||button.getButtonText()=="listen")r=r.withSizeKeepingCentre(r.getWidth()*20.2746f/32.f,r.getHeight()*20.2746f/32.f);
    g.setColour(hover?t.raised.brighter(.12f):t.raised);g.fillRoundedRectangle(r,2.5f);
    g.setColour(down?t.ink:t.out.withAlpha(.75f));g.drawRoundedRectangle(r,2.5f,1.2f);
}
void PocketLook::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool){
    if(bool(b.getProperties().getWithDefault("pocket.activationText",false))){
        const float scale=float(b.getProperties().getWithDefault("pocket.activationScale",1.5f));
        g.setColour((b.getToggleState()?tokens().out:ink()).withAlpha(b.isEnabled()?1.f:.45f));g.setFont(uiFont(12*scale));g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(juce::roundToInt(6*scale)),juce::Justification::centred,1);return;
    }
    auto r=b.getLocalBounds().toFloat();auto name=b.getButtonText();
    if(name=="freeze"||name=="listen")r=r.withSizeKeepingCentre(r.getWidth()*20.2746f/32.f,r.getHeight()*20.2746f/32.f);
    const auto controlInk=isDark()?tokens().out:ink();
    auto c=r.getCentre();float s=juce::jmin(r.getWidth(),r.getHeight())/40;juce::Path p;
    if(name=="expand"){
        const float direction=b.getToggleState()?-1.f:1.f;
        p.startNewSubPath(c.x-10*s,c.y-3*s*direction);p.lineTo(c.x,c.y+3*s*direction);p.lineTo(c.x+10*s,c.y-3*s*direction);juce::Path outline;juce::PathStrokeType(1.7f*s).createStrokedPath(outline,p);
        juce::DropShadow(ink().withAlpha(.30f),7,{0,0}).drawForPath(g,outline);
        juce::DropShadow(ink().withAlpha(.55f),3,{0,0}).drawForPath(g,outline);
        stroke(g,p,ink().brighter(.2f),1.7f*s);return;
    }
    if(name=="power"){p.addCentredArc(0,1,8,8,0,.65f,juce::MathConstants<float>::twoPi-.65f,true);p.startNewSubPath(0,-10);p.lineTo(0,-1);p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,controlInk,1.7f*s);return;}

    if(name=="listen"){
        p.addCentredArc(0,0,10,11,0,-juce::MathConstants<float>::halfPi,juce::MathConstants<float>::halfPi,true);
        p.addRoundedRectangle(-11,-1,5,11,2);p.addRoundedRectangle(6,-1,5,11,2);
        p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));
        stroke(g,p,b.getToggleState()?accent():controlInk,1.7f*s);return;
    }
    if(name=="freeze"){
        // small snowflake: three crossed arms with barbs, lit up while frozen
        for(int arm=0;arm<3;++arm){
            const float a=juce::MathConstants<float>::halfPi+float(arm)*juce::MathConstants<float>::pi/3.f;
            const float dx=std::cos(a),dy=std::sin(a);
            p.startNewSubPath(-dx*10,-dy*10);p.lineTo(dx*10,dy*10);
            for(float sign:{-1.f,1.f})for(float at:{5.5f,9.f}){
                const float bx=dx*at*sign,by=dy*at*sign;
                for(float spread:{.62f,-.62f}){
                    const float ax=std::cos(a+spread),ay=std::sin(a+spread);
                    p.startNewSubPath(bx,by);p.lineTo(bx+ax*3.2f*sign,by+ay*3.2f*sign);
                }
            }
        }
        p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));
        stroke(g,p,(b.getToggleState()?accent():controlInk).withAlpha(b.isEnabled()?1.f:.35f),1.35f*s);return;
    }
    constexpr int teeth=10;for(int i=0;i<teeth*4;++i){float a=float(i)*juce::MathConstants<float>::twoPi/float(teeth*4)-juce::MathConstants<float>::halfPi;float radius=(i%4==1||i%4==2)?10.f:7.7f;auto pt=juce::Point<float>(std::cos(a)*radius,std::sin(a)*radius);if(i==0)p.startNewSubPath(pt);else p.lineTo(pt);}p.closeSubPath();p.applyTransform(juce::AffineTransform::scale(s).translated(c.x,c.y));stroke(g,p,controlInk,1.65f*s);g.setColour(controlInk);g.drawEllipse(c.x-3.2f*s,c.y-3.2f*s,6.4f*s,6.4f*s,1.65f*s);
}
void PocketLook::drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float minPos,float maxPos,juce::Slider::SliderStyle style,juce::Slider& slider){
    auto t=tokens();
    const float scale=float(slider.getWidth())/670.f;
    const float cy=float(y)+float(h)*.5f,left=style==juce::Slider::TwoValueHorizontal?minPos:float(x),right=style==juce::Slider::TwoValueHorizontal?maxPos:pos;
    const auto light=theme==PocketTheme::SolidDark?juce::Colour(0xffe2f5f8):(slider.getName()=="key"?t.key:t.out);
    g.setColour(t.glass.darker(.6f));g.fillRoundedRectangle(float(x),cy-4*scale,float(w),8*scale,3*scale);
    g.setColour(t.border.withAlpha(.6f));g.drawLine(float(x),cy+4*scale,float(x+w),cy+4*scale,.6f*scale);
    g.setColour(light.withAlpha(.18f));g.fillRoundedRectangle(left,cy-6*scale,juce::jmax(.1f,right-left),12*scale,4*scale);
    g.setColour(light);g.fillRect(left,cy-2.5f*scale,juce::jmax(.1f,right-left),5*scale);
    auto thumb=[&](float px){auto r=juce::Rectangle<float>(13*scale,13*scale).withCentre({px,cy});
        g.setColour(juce::Colours::black.withAlpha(.45f));g.fillRoundedRectangle(r.translated(scale,2*scale),2*scale);
        g.setGradientFill(juce::ColourGradient(t.out.interpolatedWith(t.raised,.6f),r.getX(),r.getY(),t.glass,r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,2*scale);
        g.setColour(t.ink.withAlpha(.25f));g.drawRoundedRectangle(r,2*scale,.65f*scale);};
    if(style==juce::Slider::TwoValueHorizontal){thumb(minPos);thumb(maxPos);}else thumb(pos);
}
ModernDial::ModernDial(PocketLook& l,juce::String t,juce::String sub,juce::String u,juce::uint32 a,bool inf,bool infMin,bool compactDial,juce::String infLabel):look(l),title(t),subtitle(sub),unit(u),infinity(inf),compact(compactDial){juce::ignoreUnused(a,infMin,infLabel);setSliderStyle(juce::Slider::RotaryVerticalDrag);setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);setName(t);setWantsKeyboardFocus(true);}
void ModernDial::paint(juce::Graphics& g){
    const auto t=look.tokens();const float designWidth=compact?132.f:240.f,designHeight=compact?135.f:250.f;
    const float factor=float(getWidth())/designWidth;
    const float scale=g.getInternalContext().getPhysicalPixelScaleFactor();
    const int pw=juce::jmax(1,juce::roundToInt(getWidth()*scale)),ph=juce::jmax(1,juce::roundToInt(getHeight()*scale));
    const juce::Point<float> centre=compact?juce::Point<float>{66.26f,75.40f}:juce::Point<float>{119.27f,135.27f};
    const float radius=compact?49.68f:97.5f,faceRadius=compact?31.34f:60.f;
    if(!body.isValid()||body.getWidth()!=pw||body.getHeight()!=ph||bodyTheme!=look.theme||std::abs(bodyScale-scale)>.001f){
        body=juce::Image(juce::Image::ARGB,pw,ph,true,juce::SoftwareImageType());bodyTheme=look.theme;bodyScale=scale;
        ringValue=std::numeric_limits<double>::quiet_NaN();juce::Graphics bg(body);bg.addTransform(juce::AffineTransform::scale(scale*factor));
        if(look.theme==PocketTheme::SolidDark){
            static const auto large=juce::ImageFileFormat::loadFrom(BinaryData::DialLarge_png,BinaryData::DialLarge_pngSize);
            static const auto small=juce::ImageFileFormat::loadFrom(BinaryData::DialSmall_png,BinaryData::DialSmall_pngSize);
            bg.setImageResamplingQuality(juce::Graphics::highResamplingQuality);bg.drawImage(compact?small:large,{0,0,designWidth,designHeight},juce::RectanglePlacement::stretchToFit);
            // Cool, deeper material grade is cached with the SVG-derived body.
            bg.setGradientFill(juce::ColourGradient(juce::Colour(0xff142837).withAlpha(.16f),centre.x-radius,centre.y-radius,juce::Colour(0xff020b13).withAlpha(.40f),centre.x+radius,centre.y+radius,false));
            bg.fillEllipse(centre.x-radius,centre.y-radius,2*radius,2*radius);
        }else{
            auto rim=juce::Rectangle<float>(2*radius,2*radius).withCentre(centre);
            bg.setGradientFill(juce::ColourGradient(t.raised.brighter(.2f),centre.x-radius,centre.y-radius,t.glass.darker(.15f),centre.x+radius,centre.y+radius,false));bg.fillEllipse(rim);
            bg.setColour(t.glass.darker(.5f));bg.drawEllipse(rim,compact?5.f:6.f);
            auto face=juce::Rectangle<float>(2*faceRadius,2*faceRadius).withCentre(centre);
            bg.setGradientFill(juce::ColourGradient(t.glass,centre.x-faceRadius,centre.y-faceRadius,t.raised,centre.x+faceRadius,centre.y+faceRadius,false));bg.fillEllipse(face);
            bg.setColour(t.border);bg.drawEllipse(face,1.5f);
        }
        heading.reset();
        {
            juce::GlyphArrangement glyphs;glyphs.addLineOfText(pocketFont(14.f),title,0,0);const float width=glyphs.getBoundingBox(0,-1,true).getWidth(),r=radius+11.f;
            bg.setColour(t.ink);for(int i=0;i<glyphs.getNumGlyphs();++i){const auto& glyph=glyphs.getGlyph(i);juce::Path path;glyph.createPath(path);const float x=glyph.getBounds().getCentreX(),angle=(x-width*.5f)/r;bg.fillPath(path,juce::AffineTransform::translation(-x,0).rotated(angle).translated(centre.x+std::sin(angle)*r,centre.y-std::cos(angle)*r));}
        }
        if(heading){heading->replaceColour(juce::Colour(0xffced6e2),t.ink);heading->draw(bg,1.f);}
    }
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImageTransformed(body,juce::AffineTransform::scale(1.f/bodyScale));
    // Cache the blurred ring independently; audio-driven metering never rebuilds it.
    const float proportion=float(valueToProportionOfLength(getValue()));
    if(!ringImage.isValid()||ringValue!=double(proportion)||ringImage.getWidth()!=pw||ringImage.getHeight()!=ph){
        ringValue=double(proportion);ringImage=juce::Image(juce::Image::ARGB,pw,ph,true,juce::SoftwareImageType());juce::Graphics rg(ringImage);rg.addTransform(juce::AffineTransform::scale(scale*factor));
        const float start=juce::MathConstants<float>::pi,end=start+juce::MathConstants<float>::twoPi*proportion;
        const auto colour=look.theme==PocketTheme::SolidDark?juce::Colour(0xfff5f8ff):(title=="Influence"||title=="Output"?t.out:t.neutral);
        juce::Path arc;
        if(proportion>=.99999f)arc.addEllipse(centre.x-radius,centre.y-radius,2*radius,2*radius);
        else if(proportion>0)arc.addCentredArc(centre.x,centre.y,radius,radius,0,start,end,true);
        // Rasterised once per value/size/theme, no idle glow animation.
        if(!arc.isEmpty()){
            juce::Path outline;juce::PathStrokeType(compact?4.2f:4.6f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded).createStrokedPath(outline,arc);
            juce::DropShadow(colour.withAlpha(.5f),14,{0,0}).drawForPath(rg,outline);
            juce::DropShadow(colour.withAlpha(.9f),5,{0,0}).drawForPath(rg,outline);
            stroke(rg,arc,colour,compact?4.2f:4.6f);
        }
        const float inner=faceRadius+8.f,outer=inner+(compact?9.f:16.f);
        rg.setColour(t.ink.withAlpha(.75f));rg.drawLine(centre.x+inner*std::sin(end),centre.y-inner*std::cos(end),centre.x+outer*std::sin(end),centre.y-outer*std::cos(end),1.2f);
    }
    g.drawImageTransformed(ringImage,juce::AffineTransform::scale(1.f/bodyScale));
    juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(factor));
    if(emphasis>.001f){g.setColour(t.ink.withAlpha(emphasis*.10f));g.drawEllipse(centre.x-faceRadius,centre.y-faceRadius,2*faceRadius,2*faceRadius,1.f);}
    const auto value=displayedValue();
    g.setFont(pocketFont(valueTextHeight(value)/factor));g.setColour(t.ink);
    g.drawText(value,juce::Rectangle<float>{centre.x-faceRadius,centre.y-(compact?15.f:19.f),2*faceRadius,compact?25.f:35.f},juce::Justification::centred);
    const auto detail=unit=="balance"?balanceLabel():(unit=="dB"?juce::String(title=="Threshold"?"dBTP":"dB"):(isAutoValue()?juce::String("AUTO"):subtitle));
    g.setFont(pocketFont(compact?11.f:13.f));g.setColour(t.muted);g.drawText(detail,juce::Rectangle<float>{centre.x-faceRadius,centre.y+(compact?8.f:17.f),2*faceRadius,20.f},juce::Justification::centred);
}

PhasePocketAudioProcessorEditor::PhasePocketAudioProcessorEditor(PhasePocketAudioProcessor& p):AudioProcessorEditor(&p),audioProcessor(p){
    juce::PropertiesFile::Options options;options.applicationName="PhasePocket";options.filenameSuffix="settings";options.folderName="RainlineMusic";options.osxLibrarySubFolder="Application Support";preferences=std::make_unique<juce::PropertiesFile>(options);
    const auto saved=preferences->getValue("phasePocket.ui.theme","solidDark");look.theme=saved=="neon"?PocketTheme::Neon:(saved=="amber"?PocketTheme::Amber:PocketTheme::SolidDark);
    window=juce::jlimit(.5,10.,preferences->getDoubleValue("phasePocket.ui.graphWindow",5.));
    setLookAndFeel(&look);setOpaque(true);setResizable(true,true);
    for(auto* c:std::initializer_list<juce::Component*>{&threshold,&rotation,&transition,&tolerance,&settingsButton,&bypassButton,&freezeButton})addAndMakeVisible(c);
    const char* ids[]{"threshold","maxAngle","transition","tolerance"};juce::Slider* dials[]{&threshold,&rotation,&transition,&tolerance};const double defaults[]{-1.,90.,15.,.1};
    for(size_t i=0;i<4;++i){attachments[i]=std::make_unique<SliderAttachment>(p.parameters,ids[i],*dials[i]);dials[i]->setDoubleClickReturnValue(true,defaults[i]);dials[i]->onValueChange=[this]{repaint(plotArea);};}
    bypassButton.setClickingTogglesState(true);bypassAttach=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"bypass",bypassButton);
    settingsButton.onClick=[this]{showSettingsMenu();};freezeButton.setClickingTogglesState(true);freezeButton.onClick=[this]{frozenGraph=freezeButton.getToggleState();frozen.clear();if(frozenGraph)for(int i=0;i<filled;++i)frozen.push_back(history[size_t((cursor-filled+i+historyCapacity)%historyCapacity)]);invalidateChrome();};
    for(auto& b:buckets)b.resize(1200);
    frozen.reserve(historyCapacity);
    const int width=juce::jlimit(560,1400,p.editorWidth.load()>0?p.editorWidth.load():preferences->getIntValue("phasePocket.ui.width",800));
    setResizeLimits(560,490,1400,1225);getConstrainer()->setFixedAspectRatio(800./700.);setSize(width,juce::roundToInt(width*700./800.));
    addChildComponent(activationPanel);
    activationPanel.setOpaque(true);
    for(auto* button:{&activateButton,&onlineButton,&offlineButton,&copyDeviceButton,&chooseLicenseButton})button->getProperties().set("pocket.activationText",true);
    for(auto* c:std::initializer_list<juce::Component*>{&activationTitle,&activationMessage,&activationStatus,&licenseInput,&activateButton,&onlineButton,&offlineButton,&deviceCodeInput,&deviceCodeLabel,&copyDeviceButton,&chooseLicenseButton})activationPanel.addAndMakeVisible(c);
    activationTitle.setFont(pocketFont(19,false,true));activationMessage.setFont(uiFont(12));licenseInput.setFont(uiFont(13));deviceCodeInput.setFont(uiFont(11));deviceCodeLabel.setFont(uiFont(12));
    activationTitle.setText("Phase Pocket",juce::dontSendNotification);activateButton.setButtonText("Activate");
    activationTitle.setJustificationType(juce::Justification::centred);
    activationMessage.setJustificationType(juce::Justification::centred);
    activationStatus.setJustificationType(juce::Justification::centred);
    licenseInput.setMultiLine(false);licenseInput.setInputRestrictions(64);licenseInput.setTextToShowWhenEmpty("PHAS-7K3M-9X2P-6R8N-4W5T",juce::Colours::grey);
    deviceCodeInput.setMultiLine(true,true);deviceCodeInput.setReadOnly(true);deviceCodeInput.setText(pocket::displayDeviceCode(audioProcessor.licenseDeviceCode()),false);
    for(auto* input:{&licenseInput,&deviceCodeInput}){input->setIndents(8,6);input->setScrollbarsShown(false);}
    deviceCodeLabel.setText("Device code",juce::dontSendNotification);
    onlineButton.onClick=[this]{setActivationMode(false);};offlineButton.onClick=[this]{setActivationMode(true);};
    copyDeviceButton.onClick=[this]{juce::SystemClipboard::copyTextToClipboard(pocket::displayDeviceCode(audioProcessor.licenseDeviceCode()));};
    activateButton.onClick=[this]{juce::String error;lastOnlineMessage.clear();if(audioProcessor.startOnlineActivation(licenseInput.getText(),error))activationStatus.setText("Activating...",juce::dontSendNotification);else activationStatus.setText(error,juce::dontSendNotification);};
    licenseInput.onReturnKey=[this]{if(!audioProcessor.onlineActivationBusy())activateButton.triggerClick();};
    activationPanel.onFile=[this](const juce::File& f){importLicense(f);};
    chooseLicenseButton.onClick=[this]{
        if(licenseChooser)return;
        licenseChooser=std::make_unique<juce::FileChooser>("Select Phase Pocket license",juce::File(),"*.phaselicense;*.key;*.txt");
        juce::Component::SafePointer<PhasePocketAudioProcessorEditor> safe(this);
        licenseChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& chooser){
            if(safe){const auto file=chooser.getResult();if(file.existsAsFile())safe->importLicense(file);safe->licenseChooser.reset();}
        });
    };
    setActivationMode(false);
    activationPanel.setVisible(!audioProcessor.isActivated());resized();
    ready=true;p.editorWidth.store(width);PocketTrace discard;for(int i=0;i<8192&&p.popTrace(discard);++i){}p.editorOpen.store(true);
    vblank=std::make_unique<juce::VBlankAttachment>(this,[this]{const auto now=juce::Time::getMillisecondCounterHiRes();const double gap=window>=2.?1000./30.:1000./60.;if(now<nextFrameMs)return;nextFrameMs=now+gap;frameTick();});
}
PhasePocketAudioProcessorEditor::~PhasePocketAudioProcessorEditor(){vblank.reset();licenseChooser.reset();
#if DUCK_ENABLE_OPENGL
    if(glowRenderer)glowRenderer->stop();
    glowRenderer.reset();
#endif
    saveSize();audioProcessor.editorOpen=false;setLookAndFeel(nullptr);
}
juce::Rectangle<int> PhasePocketAudioProcessorEditor::scaled(float x,float y,float w,float h) const {const float s=float(getWidth())/800;return juce::Rectangle<float>(x*s,y*s,w*s,h*s).toNearestInt();}
void PhasePocketAudioProcessorEditor::saveSize(){if(!ready)return;audioProcessor.editorWidth=getWidth();preferences->setValue("phasePocket.ui.width",getWidth());preferences->saveIfNeeded();resizeStamp=0;}
void PhasePocketAudioProcessorEditor::invalidateChrome(){chromeValid=false;activationBackdropDirty=true;repaint();}
void PhasePocketAudioProcessorEditor::setTheme(PocketTheme t,bool persist){look.theme=t==PocketTheme::SolidWhite?PocketTheme::SolidDark:t;if(persist){preferences->setValue("phasePocket.ui.theme",t==PocketTheme::Neon?"neon":(t==PocketTheme::Amber?"amber":"solidDark"));preferences->saveIfNeeded();}for(auto* c:getChildren())c->repaint();blurredSnapshot={};updateActivationColours();invalidateChrome();}
void PhasePocketAudioProcessorEditor::setParameter(const char* id,float value){if(auto* p=audioProcessor.parameters.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();}}
void PhasePocketAudioProcessorEditor::showSettingsMenu(){
    juce::PopupMenu root,themes,hist,mode,quality;
    themes.addItem(1,"Solid Dark",true,look.theme==PocketTheme::SolidDark);themes.addItem(2,"Neon",true,look.theme==PocketTheme::Neon);themes.addItem(3,"Amber",true,look.theme==PocketTheme::Amber);root.addSubMenu("Theme",themes);
    const double seconds[]{.5,1,2,5,10};for(int i=0;i<5;++i)hist.addItem(10+i,timeLabel(seconds[i]),true,std::abs(window-seconds[i])<.01);root.addSubMenu("Graph window",hist);
    const bool partial=audioProcessor.parameters.getRawParameterValue("partial")->load()>.5f;mode.addItem(20,"Strict",true,!partial);mode.addItem(21,"Partial improvement",true,partial);root.addSubMenu("Acceptance",mode);
    const bool precise=audioProcessor.parameters.getRawParameterValue("quality")->load()>.5f;quality.addItem(30,"4x true peak",true,!precise);quality.addItem(31,"8x true peak",true,precise);root.addSubMenu("Quality",quality);
    root.addSeparator();root.addItem(0,"Analysis: 80 ms / linked stereo",false);root.addItem(0,"PDC: "+juce::String(audioProcessor.getLatencySamples()*1000./juce::jmax(1.,audioProcessor.getSampleRate()),1)+" ms (fixed)",false);
#if DUCK_ENABLE_OPENGL && ! JUCE_WINDOWS
    root.addItem(40,"OpenGL",true,glowRenderer!=nullptr);
#endif
    juce::StringArray names;
#if JUCE_WINDOWS
    if(auto* peer=getPeer()){names=peer->getAvailableRenderingEngines();juce::PopupMenu renderers;for(int i=0;i<names.size();++i)renderers.addItem(100+i,names[i],true,peer->getCurrentRenderingEngine()==i);root.addSubMenu("Renderer",renderers);}
#endif
    auto safe=juce::Component::SafePointer<PhasePocketAudioProcessorEditor>(this);root.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(settingsButton),[safe,names](int id){if(!safe)return;
        if(id>=1&&id<=3){const PocketTheme t[]{PocketTheme::SolidDark,PocketTheme::Neon,PocketTheme::Amber};safe->setTheme(t[id-1]);}
        if(id>=10&&id<15){const double seconds[]{.5,1,2,5,10};safe->window=seconds[id-10];safe->preferences->setValue("phasePocket.ui.graphWindow",safe->window);safe->preferences->saveIfNeeded();safe->invalidateChrome();}
        if(id==20||id==21)safe->setParameter("partial",id==21?1.f:0.f);
        if(id==30||id==31)safe->setParameter("quality",id==31?1.f:0.f);
#if DUCK_ENABLE_OPENGL && ! JUCE_WINDOWS
        if(id==40)safe->setOpenGL(!safe->glowRenderer);
#endif
#if JUCE_WINDOWS
        if(juce::isPositiveAndBelow(id-100,names.size()))safe->setWindowsRenderer(names[id-100]);
#endif
    });
}
void PhasePocketAudioProcessorEditor::resized(){
    settingsButton.setBounds(scaled(716,12,31,31));bypassButton.setBounds(scaled(755,12,31,31));freezeButton.setBounds(scaled(759,321,32,32));
    threshold.setBounds(scaled(45,385,240,250));rotation.setBounds(scaled(515,385,240,250));transition.setBounds(scaled(334,374,132,135));tolerance.setBounds(scaled(334,510,132,135));
    plotArea=scaled(28,99,714,227);blurArea=scaled(0,56,800,644).getIntersection(getLocalBounds());chromeValid=false;blurredSnapshot={};activationBackdropDirty=true;
    if(ready)resizeStamp=juce::Time::getMillisecondCounterHiRes();
    activationPanel.setBounds(getLocalBounds());
    const float baseHeight=offlineActivation?318.f:244.f;
    // Enlarge all content together. At the minimum editor size, fit the card
    // proportionally rather than clipping controls outside the host window.
    const float scale=juce::jmin(1.5f,float(getWidth()-24)/360.f,float(getHeight()-24)/baseHeight);
    activationPanel.uiScale=scale;
    const int cardWidth=juce::roundToInt(360*scale),cardHeight=juce::roundToInt(baseHeight*scale);
    activationPanel.card=juce::Rectangle<int>(cardWidth,cardHeight).withCentre(getLocalBounds().getCentre());
    activationPanel.palette=look.tokens();
    const auto card=activationPanel.card;
    auto bounds=[&](float x,float y,float w,float h){return (juce::Rectangle<float>(x,y,w,h)*scale).toNearestInt().translated(card.getX(),card.getY());};
    activationTitle.setBounds(bounds(20,20,320,26));
    onlineButton.setBounds(bounds(20,62,156,32));offlineButton.setBounds(bounds(184,62,156,32));
    activationMessage.setBounds(bounds(20,104,320,44));activationStatus.setBounds(bounds(20,148,320,36));
    licenseInput.setBounds(bounds(20,190,230,32));activateButton.setBounds(bounds(258,190,82,32));
    deviceCodeLabel.setBounds(bounds(20,184,320,18));deviceCodeInput.setBounds(bounds(20,208,320,44));
    copyDeviceButton.setBounds(bounds(20,266,156,32));chooseLicenseButton.setBounds(bounds(184,266,156,32));
    activationTitle.setFont(pocketFont(19*scale,false,true));activationMessage.setFont(uiFont(12*scale));activationStatus.setFont(uiFont(12*scale));deviceCodeLabel.setFont(uiFont(12*scale));
    licenseInput.setFont(uiFont(13*scale));deviceCodeInput.setFont(uiFont(11*scale));
    for(auto* input:{&licenseInput,&deviceCodeInput})input->setIndents(juce::roundToInt(8*scale),juce::roundToInt(6*scale));
    for(auto* button:{&activateButton,&onlineButton,&offlineButton,&copyDeviceButton,&chooseLicenseButton})button->getProperties().set("pocket.activationScale",scale);
    updateActivationColours();
    activationBackdropDirty=true;
    activationPanel.toFront(false);
}
void PhasePocketAudioProcessorEditor::paintChrome(juce::Graphics& g){
    const float scale=juce::jlimit(.75f,4.f,g.getInternalContext().getPhysicalPixelScaleFactor());const int w=juce::jmax(1,juce::roundToInt(getWidth()*scale)),h=juce::jmax(1,juce::roundToInt(getHeight()*scale));
    if(!chromeValid||chrome.getWidth()!=w||chrome.getHeight()!=h){chrome=juce::Image(juce::Image::ARGB,w,h,true,juce::SoftwareImageType());chromeScale=scale;++chromeRevision;juce::Graphics cg(chrome);cg.addTransform(juce::AffineTransform::scale(scale*float(getWidth())/800));const auto t=look.tokens();
        cg.setGradientFill(juce::ColourGradient(t.chassis,400,240,t.chassis.darker(.2f),0,700,true));cg.fillRect(0.f,0.f,800.f,700.f);
        juce::Random noise(0xD0C);for(int i=0;i<5000;++i){cg.setColour((i%2?juce::Colours::white:juce::Colours::black).withAlpha(.012f));cg.fillRect(float(noise.nextInt(800)),float(noise.nextInt(700)),1.f,1.f);}
        cg.setColour(t.brand);cg.setFont(pocketFont(17,false,true));cg.drawText("PHASE",11,8,96,23,juce::Justification::centredLeft);cg.setFont(pocketFont(11));cg.drawText("POCKET",12,30,90,16,juce::Justification::centredLeft);
        text(cg,"Selective phase optimizer",{133,13,350,30},14,t.muted);
        for(float y:{53.f,359.f,650.f}){cg.setColour(juce::Colours::black.withAlpha(.5f));cg.fillRect(0.f,y,800.f,3.f);cg.setColour(t.ink.withAlpha(.05f));cg.drawLine(0,y+3,800,y+3,.7f);}
        const auto glass=t.glass.darker(.3f);cg.setGradientFill(juce::ColourGradient(glass.brighter(.035f),0,57,glass.darker(.18f),0,359,false));cg.fillRect(0.f,57.f,800.f,302.f);
        cg.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(.7f),0,57,juce::Colours::transparentBlack,0,71,false));cg.fillRect(0.f,57.f,800.f,14.f);
        text(cg,"PEAK HISTORY",{28,65,220,24},13,t.ink);text(cg,"IN",{570,65,40,24},12,t.key);text(cg,"OUT",{620,65,44,24},12,t.out);text(cg,"SAVED",{681,65,66,24},12,t.neutral);
        for(int db=0;db>=-12;db-=3){const float y=99.f-float(db)/12.f*227.f;cg.setColour(db==0?t.major:t.minor);cg.drawLine(28,y,742,y,.8f);text(cg,juce::String(db),{750,y-10,44,20},11,t.muted);}
        for(int i=0;i<6;++i){const float x=28+714.f*i/5.f;cg.setColour(t.minor.withAlpha(.6f));cg.drawLine(x,99,x,326,.7f);}
        text(cg,"-"+timeLabel(window),{28,330,130,24},11,t.muted);text(cg,frozenGraph?"FROZEN":"NOW",{650,330,92,24},11,t.muted,juce::Justification::centredRight);
        chromeValid=true;
    }
    const juce::Image* background=&chrome;
#if JUCE_WINDOWS
    if(auto* peer=getPeer()){const auto engines=peer->getAvailableRenderingEngines();const int engine=peer->getCurrentRenderingEngine();if(juce::isPositiveAndBelow(engine,engines.size())&&engines[engine].containsIgnoreCase("Direct2D")){if(!nativeChrome.isValid()||nativeChromeRevision!=chromeRevision){nativeChrome=juce::NativeImageType().convert(chrome);nativeChromeRevision=chromeRevision;}if(nativeChrome.isValid())background=&nativeChrome;}}
#endif
    juce::Graphics::ScopedSaveState save(g);
#if DUCK_ENABLE_OPENGL
    if(glowRenderer&&glowRenderer->ready.load()&&glowRenderer->presented.load()&&!capturingBlur&&bypassMix<.5f)g.excludeClipRegion(plotArea);
#endif
    g.drawImage(*background,getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
}
void PhasePocketAudioProcessorEditor::graph(juce::Graphics& g){
    const int count=frozenGraph?int(frozen.size()):filled;if(count<2)return;
    auto at=[&](int i)->const PocketTrace& {return frozenGraph?frozen[size_t(i)]:history[size_t((cursor-count+i+historyCapacity)%historyCapacity)];};
    const double now=frozenGraph?at(count-1).time:displayTime;
#if JUCE_WINDOWS
    constexpr int maximum=800;
#else
    constexpr int maximum=1200;
#endif
    const int columns=juce::jlimit(2,maximum,juce::roundToInt(714.f*g.getInternalContext().getPhysicalPixelScaleFactor()));
    for(auto& b:buckets)std::fill_n(b.begin(),columns,-1.f);
    const auto endBin=std::int64_t(std::floor(now/window*double(columns-1)));const auto startBin=endBin-columns+1;
    for(int i=0;i<count;++i){const auto& v=at(i);const auto index=std::int64_t(std::floor(v.time/window*double(columns-1)))-startBin;if(index<0||index>=columns)continue;const size_t n=size_t(index);buckets[0][n]=juce::jmax(buckets[0][n],v.input);buckets[1][n]=juce::jmax(buckets[1][n],v.output);buckets[2][n]=juce::jmax(buckets[2][n],v.saving);}
    // Telemetry is 500 Hz; physical-pixel columns may be denser. Empty
    // buckets are absent measurements, not zero-amplitude samples. Interpolate
    // only between real measurements, preserving every measured peak maximum.
    for(auto& values:buckets){int previous=-1;for(int i=0;i<columns;++i)if(values[size_t(i)]>=0){if(previous>=0&&i-previous>1){const float left=values[size_t(previous)],right=values[size_t(i)];for(int k=previous+1;k<i;++k)values[size_t(k)]=left+(right-left)*float(k-previous)/float(i-previous);}previous=i;}}
    juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion({28,99,714,227});const auto t=look.tokens();
    const float shift=float(now/window*double(columns-1)-double(endBin));
    for(int kind=0;kind<3;++kind){juce::Path line,area;bool started=false;const auto colour=kind==0?t.key:(kind==1?t.out:t.neutral);
        for(int i=0;i<columns;++i){const float x=28.f+(float(i)-shift)*714.f/float(columns-1);const float db=phase::toDb(buckets[size_t(kind)][size_t(i)]);const float y=kind==2?99.f+juce::jlimit(0.f,12.f,buckets[2][size_t(i)])/12.f*227.f:99.f-juce::jlimit(-12.f,0.f,db)/12.f*227.f;
            if(!started){line.startNewSubPath(x,y);area.startNewSubPath(x,326);area.lineTo(x,y);started=true;}else{line.lineTo(x,y);area.lineTo(x,y);}
            if(kind<2&&db>0){g.setColour(juce::Colour(0xffff766d));g.drawLine(x,99,x,103,1.f);}
        }
        if(kind<2){area.lineTo(742,326);area.closeSubPath();g.setGradientFill(juce::ColourGradient(colour.withAlpha(kind==0?.12f:.2f),0,99,colour.withAlpha(.015f),0,326,false));g.fillPath(area);}
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(.12f),28,0,colour.withAlpha(kind==2?.55f:1.f),742,0,false));g.strokePath(line,juce::PathStrokeType(kind==2?.9f:1.25f));
    }
    const float thresholdY=99.f-threshold.getValue()/12.f*227.f;g.setColour(t.key.withAlpha(.75f));const float dashes[]{4,5};g.drawDashedLine({28,thresholdY,742,thresholdY},dashes,2,1.f);
}
void PhasePocketAudioProcessorEditor::paint(juce::Graphics& g){
    paintChrome(g);juce::Graphics::ScopedSaveState save(g);g.addTransform(juce::AffineTransform::scale(float(getWidth())/800));
    if(g.clipRegionIntersects({28,99,714,227})){
#if DUCK_ENABLE_OPENGL
    if(glowRenderer&&glowRenderer->ready.load()&&!capturingBlur&&bypassMix<.5f){
        auto frame=std::make_shared<PocketGlowRenderer::Frame>();frame->width=getWidth();frame->height=getHeight();frame->chromeRevision=chromeRevision;frame->time=displayTime;frame->frozen=frozenGraph;frame->rasterScale=chromeScale;
        auto& layer=frame->plots[0];layer.bounds=plotArea;layer.background=chrome.getClippedImage((plotArea.toFloat()*chromeScale).toNearestInt().getIntersection(chrome.getBounds()));
        const float coreScale=g.getInternalContext().getPhysicalPixelScaleFactor();layer.core=juce::Image(juce::Image::ARGB,juce::jmax(1,juce::roundToInt(714*coreScale)),juce::jmax(1,juce::roundToInt(227*coreScale)),true,juce::SoftwareImageType());juce::Graphics cg(layer.core);cg.addTransform(juce::AffineTransform::translation(-28.f,-99.f).scaled(coreScale));graph(cg);layer.intensity=0;glowRenderer->publish(std::move(frame));
    }else
#endif
    graph(g);
    }
    const auto t=look.tokens();auto meter=[&](float v){return v<=-100?juce::String("--"):juce::String(v,2);};
    text(g,"IN  "+meter(audioProcessor.inputTP.load())+" dBTP",{28,365,245,25},13,t.key);
    text(g,"OUT  "+meter(audioProcessor.outputTP.load())+" dBTP",{527,365,245,25},13,t.out,juce::Justification::centredRight);
    const bool partial=audioProcessor.parameters.getRawParameterValue("partial")->load()>.5f;
    text(g,partial?"PARTIAL":"STRICT",{325,655,150,25},12,t.ink,juce::Justification::centred);
    text(g,"Accepted "+juce::String(audioProcessor.accepted.load())+"   Skipped "+juce::String(audioProcessor.skipped.load())+"   Late "+juce::String(audioProcessor.late.load()),{28,655,295,25},11,t.muted);
    text(g,"Angle "+juce::String(audioProcessor.lastAngle.load(),0)+juce::String::fromUTF8("°"),{555,655,218,25},12,t.muted,juce::Justification::centredRight);
    text(g,"Conditional peak reduction / threshold is not a ceiling",{28,678,744,20},11,t.muted,juce::Justification::centred);
}
void PhasePocketAudioProcessorEditor::setActivationMode(bool offline){
    offlineActivation=offline;activationPanel.offline=offline;activationPanel.dragging=false;
    onlineButton.setToggleState(!offline,juce::dontSendNotification);offlineButton.setToggleState(offline,juce::dontSendNotification);
    licenseInput.setVisible(!offline);activateButton.setVisible(!offline);
    for(auto* c:std::initializer_list<juce::Component*>{&deviceCodeInput,&deviceCodeLabel,&copyDeviceButton,&chooseLicenseButton})c->setVisible(offline);
    activationMessage.setText(offline?(audioProcessor.licenseDeviceCode().isEmpty()?"System device ID is unavailable. Contact support.":"Get your file at rainlinemusic.su/account.\nDrop it here or choose a file below."):"Enter the license key from your purchase email.",juce::dontSendNotification);
    activationStatus.setText({},juce::dontSendNotification);
    copyDeviceButton.setEnabled(audioProcessor.licenseDeviceCode().isNotEmpty());lastOnlineMessage.clear();resized();activationPanel.repaint();
}
void PhasePocketAudioProcessorEditor::importLicense(const juce::File& file){
    juce::String error;if(audioProcessor.importLicenseFile(file,error))activationPanel.setVisible(false);else activationStatus.setText(error,juce::dontSendNotification);
}
void PhasePocketAudioProcessorEditor::captureBlurSnapshot(){
    if(capturingBlur||blurArea.isEmpty())return;
    capturingBlur=true;const float old=bypassMix;bypassMix=0;
    auto source=createComponentSnapshot(blurArea,true,1.f);
    bypassMix=old;capturingBlur=false;
    if(!source.isValid()||source.getWidth()<8||source.getHeight()<8)return;
    const int w=juce::jmax(16,source.getWidth()/2),h=juce::jmax(16,source.getHeight()/2);
    juce::Image small(juce::Image::ARGB,w,h,true,juce::SoftwareImageType());
    {juce::Graphics sg(small);sg.setImageResamplingQuality(juce::Graphics::highResamplingQuality);sg.drawImage(source,juce::Rectangle<float>(0,0,float(w),float(h)),juce::RectanglePlacement::stretchToFit);}
    // JUCE 8.0.4 convolution forms out-of-image edge pointers before checking
    // bounds (UBSan on macOS). Reuse the clamped, premultiplied separable blur.
    juce::Image horizontal(juce::Image::ARGB,w,h,true,juce::SoftwareImageType());
    juce::Image soft(juce::Image::ARGB,w,h,true,juce::SoftwareImageType());
    const int radius=juce::jmax(1,juce::roundToInt(float(getWidth())/800.f*3.f));
    for(int pass=0;pass<3;++pass){PocketSoftwareGlow::boxBlur(pass==0?small:soft,horizontal,true,radius);PocketSoftwareGlow::boxBlur(horizontal,soft,false,radius);}
    blurredSnapshot=soft;
}
void PhasePocketAudioProcessorEditor::updateActivationColours(){
    const auto palette=look.tokens();activationPanel.palette=palette;
    activationTitle.setColour(juce::Label::textColourId,palette.ink);activationMessage.setColour(juce::Label::textColourId,palette.muted);deviceCodeLabel.setColour(juce::Label::textColourId,palette.muted);
    activationStatus.setColour(juce::Label::textColourId,palette.out);
    for(auto* input:{&licenseInput,&deviceCodeInput}){
        input->setColour(juce::TextEditor::backgroundColourId,palette.glass);input->setColour(juce::TextEditor::textColourId,palette.ink);
        input->setColour(juce::TextEditor::outlineColourId,palette.border);input->setColour(juce::TextEditor::focusedOutlineColourId,palette.out);
        input->setColour(juce::TextEditor::highlightColourId,palette.out.withAlpha(.22f));input->setColour(juce::TextEditor::highlightedTextColourId,palette.ink);
    }
}
void PhasePocketAudioProcessorEditor::captureActivationBackdrop(){
    if(capturingBlur||getWidth()<8||getHeight()<8)return;
    // The modal itself must not be part of the snapshot. Alpha keeps keyboard
    // focus intact while the background is captured on the message thread.
    const auto oldAlpha=activationPanel.getAlpha();const float oldMix=bypassMix;
    activationPanel.setOpaque(false);activationPanel.setAlpha(0);capturingBlur=true;bypassMix=0;
    auto source=createComponentSnapshot(getLocalBounds(),true,.5f);
    bypassMix=oldMix;capturingBlur=false;activationPanel.setAlpha(oldAlpha);activationPanel.setOpaque(true);
    activationBackdropDirty=false;
    if(!source.isValid())return;
    const int w=source.getWidth(),h=source.getHeight();
    juce::Image horizontal(juce::Image::ARGB,w,h,true,juce::SoftwareImageType());
    juce::Image soft(juce::Image::ARGB,w,h,true,juce::SoftwareImageType());
    const int radius=juce::jmax(2,juce::roundToInt(float(getWidth())/800.f*5.f));
    for(int pass=0;pass<3;++pass){PocketSoftwareGlow::boxBlur(pass==0?source:soft,horizontal,true,radius);PocketSoftwareGlow::boxBlur(horizontal,soft,false,radius);}
    activationPanel.backdrop=soft;activationPanel.palette=look.tokens();activationPanel.repaint();
}
void PhasePocketAudioProcessorEditor::paintOverChildren(juce::Graphics& g){
    if(capturingBlur||activationPanel.isVisible()||bypassMix<.5f||!blurredSnapshot.isValid())return;
    juce::Graphics::ScopedSaveState save(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(blurredSnapshot,blurArea.toFloat(),juce::RectanglePlacement::stretchToFit);
    g.setColour((look.isDark()?juce::Colours::black:juce::Colours::white).withAlpha(.18f));g.fillRoundedRectangle(blurArea.toFloat(),12);
    const auto centre=blurArea.toFloat().translated(0,-float(blurArea.getHeight())*.05f);
    const float size=juce::jmax(34.f,float(getWidth())/18.f);
    const auto ink=look.isDark()?juce::Colours::white:juce::Colour(0xff202124);
    g.setColour((look.isDark()?juce::Colours::black:juce::Colours::white).withAlpha(.55f));g.setFont(uiFont(size));g.drawText("BYPASSED",centre.translated(0,2),juce::Justification::centred);
    text(g,"BYPASSED",centre,size,ink,juce::Justification::centred,look.isDark()?.1f:0.f);
}
#if JUCE_WINDOWS
void PhasePocketAudioProcessorEditor::setWindowsRenderer(const juce::String& name,bool persist){
    auto* peer=getPeer();if(peer==nullptr)return;
    const auto names=peer->getAvailableRenderingEngines();
    const int index=names.indexOf(name);
    if(index<0)return; // Preserve JUCE's default when a stored backend is unavailable.
    if(peer->getCurrentRenderingEngine()!=index){
        peer->setCurrentRenderingEngine(index);
        blurredSnapshot={};invalidateChrome();
        for(auto* child:getChildren())child->repaint();
    }
    if(persist&&preferences){
        const int actual=peer->getCurrentRenderingEngine();
        if(juce::isPositiveAndBelow(actual,names.size())){
            preferences->setValue("phasePocket.ui.windowsRenderer.v1",names[actual]);
            preferences->saveIfNeeded();
        }
    }
}
#endif
void PhasePocketAudioProcessorEditor::parentHierarchyChanged(){
#if JUCE_WINDOWS
    if(getPeer()&&preferences&&preferences->containsKey("phasePocket.ui.windowsRenderer.v1"))setWindowsRenderer(preferences->getValue("phasePocket.ui.windowsRenderer.v1"),false);
#endif
#if DUCK_ENABLE_OPENGL && ! JUCE_WINDOWS
    if(getPeer()&&preferences&&preferences->getBoolValue("phasePocket.ui.opengl.v3",false)&&!glowRenderer)setOpenGL(true,false);
#endif
}
#if DUCK_ENABLE_OPENGL
void PhasePocketAudioProcessorEditor::setOpenGL(bool enabled,bool persist){
#if JUCE_WINDOWS
    enabled=false;
#endif
    if(glowRenderer){glowRenderer->stop();glowRenderer.reset();}setOpaque(!enabled);
    if(enabled&&getPeer()){glowRenderer=std::make_unique<PocketGlowRenderer>();glAttachTime=juce::Time::getMillisecondCounterHiRes();glowRenderer->attach(*this);}
    if(persist){preferences->setValue("phasePocket.ui.opengl.v3",enabled);preferences->saveIfNeeded();}invalidateChrome();
}
#endif
void PhasePocketAudioProcessorEditor::frameTick(){
    const double now=juce::Time::getMillisecondCounterHiRes(),clock=now*.001;
    if(resizeStamp>0&&now-resizeStamp>400)saveSize();
    const bool activating=!audioProcessor.isActivated();if(activationPanel.isVisible()!=activating){activationPanel.setVisible(activating);activationBackdropDirty=true;}
    activateButton.setEnabled(!audioProcessor.onlineActivationBusy());const auto message=audioProcessor.onlineActivationMessage();if(message.isNotEmpty()&&message!=lastOnlineMessage){lastOnlineMessage=message;activationStatus.setText(message,juce::dontSendNotification);}
    if(activating&&activationBackdropDirty)captureActivationBackdrop();
#if DUCK_ENABLE_OPENGL
    if(glowRenderer&&(glowRenderer->failed.load()||(!glowRenderer->ready.load()&&now-glAttachTime>2000)))setOpenGL(false);
#endif
    PocketTrace v;bool fresh=false;for(int i=0;i<8192&&audioProcessor.popTrace(v);++i){history[size_t(cursor)]=v;cursor=(cursor+1)%historyCapacity;filled=juce::jmin(historyCapacity,filled+1);if(v.input>.0001f||v.output>.0001f)lastSignalTime=v.time;fresh=true;}
    if(filled){const double latest=history[size_t((cursor+historyCapacity-1)%historyCapacity)].time;const double dt=lastClock>0?juce::jlimit(0.,.1,clock-lastClock):0.;const bool resumed=fresh&&(clock-lastPacketClock>.1);if(fresh){lastPacketClock=clock;lastLatest=latest;}
        const double target=juce::jmax(0.,latest-.008);if(resumed||displayTime<=0||std::abs(target-displayTime)>.4)displayTime=target;else{displayTime+=dt;displayTime+=(target-displayTime)*.06;}displayTime=juce::jmin(displayTime,latest);
    }lastClock=clock;
    const bool target=audioProcessor.displayBypass.load()||bypassButton.getToggleState();if(target!=bypassTarget){bypassTarget=target;if(target){captureBlurSnapshot();bypassMix=1;}else{bypassMix=0;blurredSnapshot={};}repaint();}
    if(bypassMix>.5f&&!blurredSnapshot.isValid()){captureBlurSnapshot();repaint();}
    if(!chromeValid)repaint();
    if(!activating&&bypassMix<.5f&&fresh&&!frozenGraph){if(lastSignalTime<0||displayTime-lastSignalTime<window+.2)repaint(scaled(0,57,800,303));repaint(scaled(0,363,800,28));repaint(scaled(0,653,800,47));}
}
