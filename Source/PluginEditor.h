#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UnitView.h"

// Low-poly / PS1-style look: everything is rendered at low resolution and
// scaled up with nearest-neighbour filtering.
class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parentArea) override;
};

// Spinning low-poly gem logo.
class GemLogo : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

// Waveform + live grains as spinning cubes in a low-res void. Accepts dropped files.
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (ChaosGrainProcessor& p) : proc (p) {}

    void refreshPeaks();
    void paint (juce::Graphics&) override;
    void resized() override { refreshPeaks(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

private:
    ChaosGrainProcessor& proc;
    std::vector<std::pair<float, float>> peaks;
    juce::String sampleName;
    bool dragging = false;
};

// Full "advanced" panel with every parameter.
class FullPanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit FullPanel (ChaosGrainProcessor&);
    ~FullPanel() override;

    std::function<void()> onBack;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildBackground();

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& name, const juce::String& tip);
    Choice& addChoice (const juce::String& paramId, const juce::String& name, const juce::StringArray& items);

    ChaosGrainProcessor& proc;
    RetroLookAndFeel lnf;

    GemLogo logo;
    WaveformView waveform;
    juce::TextButton loadButton { "LOAD" }, randomButton { "RANDOMIZE" }, backButton { "< UNIT" };
    juce::ComboBox presetBox;
    juce::ToggleButton holdButton { "HOLD" }, syncButton { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttach, syncAttach;
    juce::TooltipWindow tooltips { this, 700 };

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<juce::Component*> rows[3];
    juce::Rectangle<int> rowPanels[3], titleArea;
    juce::Image background;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastSampleVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullPanel)
};

// Plugin window: the compact unit by default, the full panel on request.
class ChaosGrainEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChaosGrainEditor (ChaosGrainProcessor&);
    ~ChaosGrainEditor() override;

    void resized() override;

private:
    void setAdvanced (bool);

    ChaosGrainProcessor& proc;
    RetroLookAndFeel lnf;
    UnitView unit;
    FullPanel full;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChaosGrainEditor)
};
