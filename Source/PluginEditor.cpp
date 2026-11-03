#include "PluginEditor.h"
#include "RetroGfx.h"

using namespace retro;

namespace
{
    float nowSeconds() { return (float) (juce::Time::getMillisecondCounterHiRes() * 0.001); }

    juce::Rectangle<int> toLow (juce::Rectangle<int> r)
    {
        return { r.getX() / px, r.getY() / px, r.getWidth() / px, r.getHeight() / px };
    }
}

//==============================================================================
RetroLookAndFeel::RetroLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, pal::cyan);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, pal::magenta.withAlpha (0.5f));
    setColour (juce::Label::textColourId, pal::text);
    setColour (juce::Label::textWhenEditingColourId, pal::text);
    setColour (juce::TextEditor::backgroundColourId, pal::inset);
    setColour (juce::TextEditor::textColourId, pal::text);
    setColour (juce::TextEditor::highlightColourId, pal::magenta.withAlpha (0.5f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, pal::text);
    setColour (juce::PopupMenu::backgroundColourId, pal::panel);
    setColour (juce::PopupMenu::textColourId, pal::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::magenta);
    setColour (juce::PopupMenu::highlightedTextColourId, pal::text);
    setColour (juce::TextButton::textColourOffId, pal::text);
    setColour (juce::ToggleButton::textColourId, pal::text);
    setColour (juce::TooltipWindow::backgroundColourId, pal::panel);
}

void RetroLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const int side = juce::jmin (w, h);
    const int L = side / px;
    if (L < 6) return;
    const auto sq = juce::Rectangle<int> (x, y, w, h).withSizeKeepingCentre (L * px, L * px);

    juce::Image img (juce::Image::ARGB, L, L, true);
    {
        juce::Graphics ig (img);
        const float c = (float) L * 0.5f;
        const float ringR = (float) L * 0.45f;

        // Value ring made of chunky blocks.
        const int segs = 15;
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        for (int i = 0; i < segs; ++i)
        {
            const float t = (float) i / (float) (segs - 1);
            const float a = startAngle + t * (endAngle - startAngle);
            const bool on = bipolar ? (pos >= 0.5f ? (t >= 0.5f - 1e-3f && t <= pos + 1e-3f)
                                                   : (t <= 0.5f + 1e-3f && t >= pos - 1e-3f))
                                    : t <= pos + 1e-3f;
            const auto col = on ? pal::magenta.interpolatedWith (pal::yellow, t) : pal::panelLo.brighter (0.5f);
            const int bx = juce::roundToInt (c + std::sin (a) * ringR) - 1;
            const int by = juce::roundToInt (c - std::cos (a) * ringR) - 1;
            ig.setColour (pal::panelLo);
            ig.fillRect (bx + 1, by + 1, 2, 2);
            ig.setColour (to15bit (col));
            ig.fillRect (bx, by, 2, 2);
        }

        // The knob itself: a low-poly mesh that physically rotates.
        const bool isChaos = s.getName() == "chaos";
        const auto& mesh = knobMesh (isChaos ? pal::magenta : juce::Colour (0xffb4acec),
                                     isChaos ? juce::Colour (0xff8a2458) : juce::Colour (0xff4a4398),
                                     isChaos ? pal::yellow : pal::cyan);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const Camera cam { c, c + (float) L * 0.02f, (float) L * 0.27f, 5.0f };
        drawMesh (ig, mesh, [angle] (Vec3 p) { return rotX (rotZ (p, -angle), -0.55f); }, cam);
    }
    crispen (img);
    blitLowRes (g, img, sq.getX(), sq.getY(), px);
}

void RetroLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited()) return;

    auto area = label.getLocalBounds();
    const auto text = label.getText().toUpperCase();

    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        // Value readout: inset LCD-ish box.
        const auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 104), area.getHeight());
        g.setColour (pal::panelLo);
        g.fillRect (box);
        g.setColour (pal::inset);
        g.fillRect (box.reduced (2));
        pixelText (g, text, box.reduced (2, 1), pal::cyan, juce::Justification::centred, 2, false);
    }
    else if (dynamic_cast<juce::ComboBox*> (label.getParentComponent()) != nullptr)
    {
        pixelText (g, text, area.reduced (2, 0), pal::text, juce::Justification::centredLeft, 2);
    }
    else
    {
        pixelText (g, text, area, pal::dim, juce::Justification::centredBottom, 2);
    }
}

void RetroLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds();
    juce::Image img (juce::Image::RGB, r.getWidth() / px, r.getHeight() / px, false);
    {
        juce::Graphics ig (img);
        auto face = b.getProperties()["hot"] ? pal::magenta.darker (0.15f) : pal::panel.brighter (0.15f);
        if (over) face = face.brighter (0.2f);
        bevelBox (ig, img.getBounds(), face, ! down);
    }
    blitLowRes (g, img, 0, 0, px);
}

void RetroLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto r = b.getLocalBounds();
    if (down) r.translate (px, px);
    pixelText (g, b.getButtonText().toUpperCase(), r, pal::text, juce::Justification::centred, 2);
}

void RetroLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const int boxSize = 6; // low-res pixels
    const int h = b.getHeight() / px;
    juce::Image img (juce::Image::ARGB, boxSize, boxSize, true);
    {
        juce::Graphics ig (img);
        bevelBox (ig, img.getBounds(), over ? pal::inset.brighter (0.3f) : pal::inset, false);
        if (b.getToggleState())
        {
            ig.setColour (pal::cyan);
            ig.fillRect (2, 2, boxSize - 4, boxSize - 4);
        }
    }
    const int y = (h - boxSize) / 2 * px;
    blitLowRes (g, img, 0, y, px);
    pixelText (g, b.getButtonText().toUpperCase(), b.getLocalBounds().withTrimmedLeft (boxSize * px + 6),
               b.getToggleState() ? pal::cyan : pal::text, juce::Justification::centredLeft, 2);
}

void RetroLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    juce::Image img (juce::Image::RGB, w / px, h / px, false);
    {
        juce::Graphics ig (img);
        bevelBox (ig, img.getBounds(), box.isMouseOver (true) ? pal::inset.brighter (0.25f) : pal::inset, false);

        // Blocky down-arrow.
        const int ax = img.getWidth() - 7, ay = img.getHeight() / 2 - 1;
        ig.setColour (pal::magenta);
        ig.fillRect (ax, ay, 5, 1);
        ig.fillRect (ax + 1, ay + 1, 3, 1);
        ig.fillRect (ax + 2, ay + 2, 1, 1);
    }
    blitLowRes (g, img, 0, 0, px);
}

void RetroLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
}

void RetroLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    juce::Image img (juce::Image::RGB, juce::jmax (1, w / px + 1), juce::jmax (1, h / px + 1), false);
    {
        juce::Graphics ig (img);
        bevelBox (ig, img.getBounds(), pal::panel, true);
    }
    blitLowRes (g, img, 0, 0, px);
}

void RetroLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool,
                                          bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                          const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator) return;
    auto r = area.reduced (px * 2, 0);
    if (isHighlighted)
    {
        g.setColour (pal::magenta);
        g.fillRect (r);
    }
    const juce::String prefix = isTicked ? "> " : "  ";
    pixelText (g, prefix + text.toUpperCase(), r.reduced (4, 0), isTicked ? pal::yellow : pal::text,
               juce::Justification::centredLeft, 2);
}

juce::Rectangle<int> RetroLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (420, text.length() * 12 + 24), h = 28;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
               .constrainedWithin (parent);
}

void RetroLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    drawPopupMenuBackground (g, w, h);
    pixelText (g, text.toUpperCase(), { 8, 0, w - 12, h }, pal::yellow, juce::Justification::centredLeft, 2);
}

//==============================================================================
void GemLogo::paint (juce::Graphics& g)
{
    const int L = juce::jmin (getWidth(), getHeight()) / 2;
    if (L < 4) return;
    juce::Image img (juce::Image::ARGB, L, L, true);
    {
        juce::Graphics ig (img);
        const float t = nowSeconds();
        const Camera cam { (float) L * 0.5f, (float) L * 0.5f, (float) L * 0.3f, 6.0f };
        drawMesh (ig, gemMesh(), [t] (Vec3 p) { return rotX (rotY (p, t * 1.3f), 0.35f + 0.15f * std::sin (t * 0.7f)); },
                  cam, normalise ({ -0.5f, 0.7f, -0.5f }), 0.35f);
    }
    crispen (img);
    blitLowRes (g, img, 0, 0, 2);
}

//==============================================================================
void WaveformView::refreshPeaks()
{
    if (kIsFx)
    {
        proc.engine.getLivePeaks (peaks, juce::jmax (1, getWidth() / px - 4));
        float pk = 0.05f; // auto-scale so quiet input is still visible
        for (auto& pr : peaks) pk = std::max (pk, std::max (-pr.first, pr.second));
        for (auto& pr : peaks) { pr.first /= pk * 1.1f; pr.second /= pk * 1.1f; }
        const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
        sampleName = frozen ? "LIVE INPUT - FROZEN" : "LIVE INPUT";
        return;
    }
    peaks.clear();
    auto s = proc.getCurrentSample();
    if (s == nullptr || getWidth() <= 0) { sampleName = {}; repaint(); return; }

    sampleName = juce::File (s->path).getFileName();
    const auto& buf = s->buffer;
    const int cols = juce::jmax (1, getWidth() / px - 4);
    const int n = buf.getNumSamples();
    peaks.resize ((size_t) cols);

    for (int c = 0; c < cols; ++c)
    {
        const int a = (int) ((juce::int64) n * c / cols);
        const int b = juce::jmax (a + 1, (int) ((juce::int64) n * (c + 1) / cols));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto range = juce::FloatVectorOperations::findMinAndMax (buf.getReadPointer (ch, a), b - a);
            lo = juce::jmin (lo, range.getStart());
            hi = juce::jmax (hi, range.getEnd());
        }
        peaks[(size_t) c] = { lo, hi };
    }
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    const int W = getWidth() / px, H = getHeight() / px;
    if (W < 8 || H < 8) return;

    const float t = nowSeconds();
    juce::Image img (juce::Image::RGB, W, H, false);
    {
        juce::Graphics ig (img);
        bevelBox (ig, img.getBounds(), pal::inset, false);
        const auto inner = img.getBounds().reduced (2);
        ditherGradient (img, inner, juce::Colour (0xff160f40), juce::Colour (0xff030208), 6);

        ig.reduceClipRegion (inner);

        // Scrolling perspective floor grid.
        const int horizon = inner.getY() + inner.getHeight() * 55 / 100;
        const float vx = (float) inner.getCentreX();
        ig.setColour (to15bit (juce::Colour (0xff3b2d8a)));
        for (int k = -12; k <= 12; ++k)
        {
            const float bx = vx + (float) k * (float) W * 0.12f;
            ig.drawLine (vx, (float) horizon, bx, (float) inner.getBottom(), 1.0f);
        }
        const float scroll = std::fmod (t * 0.5f, 1.0f);
        for (int k = 0; k < 8; ++k)
        {
            const float d = ((float) k + scroll) / 8.0f;
            const int yy = horizon + (int) ((float) (inner.getBottom() - horizon) * d * d);
            ig.fillRect (inner.getX(), yy, inner.getWidth(), 1);
        }
        ig.setColour (to15bit (juce::Colour (0xff6a4fd0)));
        ig.fillRect (inner.getX(), horizon, inner.getWidth(), 1);

        const int mid = inner.getCentreY();
        const float halfH = (float) inner.getHeight() * 0.42f;

        if (peaks.empty())
        {
            // empty state drawn after scaling (text)
        }
        else
        {
            const int slices = (int) proc.apvts.getRawParameterValue ("slices")->load();
            if (slices > 1)
            {
                ig.setColour (pal::dim.withAlpha (0.6f));
                for (int i = 1; i < slices; ++i)
                {
                    const int sx = inner.getX() + inner.getWidth() * i / slices;
                    for (int yy = inner.getY(); yy < inner.getBottom(); yy += 3)
                        ig.fillRect (sx, yy, 1, 1);
                }
            }

            // Waveform as chunky columns.
            for (size_t c = 0; c < peaks.size(); ++c)
            {
                const int xx = inner.getX() + (int) c;
                const int top = mid - (int) std::ceil (peaks[c].second * halfH);
                const int bot = mid - (int) std::floor (peaks[c].first * halfH);
                ig.setColour (to15bit (pal::cyan.darker (0.45f)));
                ig.fillRect (xx, top, 1, juce::jmax (1, bot - top + 1));
                ig.setColour (pal::cyan);
                ig.fillRect (xx, top, 1, 1);
                ig.fillRect (xx, bot, 1, 1);
            }

            // Playhead
            const float ph = proc.engine.uiPlayhead.load (std::memory_order_relaxed);
            const int phx = inner.getX() + (int) (ph * (float) inner.getWidth());
            ig.setColour (pal::yellow);
            for (int yy = inner.getY(); yy < inner.getBottom(); yy += 2)
                ig.fillRect (phx, yy, 1, 1);

            // Grains: spinning flat-shaded cubes.
            static const Mesh pinkCube = tinted (cubeMesh(), pal::magenta);
            static const Mesh goldCube = tinted (cubeMesh(), pal::yellow);
            const int n = proc.engine.uiNumGrains.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                const float gp = proc.engine.uiGrainPos[(size_t) i].load (std::memory_order_relaxed);
                const float lv = proc.engine.uiGrainLevel[(size_t) i].load (std::memory_order_relaxed);
                if (lv < 0.05f) continue;
                const float gx = (float) inner.getX() + gp * (float) inner.getWidth();
                const float gy = (float) mid + std::sin ((float) i * 2.39996f) * halfH * 0.7f;
                const float size = 1.5f + 3.5f * lv;
                const float spin = t * (1.5f + (float) (i % 3)) + (float) i;
                const Camera cam { gx, gy, size, 4.0f };
                drawMesh (ig, (i % 3 == 0) ? goldCube : pinkCube,
                          [spin] (Vec3 p) { return rotX (rotY (p, spin), spin * 0.7f); }, cam);
            }
        }
    }
    blitLowRes (g, img, 0, 0, px);

    auto textArea = getLocalBounds().reduced (px * 4, px * 3);
    if (peaks.empty())
    {
        const bool blink = std::fmod (t, 1.2f) < 0.8f;
        if (blink)
            pixelText (g, "INSERT SAMPLE", getLocalBounds(), pal::yellow, juce::Justification::centred, 4);
        pixelText (g, "DRAG A FILE HERE OR CLICK LOAD", getLocalBounds().withTrimmedTop (getHeight() / 2 + 18).withHeight (24),
                   pal::dim, juce::Justification::centredTop, 2);
    }
    else
    {
        pixelText (g, sampleName.toUpperCase(), textArea.withHeight (20), pal::dim, juce::Justification::topLeft, 2);
    }

    if (dragging)
    {
        g.setColour (pal::magenta);
        g.drawRect (getLocalBounds(), px * 2);
    }
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (kIsFx) return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    for (auto& f : files)
        if (proc.loadSample (juce::File (f)))
            break;
    refreshPeaks();
}

//==============================================================================
FullPanel::FullPanel (ChaosGrainProcessor& p)
    : proc (p), waveform (p)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);

    backButton.setTooltip ("Back to the compact unit");
    backButton.onClick = [this] { if (onBack) onBack(); };
    for (auto* c : std::initializer_list<juce::Component*> { &backButton, &logo, &waveform, &loadButton, &randomButton,
                                                             &presetBox, &holdButton, &syncButton })
        addAndMakeVisible (c);
    logo.setInterceptsMouseClicks (false, false);
    randomButton.getProperties().set ("hot", true);

    holdAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, kIsFx ? "freeze" : "hold", holdButton);
    if (kIsFx)
    {
        holdButton.setButtonText ("FREEZE");
        loadButton.setVisible (false);
    }
    syncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "sync", syncButton);
    holdButton.setTooltip (kIsFx ? "Stop recording and keep playing the buffer" : "Drone the sample without MIDI");
    syncButton.setTooltip ("Trigger grains on the host tempo grid");
    loadButton.setTooltip ("Load an audio file (or drag one onto the screen)");
    randomButton.setTooltip ("Roll the dice on every grain setting");

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a sample", juce::File(),
                                                       proc.formatManager.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      proc.loadSample (fc.getResult());
                              });
    };

    randomButton.onClick = [this] { proc.randomize(); };

    presetBox.addItemList (ChaosGrainProcessor::getPresetNames(), 1);
    presetBox.setText ("PRESETS", juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedItemIndex() >= 0)
            proc.applyPreset (presetBox.getSelectedItemIndex());
    };

    rows[0] = { &addKnob ("position", "Position", "Where in the sample grains start").slider,
                &addKnob ("scan", "Scan", "Moves the position through the sample (1x = real time)").slider,
                &addKnob ("spray", "Spray", "Random offset around the position").slider,
                &addKnob ("size", "Size", "Grain length: short = texture, long = chops/pads").slider,
                &addKnob ("density", "Density", "Grains per second (when not synced)").slider,
                &addKnob ("jitter", "Jitter", "Timing randomness (synced: chance to skip steps)").slider,
                &addKnob ("shape", "Shape", "0 = hard chop, 100 = smooth bell envelope").slider };

    rows[1] = { &addKnob ("pitch", "Pitch", "Transpose in semitones").slider,
                &addKnob ("pitchRand", "Pitch Rnd", "Random pitch per grain (+/- semitones)").slider,
                &addChoice ("pitchQuant", "Quantize", ChaosGrainProcessor::quantNames()).box,
                &addKnob ("reverse", "Reverse", "Chance a grain plays backwards").slider,
                &addKnob ("slices", "Slices", "Cut the sample into N chops").slider,
                &addKnob ("jumble", "Jumble", "Chance of jumping to a random slice").slider,
                &addKnob ("repeat", "Repeat", "Chance to stutter the previous grain").slider };

    rows[2] = { &addKnob ("pan", "Spread", "Random stereo placement").slider,
                &addChoice ("rate", "Sync Rate", ChaosGrainProcessor::rateNames()).box };
    if (! kIsFx)
    {
        rows[2].push_back (&addKnob ("attack", "Attack", "Note fade in").slider);
        rows[2].push_back (&addKnob ("release", "Release", "Note fade out").slider);
    }
    rows[2].push_back (&addKnob ("chaos", "CHAOS", "Macro: scales every random amount").slider);
    rows[2].push_back (&addKnob ("gain", "Gain", "Output level").slider);

    rows[3] = { &addKnob ("filter", "Filter", "Low-pass filter on the grains (inside the feedback loop)").slider,
                &addKnob ("reso", "Reso", "Filter resonance").slider,
                &addKnob ("space", "Space", "Reverb amount and size").slider };
    if (kIsFx)
    {
        rows[3].push_back (&addKnob ("repeats", "Repeats", "Feedback: output is fed back into the buffer").slider);
        rows[3].push_back (&addKnob ("memory", "Memory", "How far back in time grains can reach").slider);
        rows[3].push_back (&addKnob ("mix", "Mix", "Dry / wet").slider);
    }

    setSize (panelW, panelH);
    startTimerHz (30);
}

FullPanel::~FullPanel()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

FullPanel::Knob& FullPanel::addKnob (const juce::String& id, const juce::String& name, const juce::String& tip)
{
    auto k = std::make_unique<Knob>();
    k->slider.setName (id);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 104, 20);
    k->slider.setTooltip (tip);
    k->label.setText (name, juce::dontSendNotification);
    k->label.attachToComponent (&k->slider, false);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

FullPanel::Choice& FullPanel::addChoice (const juce::String& id, const juce::String& name, const juce::StringArray& items)
{
    auto c = std::make_unique<Choice>();
    c->box.addItemList (items, 1);
    c->label.setText (name, juce::dontSendNotification);
    c->label.attachToComponent (&c->box, false);
    addAndMakeVisible (c->box);
    c->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->box);
    choices.push_back (std::move (c));
    return *choices.back();
}

void FullPanel::rebuildBackground()
{
    const int W = getWidth() / px + 1, H = getHeight() / px + 1;
    background = juce::Image (juce::Image::RGB, W, H, false);
    ditherGradient (background, background.getBounds(), pal::bgTop, pal::bgBottom, 7);

    juce::Graphics ig (background);
    for (auto& r : rowPanels)
        bevelBox (ig, toLow (r), pal::panel, true);
}

void FullPanel::paint (juce::Graphics& g)
{
    if (background.isValid())
        blitLowRes (g, background, 0, 0, px);


    const char* titles[] = { "GRAIN", "PITCH / ORDER", "MOTION / OUTPUT", kIsFx ? "COLOUR / FEEDBACK / MIX" : "COLOUR / SPACE" };
    for (int i = 0; i < 4; ++i)
        pixelText (g, titles[i], rowPanels[i].reduced (px * 3, px * 2).withHeight (18), pal::yellow,
                   juce::Justification::topLeft, 2);
}

void FullPanel::paintOverChildren (juce::Graphics& g)
{
    // CRT scanlines
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (int y = 0; y < getHeight(); y += px)
        g.fillRect (0, y, getWidth(), 1);
}

void FullPanel::resized()
{
    auto area = getLocalBounds().reduced (15, 12);

    auto top = area.removeFromTop (48);
    logo.setBounds (top.removeFromLeft (48));
    top.removeFromLeft (8);
    backButton.setBounds (top.removeFromLeft (110).withSizeKeepingCentre (110, 36));
    titleArea = {};

    auto buttons = top.withSizeKeepingCentre (top.getWidth(), 36);
    randomButton.setBounds (buttons.removeFromRight (138));
    buttons.removeFromRight (9);
    presetBox.setBounds (buttons.removeFromRight (171));
    buttons.removeFromRight (9);
    loadButton.setBounds (buttons.removeFromRight (78));
    buttons.removeFromRight (12);
    syncButton.setBounds (buttons.removeFromRight (92));
    holdButton.setBounds (buttons.removeFromRight (96));


    area.removeFromTop (9);
    waveform.setBounds (area.removeFromTop (juce::jmax (120, area.getHeight() / 4)));
    area.removeFromTop (9);

    const int rowH = area.getHeight() / 4;
    for (int i = 0; i < 4; ++i)
    {
        rowPanels[i] = area.removeFromTop (rowH).withTrimmedBottom (6);
        auto r = rowPanels[i].reduced (px * 3, px * 2);
        r.removeFromTop (40); // section title + knob labels
        r.removeFromBottom (4);
        const int colW = r.getWidth() / 7;
        for (auto* c : rows[i])
        {
            auto cell = r.removeFromLeft (colW).reduced (4, 0);
            if (dynamic_cast<juce::ComboBox*> (c) != nullptr)
                c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 33));
            else
                c->setBounds (cell);
        }
    }

    rebuildBackground();
}

void FullPanel::timerCallback()
{
    if (kIsFx || proc.getSampleVersion() != lastSampleVersion)
    {
        lastSampleVersion = proc.getSampleVersion();
        waveform.refreshPeaks();
    }
    waveform.repaint();
    logo.repaint();
}

//==============================================================================
ChaosGrainEditor::ChaosGrainEditor (ChaosGrainProcessor& p)
    : AudioProcessorEditor (&p), proc (p), unit (p), full (p)
{
    setLookAndFeel (&lnf);
    addChildComponent (unit);
    addChildComponent (full);
    unit.onShowAdvanced = [this] { setAdvanced (true); };
    full.onBack = [this] { setAdvanced (false); };
    setAdvanced ((bool) proc.apvts.state.getProperty ("advancedUI", false));
}

ChaosGrainEditor::~ChaosGrainEditor()
{
    setLookAndFeel (nullptr);
}

void ChaosGrainEditor::setAdvanced (bool advanced)
{
    proc.apvts.state.setProperty ("advancedUI", advanced, nullptr);
    unit.setVisible (! advanced);
    full.setVisible (advanced);
    if (advanced) setSize (FullPanel::panelW, FullPanel::panelH);
    else          setSize (UnitView::lowW * UnitView::scale, UnitView::lowH * UnitView::scale);
}

void ChaosGrainEditor::resized()
{
    unit.setBounds (getLocalBounds());
    full.setBounds (getLocalBounds());
}
