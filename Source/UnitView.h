#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The plugin's whole UI: a 128x198 pixel-art "handheld unit" scaled up 4x, with three
// big knobs, a strip of small knobs (filter / space / ...), HOLD|FREEZE, SYNC and RAND
// buttons, a phosphor screen, a grain counter and a density LED row.
// Right-click for presets and the remaining options.
class UnitView : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer
{
public:
    static constexpr int lowW = 128, lowH = 198, scale = 4;

    explicit UnitView (ChaosGrainProcessor&);
    ~UnitView() override;

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

    struct KnobSpec { const char* paramId; const char* label; float cx, cy, r; bool red; bool mini; };
    static std::vector<KnobSpec> makeKnobSpecs();
    const std::vector<KnobSpec> knobSpecs = makeKnobSpecs();

    void timerCallback() override;
    void refreshPeaks();
    void showMenu();
    void loadViaChooser();
    void render (juce::Image& img);

    ChaosGrainProcessor& proc;
    InvisibleKnobs invisible;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    double randFlashMs = 0.0;

    std::vector<std::pair<float, float>> peaks;
    int lastSampleVersion = -1;
    bool dragging = false;
    int touched = -1;
    double touchedTime = 0.0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnitView)
};
