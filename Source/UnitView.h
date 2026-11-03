#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The compact "handheld unit" face: a 128x170 pixel-art device scaled up 4x,
// with three knobs (SIZE / CHAOS / PITCH), a phosphor screen, a grain counter
// and a density LED row. Right-click for everything else.
class UnitView : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer
{
public:
    static constexpr int lowW = 128, lowH = 170, scale = 4;

    explicit UnitView (ChaosGrainProcessor&);
    ~UnitView() override;

    std::function<void()> onShowAdvanced;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; }
    void fileDragExit (const juce::StringArray&) override { dragging = false; }

private:
    struct InvisibleKnobs : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
    };

    struct KnobSpec { const char* paramId; const char* label; float cx, cy, r; bool red; };
    static const KnobSpec knobSpecs[3];

    void timerCallback() override;
    void refreshPeaks();
    void showMenu();
    void loadViaChooser();
    void render (juce::Image& img);

    ChaosGrainProcessor& proc;
    InvisibleKnobs invisible;
    juce::Slider sliders[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];

    std::vector<std::pair<float, float>> peaks;
    int lastSampleVersion = -1;
    bool dragging = false;
    int touched = -1;
    double touchedTime = 0.0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnitView)
};
