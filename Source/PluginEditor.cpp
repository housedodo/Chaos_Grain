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
ChaosGrainEditor::ChaosGrainEditor (ChaosGrainProcessor& p)
    : AudioProcessorEditor (&p), unit (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (unit);
    setSize (UnitView::lowW * UnitView::scale, UnitView::lowH * UnitView::scale);
}

ChaosGrainEditor::~ChaosGrainEditor()
{
    setLookAndFeel (nullptr);
}

void ChaosGrainEditor::resized()
{
    unit.setBounds (getLocalBounds());
}
