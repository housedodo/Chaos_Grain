#pragma once

#include "HoistProcessor.h"

// Pixel-art handheld unit (low-res, scaled up): ANCHOR button, a screen with a little boat
// on the sea (waves follow TIDE, the sky darkens with DREAD), five DREAD stage lamps and
// three knobs: TIDE, DREAD, MIX.
class HoistEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HoistEditor (HoistProcessor&);
    ~HoistEditor() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int lowW = 128, lowH = 170;

private:
    void timerCallback() override { repaint(); }
    void render (juce::Image& img);
    void setScale (int s);
    int knobAt (juce::Point<int> p) const;

    HoistProcessor& proc;
    int scale = 4;
    int dragKnob = -1;
    float dragStartValue = 0.0f;
    int dragStartY = 0;
    int touched = -1;
    double touchedMs = 0.0;
};
