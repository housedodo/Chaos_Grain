#include "UnitView.h"
#include "RetroGfx.h"

namespace
{
    namespace col
    {
        const juce::Colour casing  { 0xffa3a197 };
        const juce::Colour caseHi  { 0xffc4c2b7 };
        const juce::Colour caseLo  { 0xff77756c };
        const juce::Colour caseDk  { 0xff4c4b45 };
        const juce::Colour panel   { 0xff93918a };
        const juce::Colour label   { 0xff2f2f2b };
        const juce::Colour red     { 0xffcf2a3a };
        const juce::Colour blue    { 0xff2d3b8f };
        const juce::Colour knob    { 0xffd9d7cd };
        const juce::Colour scrA    { 0xff0f5249 };
        const juce::Colour scrB    { 0xff082824 };
        const juce::Colour grid    { 0xff1e7466 };
        const juce::Colour streak  { 0xff2a8d7d };
        const juce::Colour phos    { 0xffc8fff2 };
        const juce::Colour glow    { 0xff4fd8bf };
        const juce::Colour seg     { 0xffb8ff3a };
        const juce::Colour segOff  { 0xff1f3a12 };
        const juce::Colour segBg   { 0xff0c160a };
        const juce::Colour led     { 0xff7dff4f };
        const juce::Colour ledOff  { 0xff2b4a22 };
        const juce::Colour black   { 0xff151513 };
    }

    juce::Colour shade (juce::Colour c, float f)
    {
        auto ch = [f] (juce::uint8 v) { return (juce::uint8) juce::jlimit (0, 255, (int) ((float) v * f)); };
        return retro::to15bit (juce::Colour (ch (c.getRed()), ch (c.getGreen()), ch (c.getBlue())));
    }

    float bayer (int x, int y) { return retro::bayer4 (x, y); }

    // Pixel-exact drawing onto a low-res image.
    struct Canvas
    {
        explicit Canvas (juce::Image& i) : bd (i, juce::Image::BitmapData::readWrite), W (i.getWidth()), H (i.getHeight()) {}

        juce::Image::BitmapData bd;
        int W, H;

        void px (int x, int y, juce::Colour c) { if (x >= 0 && y >= 0 && x < W && y < H) bd.setPixelColour (x, y, c); }
        void px (float x, float y, juce::Colour c) { px ((int) std::floor (x), (int) std::floor (y), c); }

        void rect (int x, int y, int w, int h, juce::Colour c)
        {
            for (int j = y; j < y + h; ++j)
                for (int i = x; i < x + w; ++i) px (i, j, c);
        }

        void poly (const std::vector<juce::Point<float>>& p, juce::Colour c)
        {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (auto& q : p) { x0 = std::min (x0, q.x); x1 = std::max (x1, q.x); y0 = std::min (y0, q.y); y1 = std::max (y1, q.y); }
            for (int Y = (int) std::floor (y0); Y <= (int) std::ceil (y1); ++Y)
                for (int X = (int) std::floor (x0); X <= (int) std::ceil (x1); ++X)
                {
                    const float tx = (float) X + 0.5f, ty = (float) Y + 0.5f;
                    bool inside = false;
                    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
                        if ((p[i].y > ty) != (p[j].y > ty) && tx < (p[j].x - p[i].x) * (ty - p[i].y) / (p[j].y - p[i].y) + p[i].x)
                            inside = ! inside;
                    if (inside) px (X, Y, c);
                }
        }

        void chamfer (float X, float Y, float w, float h, float c, juce::Colour colour)
        {
            poly ({ { X + c, Y }, { X + w - c, Y }, { X + w, Y + c }, { X + w, Y + h - c },
                    { X + w - c, Y + h }, { X + c, Y + h }, { X, Y + h - c }, { X, Y + c } }, colour);
        }

        void bevel (float X, float Y, float w, float h, juce::Colour face, float c = 2, bool raised = true, float edge = 1)
        {
            const auto hi = raised ? shade (face, 1.25f) : shade (face, 0.55f);
            const auto lo = raised ? shade (face, 0.6f) : shade (face, 1.2f);
            const float c1 = std::max (0.0f, c - 1);
            chamfer (X, Y, w, h, c, col::black);
            chamfer (X + 1, Y + 1, w - 2, h - 2, c1, lo);
            chamfer (X + 1, Y + 1, w - 2 - edge, h - 2 - edge, c1, hi);
            chamfer (X + 1 + edge, Y + 1 + edge, w - 2 - 2 * edge, h - 2 - 2 * edge, c1, retro::to15bit (face));
        }

        void line (float fx0, float fy0, float fx1, float fy1, juce::Colour c)
        {
            int x0 = (int) fx0, y0 = (int) fy0, x1 = (int) fx1, y1 = (int) fy1;
            const int dx = std::abs (x1 - x0), dy = -std::abs (y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            int e = dx + dy;
            for (;;)
            {
                px (x0, y0, c);
                if (x0 == x1 && y0 == y1) break;
                const int e2 = 2 * e;
                if (e2 >= dy) { e += dy; x0 += sx; }
                if (e2 <= dx) { e += dx; y0 += sy; }
            }
        }

        void text (juce::String s, float X, int Y, juce::Colour c, bool centre = false)
        {
            s = s.toUpperCase();
            if (centre) X -= (float) (s.length() * 6 - 1) / 2.0f;
            const int x0 = (int) X;
            for (int i = 0; i < s.length(); ++i)
                if (auto* g = retro::glyph5x7 (s[i]))
                    for (int r = 0; r < 7; ++r)
                        for (int b = 0; b < 5; ++b)
                            if (g[r] & (0x10 >> b)) px (x0 + i * 6 + b, Y + r, c);
        }

        void small (juce::String s, float X, int Y, juce::Colour c, bool centre = false)
        {
            static const std::pair<char, std::array<juce::uint8, 5>> f3[] = {
                { '0', { 7,5,5,5,7 } }, { '1', { 2,6,2,2,7 } }, { '2', { 7,1,7,4,7 } }, { '3', { 7,1,3,1,7 } }, { '4', { 5,5,7,1,1 } },
                { '5', { 7,4,7,1,7 } }, { '6', { 7,4,7,5,7 } }, { '7', { 7,1,1,2,2 } }, { '8', { 7,5,7,5,7 } }, { '9', { 7,5,7,1,7 } },
                { '.', { 0,0,0,0,2 } }, { '-', { 0,0,7,0,0 } }, { '+', { 0,2,7,2,0 } }, { '%', { 5,1,2,4,5 } }, { '/', { 1,1,2,4,4 } },
                { 'A', { 2,5,7,5,5 } }, { 'B', { 6,5,6,5,6 } }, { 'C', { 3,4,4,4,3 } }, { 'D', { 6,5,5,5,6 } }, { 'E', { 7,4,6,4,7 } },
                { 'F', { 7,4,6,4,4 } }, { 'G', { 3,4,5,5,3 } }, { 'H', { 5,5,7,5,5 } }, { 'I', { 7,2,2,2,7 } }, { 'J', { 1,1,1,5,2 } },
                { 'K', { 5,5,6,5,5 } }, { 'L', { 4,4,4,4,7 } }, { 'M', { 5,7,7,5,5 } }, { 'N', { 6,5,5,5,5 } }, { 'O', { 2,5,5,5,2 } },
                { 'P', { 6,5,6,4,4 } }, { 'Q', { 2,5,5,6,3 } }, { 'R', { 6,5,6,5,5 } }, { 'S', { 3,4,2,1,6 } }, { 'T', { 7,2,2,2,2 } },
                { 'U', { 5,5,5,5,7 } }, { 'V', { 5,5,5,5,2 } }, { 'W', { 5,5,7,7,5 } }, { 'X', { 5,5,2,5,5 } }, { 'Y', { 5,5,2,2,2 } },
                { 'Z', { 7,1,2,4,7 } }, { ' ', { 0,0,0,0,0 } }, { '#', { 5,7,5,7,5 } },
            };
            s = s.toUpperCase();
            if (centre) X -= (float) (s.length() * 4 - 1) / 2.0f;
            const int x0 = (int) X;
            for (int i = 0; i < s.length(); ++i)
                for (auto& g : f3)
                    if ((juce::juce_wchar) g.first == s[i])
                    {
                        for (int r = 0; r < 5; ++r)
                            for (int b = 0; b < 3; ++b)
                                if (g.second[(size_t) r] & (4 >> b)) px (x0 + i * 4 + b, Y + r, c);
                        break;
                    }
        }
    };

    std::vector<juce::Point<float>> ngon (float cx, float cy, float r, int n, float rot)
    {
        std::vector<juce::Point<float>> p;
        for (int i = 0; i < n; ++i)
        {
            const float a = rot + (float) i * juce::MathConstants<float>::twoPi / (float) n;
            p.push_back ({ cx + std::cos (a) * r, cy + std::sin (a) * r });
        }
        return p;
    }

    void drawKnob (Canvas& c, float cx, float cy, float r, juce::Colour cap, float value)
    {
        const float a0 = juce::degreesToRadians (-225.0f), a1 = juce::degreesToRadians (45.0f);
        const float ang = a0 + value * (a1 - a0);

        for (int i = 0; i <= 10; ++i)
        {
            const float a = a0 + (float) i / 10.0f * (a1 - a0), rr = r + 3;
            c.px (cx + std::cos (a) * rr, cy + std::sin (a) * rr, col::label);
            if (i % 5 == 0) c.px (cx + std::cos (a) * (rr + 1), cy + std::sin (a) * (rr + 1), col::label);
        }

        c.poly (ngon (cx + 2, cy + 2, r, 14, 0), shade (col::caseDk, 0.8f));
        const float light = -2.3f;
        const auto sk = ngon (cx, cy, r, 14, ang);
        for (int i = 0; i < 14; ++i)
        {
            const float mid = ang + ((float) i + 0.5f) * juce::MathConstants<float>::twoPi / 14.0f;
            const float f = 0.72f + 0.3f * std::cos (mid - light) + (i % 2 ? 0.06f : -0.06f);
            c.poly ({ { cx, cy }, sk[(size_t) i], sk[(size_t) ((i + 1) % 14)] }, shade (col::knob, f));
        }
        c.poly (ngon (cx, cy, r * 0.74f, 14, ang), shade (col::knob, 0.86f));

        const float capRot = ang + juce::MathConstants<float>::pi / 8.0f;
        const auto cp = ngon (cx, cy, r * 0.52f, 8, capRot);
        for (int i = 0; i < 8; ++i)
        {
            const float mid = capRot + ((float) i + 0.5f) * juce::MathConstants<float>::pi / 4.0f;
            c.poly ({ { cx, cy }, cp[(size_t) i], cp[(size_t) ((i + 1) % 8)] }, shade (cap, 0.75f + 0.35f * std::cos (mid - light)));
        }

        c.line (cx + std::cos (ang) * r * 0.55f, cy + std::sin (ang) * r * 0.55f,
                cx + std::cos (ang) * (r - 1), cy + std::sin (ang) * (r - 1), juce::Colours::white);
    }

    void drawLed (Canvas& c, int X, int Y, bool on)
    {
        c.rect (X - 1, Y - 1, 5, 4, col::black);
        c.rect (X, Y, 3, 2, on ? col::led : col::ledOff);
        if (on) c.px (X, Y, juce::Colour (0xffe6ffd0));
    }

    void sevenSeg (Canvas& c, int X, int Y, const juce::String& str, int digits)
    {
        static const std::map<juce::juce_wchar, const char*> seg = {
            { '0', "abcdef" }, { '1', "bc" }, { '2', "abged" }, { '3', "abgcd" }, { '4', "fgbc" }, { '5', "afgcd" },
            { '6', "afgedc" }, { '7', "abc" }, { '8', "abcdefg" }, { '9', "abcfgd" }, { '-', "g" }, { ' ', "" } };

        struct D { juce::juce_wchar ch; bool dp; bool off; };
        std::vector<D> ds;
        for (auto ch : str)
        {
            if (ch == '.') { if (! ds.empty()) ds.back().dp = true; }
            else ds.push_back ({ ch, false, false });
        }
        while ((int) ds.size() > digits) ds.erase (ds.begin());
        while ((int) ds.size() < digits) ds.insert (ds.begin(), { '8', false, true });

        const int w = 5, h = 9;
        for (size_t i = 0; i < ds.size(); ++i)
        {
            const int ox = X + (int) i * (w + 3);
            const juce::String on = ds[i].off ? "" : (seg.count (ds[i].ch) ? seg.at (ds[i].ch) : "");
            auto s = [&] (char k, int x, int y, int ww, int hh) { c.rect (x, y, ww, hh, on.containsChar (k) ? col::seg : col::segOff); };
            s ('a', ox + 1, Y, w - 2, 1);         s ('b', ox + w - 1, Y + 1, 1, 3);
            s ('c', ox + w - 1, Y + 5, 1, 3);     s ('d', ox + 1, Y + h - 1, w - 2, 1);
            s ('e', ox, Y + 5, 1, 3);             s ('f', ox, Y + 1, 1, 3);
            s ('g', ox + 1, Y + 4, w - 2, 1);
            c.rect (ox + w + 1, Y + h - 1, 1, 1, ds[i].dp ? col::seg : col::segOff);
        }
    }

    // Screen geometry (low-res)
    constexpr int crtX = 6, crtY = 22, crtW = 116, crtH = 66;
    constexpr int scrX = crtX + 5, scrY = crtY + 5, scrW = crtW - 10, scrH = crtH - 10;
    const juce::Rectangle<int> powerRect { 8, 7, 12, 8 };
    const juce::Rectangle<int> syncRect { 33, 7, 12, 8 };
    const juce::Rectangle<int> randRect { 51, 7, 12, 8 };
    // Middle-right panel: LOCK (+ FOLLOW in FX)
    const juce::Rectangle<int> lockRect   { kIsFx ? 68 : 66, 94, 12, 8 };
    const juce::Rectangle<int> followRect { 96, 94, 12, 8 };
    // FX: engine buttons under the screen
    const juce::Rectangle<int> engineRects[3] = { { 19, 94, 12, 8 }, { 58, 94, 12, 8 }, { 97, 94, 12, 8 } };

    // Small push-button: grey cap, lit when on.
    void drawMiniButton (Canvas& c, juce::Rectangle<int> r, bool on, juce::Colour litColour)
    {
        c.bevel ((float) r.getX(), (float) r.getY(), 12, 8, col::caseLo, 1, false);
        c.bevel ((float) r.getX() + 2, (float) r.getY() + (on ? 3 : 2), 8, 4,
                 on ? litColour : juce::Colour (0xffcfcdc3), 0, ! on);
    }

    constexpr float miniY = 176.0f, miniY2 = 202.0f, miniR = 5.0f;
}

std::vector<UnitView::KnobSpec> UnitView::makeKnobSpecs()
{
    if (kIsFx)
        return { { "shape", "SHAPE", 24.0f, 132.0f, 9.0f,  false, false },
                 { "chaos", "CHAOS", 64.0f, 133.0f, 15.0f, true,  false },
                 { "mix",   "MIX",   104.0f, 132.0f, 9.0f, false, false } };

    std::vector<KnobSpec> k = {
        { "size",  "SIZE",  24.0f, 132.0f, 9.0f,  false, false },
        { "chaos", "CHAOS", 64.0f, 133.0f, 15.0f, true,  false },
        { kIsFx ? "mix" : "pitch", kIsFx ? "MIX" : "PITCH", 104.0f, 132.0f, 9.0f, false, false },
    };

    const std::vector<std::pair<const char*, const char*>> minis = kIsFx
        ? std::vector<std::pair<const char*, const char*>> { { "filter", "FILTER" }, { "space", "SPACE" }, { "repeats", "REPEAT" }, { "rate", "TIME" } }
        : std::vector<std::pair<const char*, const char*>> { { "filter", "FILTER" }, { "reso", "RESO" }, { "space", "SPACE" } };

    for (size_t i = 0; i < minis.size(); ++i)
    {
        const float cx = 6.0f + 116.0f * ((float) i + 0.5f) / (float) minis.size();
        k.push_back ({ minis[i].first, minis[i].second, std::round (cx), miniY, miniR, false, true });
    }

    // Second row: sound character, key lock, swing
    const std::pair<const char*, const char*> row2[] = { { "character", "CHAR" }, { "scale", "KEY" }, { "swing", "SWING" } };
    for (int i = 0; i < 3; ++i)
    {
        const float cx = 6.0f + 116.0f * ((float) i + 0.5f) / 3.0f;
        k.push_back ({ row2[i].first, row2[i].second, std::round (cx), miniY2, miniR, false, true });
    }
    return k;
}

//==============================================================================
UnitView::UnitView (ChaosGrainProcessor& p) : proc (p)
{
    for (int i = 0; i < (int) knobSpecs.size(); ++i)
    {
        sliders.push_back (std::make_unique<juce::Slider>());
        auto& s = *sliders.back();
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setLookAndFeel (&invisible);
        s.setMouseDragSensitivity (180);
        s.setPopupMenuEnabled (false);
        s.onValueChange = [this, i] { touched = i; touchedTime = juce::Time::getMillisecondCounterHiRes(); };
        addAndMakeVisible (s);
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, knobSpecs[(size_t) i].paramId, s));

        if (auto* param = proc.apvts.getParameter (knobSpecs[(size_t) i].paramId))
            s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }
    touched = -1;

    // UI size: remembered per project; default depends on the screen height.
    const auto saved = proc.apvts.state.getProperty ("uiScale");
    if (! saved.isVoid()) scale = juce::jlimit (3, 4, (int) saved);
    else if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        scale = d->userArea.getHeight() >= lowH * 4 + 120 ? 4 : 3;

    setSize (lowW * scale, lowH * scale);
    startTimerHz (30);
}

UnitView::~UnitView()
{
    stopTimer();
    for (auto& s : sliders) s->setLookAndFeel (nullptr);
}

void UnitView::resized()
{
    for (size_t i = 0; i < knobSpecs.size(); ++i)
    {
        const auto& k = knobSpecs[i];
        const int r = (int) k.r + 3;
        sliders[i]->setBounds (((int) k.cx - r) * scale, ((int) k.cy - r) * scale, r * 2 * scale, r * 2 * scale);
    }
    refreshPeaks();
}

void UnitView::timerCallback()
{
    if (kIsFx)
        proc.engine.getLivePeaks (peaks, scrW - 6);
    else if (proc.getSampleVersion() != lastSampleVersion)
    {
        lastSampleVersion = proc.getSampleVersion();
        refreshPeaks();
    }
    repaint();
}

void UnitView::refreshPeaks()
{
    if (kIsFx) { proc.engine.getLivePeaks (peaks, scrW - 6); return; }
    peaks.clear();
    auto s = proc.getCurrentSample();
    if (s == nullptr) return;

    const auto& buf = s->buffer;
    const int cols = scrW - 6, n = buf.getNumSamples();
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
}

void UnitView::render (juce::Image& img)
{
    if (kIsFx) { renderFx (img); return; }

    Canvas c (img);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const bool hold = proc.apvts.getRawParameterValue (kIsFx ? "freeze" : "hold")->load() > 0.5f;
    const int active = proc.engine.uiActiveGrains.load (std::memory_order_relaxed);

    // --- Casing ---
    c.bevel (0, 0, (float) lowW, (float) lowH, col::casing, 5, true, 2);
    for (int j = 4; j < lowH - 4; ++j)
        for (int i = 4; i < lowW - 4; ++i)
            if ((float) j / (float) lowH > 0.55f + bayer (i, j) * 0.6f) c.px (i, j, shade (col::casing, 0.94f));

    // --- Power (= HOLD) + activity LED ---
    c.bevel ((float) powerRect.getX(), (float) powerRect.getY(), 12, 8, col::caseLo, 1, false);
    c.bevel ((float) powerRect.getX() + 2, (float) powerRect.getY() + (hold ? 3 : 2), 8, 4,
             hold ? shade (col::red, 1.25f) : col::red, 0, ! hold);
    drawLed (c, 24, 10, active > 0 && (active > 2 || std::fmod (nowMs, 200.0) < 120.0));
    c.small (kIsFx ? "FREEZE" : "HOLD", 14, 17, col::label, true);

    const bool syncOn = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    drawMiniButton (c, syncRect, syncOn, col::led);
    c.small ("SYNC", syncRect.getCentreX(), 17, col::label, true);
    drawMiniButton (c, randRect, nowMs - randFlashMs < 180.0, juce::Colour (0xffffd166));
    c.small ("RAND", randRect.getCentreX(), 17, col::label, true);

    c.text ("CHAOS", 96, 6, col::label, true);
    c.small (kIsFx ? "GRAIN FX" : "GRAIN UNIT", 96, 14, col::caseDk, true);

    // --- CRT ---
    c.bevel (crtX, crtY, crtW, crtH, juce::Colour (0xff8b8a82), 4, false, 2);
    c.chamfer (scrX - 1, scrY - 1, scrW + 2, scrH + 2, 4, col::black);
    for (int j = 0; j < scrH; ++j)
        for (int i = 0; i < scrW; ++i)
        {
            const bool cut = (i < 3 - j) || (i < 3 - (scrH - 1 - j)) || (scrW - 1 - i < 3 - j) || (scrW - 1 - i < 3 - (scrH - 1 - j));
            if (cut) continue;
            const float dx = ((float) i - scrW / 2.0f) / (scrW / 2.0f), dy = ((float) j - scrH / 2.0f) / (scrH / 2.0f);
            const float d = std::min (1.0f, std::sqrt (dx * dx * 0.6f + dy * dy * 0.6f));
            c.px (scrX + i, scrY + j, d > bayer (i, j) * 0.6f + 0.45f ? col::scrB : col::scrA);
        }
    for (int k = 1; k < 8; ++k)
        for (int j = 2; j < scrH - 2; j += 2) c.px (scrX + (int) std::round ((float) k * scrW / 8.0f), scrY + j, col::grid);
    for (int k = 1; k < 6; ++k)
        for (int i = 2; i < scrW - 2; i += 2) c.px (scrX + i, scrY + (int) std::round ((float) k * scrH / 6.0f), col::grid);

    // slice markers
    const int slices = (int) proc.apvts.getRawParameterValue ("slices")->load();
    if (slices > 1 && ! peaks.empty())
        for (int s = 1; s < slices; ++s)
        {
            const int xx = scrX + 3 + (int) peaks.size() * s / slices;
            for (int j = 3; j < scrH - 3; j += 3) c.px (xx, scrY + j, col::glow);
        }

    for (int j = 0; j < scrH; ++j)
    {
        const int i = (int) ((float) scrW * 0.72f - (float) j * 0.35f);
        if ((i + j) % 2 == 0 && i > 2 && i < scrW - 2) c.px (scrX + i, scrY + j, col::streak);
    }

    const int mid = scrY + scrH / 2;
    float amp = (float) scrH * 0.40f;
    if (kIsFx && ! peaks.empty())
    {
        float pk = 0.05f;
        for (auto& pr : peaks) pk = std::max (pk, std::max (-pr.first, pr.second));
        amp /= std::min (1.0f, pk * 1.1f);
    }

    if (peaks.empty())
    {
        if (std::fmod (nowMs, 1200.0) < 800.0)
            c.text ("INSERT", scrX + scrW / 2.0f, mid - 10, col::phos, true);
        c.small ("CLICK OR DROP FILE", scrX + scrW / 2.0f, mid + 3, col::glow, true);
    }
    else
    {
        // Phosphor trace: glow fill between peaks + bright outline
        int prevTop = 0, prevBot = 0;
        for (size_t k = 0; k < peaks.size(); ++k)
        {
            const int xx = scrX + 3 + (int) k;
            const int top = mid - (int) std::round (peaks[k].second * amp);
            const int bot = mid - (int) std::round (peaks[k].first * amp);
            for (int y = top + 1; y < bot; ++y)
                if ((xx + y) % 2 == 0) c.px (xx, y, col::grid.brighter (0.3f));
            if (k > 0)
            {
                for (int y = std::min (prevTop, top); y <= std::max (prevTop, top); ++y) c.px (xx, y, col::phos);
                for (int y = std::min (prevBot, bot); y <= std::max (prevBot, bot); ++y) c.px (xx, y, col::phos);
            }
            prevTop = top; prevBot = bot;
        }

        // Playhead
        const float ph = proc.engine.uiPlayhead.load (std::memory_order_relaxed);
        const int phx = scrX + 3 + (int) (ph * (float) peaks.size());
        for (int j = 2; j < scrH - 2; j += 2) c.px (phx, scrY + j, col::seg);

        // Grains: bright glowing blocks
        const int n = proc.engine.uiNumGrains.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const float gp = proc.engine.uiGrainPos[(size_t) i].load (std::memory_order_relaxed);
            const float lv = proc.engine.uiGrainLevel[(size_t) i].load (std::memory_order_relaxed);
            if (lv < 0.05f) continue;
            const int gx = scrX + 3 + (int) (gp * (float) peaks.size());
            const int gy = mid + (int) (std::sin ((float) i * 2.39996f) * (float) scrH * 0.32f);
            const int s = lv > 0.6f ? 2 : 1;
            if (gx < scrX + 2 || gx > scrX + scrW - 4) continue;
            c.rect (gx - 1, gy - 1, s + 2, s + 2, col::glow);
            c.rect (gx, gy, s, s, col::phos);
        }
    }

    // FX: mode selector at the top of the screen  "< MODE >"
    if (kIsFx)
    {
        const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
        const auto name = ChaosGrainProcessor::modeNames()[mode].toUpperCase();
        const float cx = scrX + scrW / 2.0f;
        const int ty = scrY + 3;
        const int w = name.length() * 6 - 1;
        c.rect ((int) (cx - w / 2.0f) - 3, ty - 1, w + 6, 9, col::scrB);
        c.text (name, cx, ty, col::phos, true);
        c.text ("<", (float) scrX + 6, ty, col::glow, false);
        c.text (">", (float) (scrX + scrW - 10), ty, col::glow, false);
    }

    if (dragging)
    {
        c.rect (scrX, scrY, scrW, 1, col::seg); c.rect (scrX, scrY + scrH - 1, scrW, 1, col::seg);
        c.rect (scrX, scrY, 1, scrH, col::seg); c.rect (scrX + scrW - 1, scrY, 1, scrH, col::seg);
    }

    // --- Readout: grain count, or the value of the knob being turned ---
    juce::String readout = juce::String (juce::jmin (active, 99999)) + ".";
    juce::String caption = "GRAINS";
    if (touched >= 0 && nowMs - touchedTime < 1500.0)
    {
        const float v = (float) sliders[(size_t) touched]->getValue();
        const juce::String id = knobSpecs[(size_t) touched].paramId;
        auto pct = [v] { return juce::String (juce::roundToInt (v * 100.0f)); };
        if (id == "size")    { caption = v >= 1000.0f ? "SIZE S" : "SIZE MS"; readout = v >= 1000.0f ? juce::String (v / 1000.0f, 2) : juce::String (juce::roundToInt (v)); }
        if (id == "chaos")   { caption = "CHAOS %";   readout = pct(); }
        if (id == "pitch")   { caption = "PITCH ST";  readout = juce::String (v, 1); }
        if (id == "mix")     { caption = "MIX %";     readout = pct(); }
        if (id == "filter")  { caption = "FILTER KHZ"; readout = juce::String (v / 1000.0f, 1); }
        if (id == "reso")    { caption = "RESO %";    readout = pct(); }
        if (id == "space")   { caption = "SPACE %";   readout = pct(); }
        if (id == "repeats") { caption = "REPEATS %"; readout = pct(); }
        if (id == "memory")  { caption = "MEMORY S";  readout = juce::String (v, 2); }
        if (id == "rate")
        {
            const auto name = ChaosGrainProcessor::rateNames()[juce::roundToInt (v)];
            caption = "TIME " + name;
            readout = name.fromFirstOccurrenceOf ("/", false, false).retainCharacters ("0123456789");
        }
        if (id == "character") { caption = "CHAR " + ChaosGrainProcessor::characterNames()[juce::roundToInt (v)].toUpperCase(); readout = juce::String (juce::roundToInt (v) + 1); }
        if (id == "scale")   { caption = "KEY " + ChaosGrainProcessor::scaleNames()[juce::roundToInt (v)].toUpperCase(); readout = juce::roundToInt (v) == 0 ? "-" : juce::String (juce::roundToInt (v)); }
        if (id == "swing")   { caption = "SWING %";   readout = pct(); }
    }
    c.small (caption, 33, 92, col::label, true);
    c.bevel (8, 98, 50, 12, col::caseLo, 1, false);
    c.rect (10, 100, 46, 8, col::segBg);
    sevenSeg (c, 58 - 4 - 5 * 8, 99, readout, 5);

    // --- LOCK / FOLLOW panel ---
    c.bevel (62, 92, 58, 18, col::panel, 2, false);
    const bool lockOn = proc.apvts.getRawParameterValue ("lock")->load() > 0.5f;
    drawMiniButton (c, lockRect, lockOn, col::seg);
    c.small ("LOCK", lockRect.getCentreX(), 104, col::label, true);

    if (kIsFx)
    {
        const int onsets = proc.engine.uiOnsets.load (std::memory_order_relaxed);
        if (onsets != lastOnsets) { lastOnsets = onsets; onsetFlashMs = nowMs; }
        const bool followOn = proc.apvts.getRawParameterValue ("follow")->load() > 0.5f;
        const bool flash = followOn && nowMs - onsetFlashMs < 90.0;
        drawMiniButton (c, followRect, followOn, flash ? juce::Colour (0xffeaffd8) : col::led);
        c.small ("FOLLOW", followRect.getCentreX(), 104, col::label, true);
    }
    else
    {
        // Instrument: small density meter next to LOCK
        const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
        const float dens = sync ? proc.apvts.getParameter ("rate")->getValue() : proc.apvts.getParameter ("density")->getValue();
        const int lit = juce::jlimit (1, 4, (int) std::ceil (dens * 4.0f));
        for (int i = 0; i < 4; ++i) drawLed (c, 87 + i * 7, 97, i < lit);
        c.small (sync ? "RATE" : "DENSITY", 98, 104, col::label, true);
    }

    // --- Knobs ---
    c.bevel (6, 114, 116, 50, col::panel, 3, false);
    c.bevel (6, 166, 116, 52, col::panel, 2, false);
    c.rect (9, 192, 110, 1, shade (col::panel, 0.8f)); // divider between the two rows
    for (size_t i = 0; i < knobSpecs.size(); ++i)
    {
        const auto& k = knobSpecs[i];
        const float v = (float) sliders[i]->valueToProportionOfLength (sliders[i]->getValue());
        if (k.mini)
        {
            const juce::String id = k.paramId;
            juce::String label = k.label;
            const int idx = juce::roundToInt (sliders[i]->getValue());
            if (id == "character") label = ChaosGrainProcessor::characterNames()[idx].toUpperCase();
            if (id == "scale" && idx > 0) label = ChaosGrainProcessor::scaleNames()[idx].toUpperCase();
            if (id == "rate") label = ChaosGrainProcessor::rateNames()[idx];
            const bool engaged = (id == "character" || id == "scale") && idx > 0;
            drawKnob (c, k.cx, k.cy, k.r, engaged ? juce::Colour (0xffb8a03a) : juce::Colour (0xff6b6a63), v);
            c.small (label, k.cx, (int) (k.cy + k.r + 5), col::label, true);
        }
        else
        {
            drawKnob (c, k.cx, k.cy, k.r, k.red ? col::red : col::blue, v);
            c.text (k.label, k.cx, (int) (k.cy + k.r + (k.red ? 5 : 6)), col::label, true);
        }
    }

    // Screws
    for (auto pt : { juce::Point<int> (3, 3), { lowW - 4, 3 }, { 3, lowH - 4 }, { lowW - 4, lowH - 4 } })
    {
        c.rect (pt.x - 1, pt.y - 1, 3, 3, col::caseDk);
        c.rect (pt.x - 1, pt.y, 3, 1, col::caseHi);
    }
}

void UnitView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));
    juce::Image img (juce::Image::ARGB, lowW, lowH, true);
    render (img);
    retro::blitLowRes (g, img, 0, 0, scale);
}

void UnitView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { kIsFx ? showFxMenu() : showMenu(); return; }

    const auto p = e.getPosition() / scale;
    if (kIsFx)
    {
        auto toggle = [this] (const char* id)
        {
            auto* q = proc.apvts.getParameter (id);
            q->setValueNotifyingHost (q->getValue() > 0.5f ? 0.0f : 1.0f);
        };
        if (powerRect.expanded (1).contains (p)) { toggle ("freeze"); return; }
        for (int i = 0; i < 3; ++i)
            if (engineRects[i].expanded (3).contains (p))
            {
                auto* eng = proc.apvts.getParameter ("engine");
                eng->setValueNotifyingHost (eng->convertTo0to1 ((float) i));
                return;
            }
        if (juce::Rectangle<int> (crtX, crtY, crtW, crtH).contains (p)) toggle ("freeze"); // double-click = variation
        return;
    }
    if (powerRect.expanded (1).contains (p))
    {
        auto* hold = proc.apvts.getParameter (kIsFx ? "freeze" : "hold");
        hold->setValueNotifyingHost (hold->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    else if (syncRect.expanded (1).contains (p))
    {
        auto* sync = proc.apvts.getParameter ("sync");
        sync->setValueNotifyingHost (sync->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    else if (lockRect.expanded (1).contains (p))
    {
        auto* lock = proc.apvts.getParameter ("lock");
        lock->setValueNotifyingHost (lock->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    else if (kIsFx && followRect.expanded (1).contains (p))
    {
        auto* follow = proc.apvts.getParameter ("follow");
        follow->setValueNotifyingHost (follow->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    else if (randRect.expanded (1).contains (p))
    {
        randFlashMs = juce::Time::getMillisecondCounterHiRes();
        proc.randomize();
    }
    else if (kIsFx && juce::Rectangle<int> (crtX, crtY, crtW, crtH).contains (p))
    {
        // Click the left half of the screen for the previous mode, right half for the next.
        auto* mode = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("mode"));
        const int n = mode->choices.size();
        const int next = (mode->getIndex() + (p.x < crtX + crtW / 2 ? n - 1 : 1)) % n;
        mode->setValueNotifyingHost (mode->convertTo0to1 ((float) next));
    }
    else if (! kIsFx && juce::Rectangle<int> (crtX, crtY, crtW, crtH).contains (p))
    {
        loadViaChooser();
    }
}

void UnitView::setScale (int newScale)
{
    scale = juce::jlimit (3, 4, newScale);
    proc.apvts.state.setProperty ("uiScale", scale, nullptr);
    setSize (lowW * scale, lowH * scale);
    if (onScaleChanged) onScaleChanged();
}

void UnitView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! kIsFx) return;
    if (juce::Rectangle<int> (crtX, crtY, crtW, crtH).contains (e.getPosition() / scale))
    {
        // The two clicks toggled FREEZE twice (net: unchanged); now roll a new variation.
        proc.newVariation();
        variationFlashMs = juce::Time::getMillisecondCounterHiRes();
    }
}

void UnitView::showFxMenu()
{
    juce::PopupMenu engines, times;
    const int eng = (int) proc.apvts.getRawParameterValue ("engine")->load();
    const auto names = ChaosGrainProcessor::engineNames();
    for (int i = 0; i < names.size(); ++i)
        engines.addItem (names[i], true, i == eng, [this, i]
        {
            auto* q = proc.apvts.getParameter ("engine");
            q->setValueNotifyingHost (q->convertTo0to1 ((float) i));
        });

    const int rate = (int) proc.apvts.getRawParameterValue ("rate")->load();
    const auto rates = ChaosGrainProcessor::rateNames();
    for (int i = 0; i < rates.size(); ++i)
        times.addItem (rates[i], true, i == rate, [this, i]
        {
            auto* q = proc.apvts.getParameter ("rate");
            q->setValueNotifyingHost (q->convertTo0to1 ((float) i));
        });

    const bool lock = proc.apvts.getRawParameterValue ("lock")->load() > 0.5f;
    const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;

    juce::PopupMenu m;
    m.addSubMenu ("Engine", engines);
    m.addSubMenu ("Time (tempo grid)", times);
    m.addSeparator();
    m.addItem ("Freeze", true, frozen, [this, frozen] { proc.apvts.getParameter ("freeze")->setValueNotifyingHost (frozen ? 0.0f : 1.0f); });
    m.addItem ("Lock pattern", true, lock, [this, lock] { proc.apvts.getParameter ("lock")->setValueNotifyingHost (lock ? 0.0f : 1.0f); });
    m.addItem ("New variation", [this] { proc.newVariation(); variationFlashMs = juce::Time::getMillisecondCounterHiRes(); });
    m.addItem ("Randomize knobs", [this] { proc.randomize(); variationFlashMs = juce::Time::getMillisecondCounterHiRes(); });
    m.addSeparator();
    m.addItem ("Large UI", true, scale == 4, [this] { setScale (4); });
    m.addItem ("Small UI", true, scale == 3, [this] { setScale (3); });
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void UnitView::renderFx (juce::Image& img)
{
    Canvas c (img);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float t = (float) (nowMs * 0.001);
    const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    const int eng = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("engine")->load());
    const float shape = proc.apvts.getRawParameterValue ("shape")->load();

    // Activity (for the LED)
    float activity = 0.0f;
    if (eng == 0) for (auto& l : proc.resonator.uiLevel) activity = std::max (activity, l.load (std::memory_order_relaxed));
    else if (eng == 1) for (auto& b : proc.spectral.uiBands) activity = std::max (activity, b.load (std::memory_order_relaxed) * 50.0f);
    else activity = proc.keyDetector.uiEnergy.load() + proc.evolve.uiRecall.load();

    // --- Casing ---
    c.bevel (0, 0, (float) lowW, (float) lowH, col::casing, 5, true, 2);
    for (int j = 4; j < lowH - 4; ++j)
        for (int i = 4; i < lowW - 4; ++i)
            if ((float) j / (float) lowH > 0.55f + bayer (i, j) * 0.6f) c.px (i, j, shade (col::casing, 0.94f));

    // --- FREEZE button + activity LED + title ---
    c.bevel ((float) powerRect.getX(), (float) powerRect.getY(), 12, 8, col::caseLo, 1, false);
    c.bevel ((float) powerRect.getX() + 2, (float) powerRect.getY() + (frozen ? 3 : 2), 8, 4,
             frozen ? shade (col::red, 1.25f) : col::red, 0, ! frozen);
    drawLed (c, 24, 10, activity > 0.01f);
    c.small ("FREEZE", 14, 17, col::label, true);
    c.text ("CHAOS", 96, 6, col::label, true);
    c.small ("GRAIN FX", 96, 14, col::caseDk, true);

    // --- CRT ---
    c.bevel (crtX, crtY, crtW, crtH, juce::Colour (0xff8b8a82), 4, false, 2);
    c.chamfer (scrX - 1, scrY - 1, scrW + 2, scrH + 2, 4, col::black);
    for (int j = 0; j < scrH; ++j)
        for (int i = 0; i < scrW; ++i)
        {
            const bool cut = (i < 3 - j) || (i < 3 - (scrH - 1 - j)) || (scrW - 1 - i < 3 - j) || (scrW - 1 - i < 3 - (scrH - 1 - j));
            if (cut) continue;
            const float dx = ((float) i - scrW / 2.0f) / (scrW / 2.0f), dy = ((float) j - scrH / 2.0f) / (scrH / 2.0f);
            const float d = std::min (1.0f, std::sqrt (dx * dx * 0.6f + dy * dy * 0.6f));
            c.px (scrX + i, scrY + j, d > bayer (i, j) * 0.6f + 0.45f ? col::scrB : col::scrA);
        }
    for (int k = 1; k < 8; ++k)
        for (int j = 2; j < scrH - 2; j += 2) c.px (scrX + (int) std::round ((float) k * scrW / 8.0f), scrY + j, col::grid);

    if (eng == 0)
    {
        // Vibrating strings, one per resonator, with their note names
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int v = 0; v < Resonator::numVoices; ++v)
        {
            const float lv = proc.resonator.uiLevel[(size_t) v].load (std::memory_order_relaxed);
            const float m = proc.resonator.uiMidi[(size_t) v].load (std::memory_order_relaxed);
            const int y0 = scrY + 13 + v * 6;
            const int note = (int) std::lround (m);
            c.small (m < 0.0f ? juce::String ("--") : juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1),
                     (float) scrX + 2, y0 - 2, col::glow);
            const float amp = std::min (2.5f, lv * 25.0f);
            const float freq = 0.25f + 0.05f * (float) v;
            for (int x = scrX + 16; x < scrX + scrW - 3; ++x)
            {
                const float env = std::sin (juce::MathConstants<float>::pi * (float) (x - scrX - 16) / (float) (scrW - 19));
                const int dy = (int) std::lround (amp * env * std::sin ((float) x * freq + t * (14.0f + 3.0f * (float) v)));
                c.px (x, y0 + dy, amp > 0.3f ? col::phos : col::glow);
            }
        }
    }
    else if (eng == 2)
    {
        // A pixel plant that grows with your playing: one leaf per stage, a flower for SHIMMER
        const float grow = proc.evolve.uiGrowth.load (std::memory_order_relaxed);
        float st[5];
        for (int k = 0; k < 5; ++k) st[k] = proc.evolve.uiStage[(size_t) k].load (std::memory_order_relaxed);
        const int ground = scrY + scrH - 10, cx = scrX + scrW / 2 - 4;
        for (int x = scrX + 3; x < scrX + scrW - 10; ++x) c.px (x, ground + 1, (x & 1) ? col::glow : col::scrB);

        const int h = 3 + (int) std::lround (grow * 30.0f);
        auto stemX = [&] (int y) { return cx + (int) std::lround (std::sin (t * 1.3f + (float) y * 0.18f) * (0.4f + grow) * (float) y / 30.0f); };
        for (int y = 0; y <= h; ++y) c.px (stemX (y), ground - y, col::phos);
        for (int k = 1; k <= 3; ++k) // leaves: RESONATE, ECHOES, DRONE
        {
            if (st[k] < 0.05f) continue;
            const int y = (int) ((float) h * (0.25f + 0.22f * (float) (k - 1)));
            const int len = 1 + (int) std::lround (st[k] * 5.0f);
            const int dir = (k & 1) ? -1 : 1;
            const int x0 = stemX (y);
            for (int j = 1; j <= len; ++j)
            {
                c.px (x0 + dir * j, ground - y - j / 2, col::phos);
                if (j < len) c.px (x0 + dir * j, ground - y - j / 2 + 1, col::glow);
            }
        }
        if (st[4] > 0.05f) // SHIMMER: a flower, sparkling
        {
            const int fx = stemX (h), fy = ground - h - 1;
            const int r = st[4] > 0.6f ? 2 : 1;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if (std::abs (dx) + std::abs (dy) <= r) c.px (fx + dx, fy + dy, (dx == 0 && dy == 0) ? col::glow : col::seg);
            for (int k = 0; k < 4; ++k)
            {
                const float a = t * 0.9f + (float) k * 1.7f;
                if (std::fmod (t * 3.0f + (float) k * 0.37f, 1.0f) < st[4] * 0.6f)
                    c.px (fx + (int) std::lround (std::cos (a) * 6.0f), fy + (int) std::lround (std::sin (a) * 4.0f), col::phos);
            }
        }
        // Memories: seeds on the ground; they light up while it answers you
        const int mems = proc.evolve.uiMemories.load();
        const float rec = proc.evolve.uiRecall.load();
        for (int k = 0; k < mems; ++k)
            c.rect (scrX + 4 + k * 3, ground - 1, 2, 2, rec > 0.05f && std::fmod (t * 6.0f + (float) k, 2.0f) < 1.0f ? col::seg : col::glow);
        // Growth meter with stage ticks
        const int mx = scrX + scrW - 7, top = scrY + 14;
        const int fill = (int) std::lround (grow * (float) (ground - top));
        for (int y = top; y <= ground; ++y) c.px (mx, y, col::scrB);
        for (int y = 0; y < fill; ++y) c.rect (mx - 1, ground - y, 3, 1, col::glow);
        for (float th : { 0.12f, 0.3f, 0.5f, 0.7f }) c.px (mx + 2, ground - (int) std::lround (th * (float) (ground - top)), col::phos);
        static const char* stageNames[5] = { "SPACE", "RESONATE", "ECHOES", "DRONE", "SHIMMER" };
        int stage = 0;
        for (int k = 1; k < 5; ++k) if (st[k] > 0.5f) stage = k;
        c.small (stageNames[stage], (float) scrX + 3, scrY + 13, col::glow);
    }
    else
    {
        // Live spectrum of the processed sound
        const int base = scrY + scrH - 10, maxH = scrH - 21;
        for (int b = 0; b < Spectral::numBands; ++b)
        {
            const float v = proc.spectral.uiBands[(size_t) b].load (std::memory_order_relaxed);
            const float db = 20.0f * std::log10 (v + 1.0e-6f);
            const int hgt = (int) (juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 60.0f) * (float) maxH);
            const int x = scrX + 5 + b * 3;
            for (int y = 0; y < hgt; ++y) c.rect (x, base - y, 2, 1, y == hgt - 1 ? col::phos : col::glow);
        }
    }

    // Header: engine name
    {
        const auto name = ChaosGrainProcessor::engineNames()[eng].toUpperCase();
        const float cx = scrX + scrW / 2.0f;
        const int w = name.length() * 6 - 1;
        c.rect ((int) (cx - w / 2.0f) - 3, scrY + 2, w + 6, 9, col::scrB);
        c.text (name, cx, scrY + 3, col::phos, true);
    }

    // Footer: detected key / knob read-out / FROZEN
    {
        const int fy = scrY + scrH - 7;
        juce::String footer;
        if (nowMs - variationFlashMs < 1000.0) footer = "NEW VARIATION";
        else if (touched >= 0 && nowMs - touchedTime < 1500.0)
        {
            const float v = (float) sliders[(size_t) touched]->getValue();
            const juce::String id = knobSpecs[(size_t) touched].paramId;
            const juce::String pct = juce::String (juce::roundToInt (v * 100.0f)) + "%";
            if (id == "shape")
            {
                if (eng == 2)
                {
                    const int secs = juce::roundToInt (Evolve::growSeconds (v));
                    footer = "GROW " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
                }
                else
                {
                    const char* what = eng == 0 ? (v < 0.3f ? "ECHO" : v < 0.65f ? "SUSTAIN" : "SWELL")
                                     : (v < 0.28f ? "TUNE" : v < 0.62f ? "UNISON" : "RAINBOW");
                    footer = juce::String (what) + " " + pct;
                }
            }
            else footer = (id == "chaos" ? "CHAOS " : "MIX ") + pct;
        }
        else
        {
            const int root = proc.keyDetector.uiRoot.load();
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            footer = root < 0 ? juce::String ("KEY --") : "KEY " + juce::String (names[root]) + (proc.keyDetector.uiMinor.load() ? " MIN" : " MAJ");
        }
        c.small (footer, (float) scrX + 4, fy, col::glow);
        if (frozen)
        {
            if (std::fmod (nowMs, 1000.0) < 650.0) c.small ("FROZEN", (float) (scrX + scrW - 27), fy, col::seg);
        }
        else if (proc.keyDetector.uiSection.load() > 0 && footer.length() <= 13)
        {
            // what the song brain hears right now
            const float e = proc.keyDetector.uiEnergy.load();
            c.small (e < 0.35f ? "CALM" : e < 0.7f ? "BUILD" : "PEAK", (float) (scrX + scrW - 23), fy, col::phos);
        }
    }

    // --- Engine buttons ---
    c.bevel (6, 91, 116, 21, col::panel, 2, false);
    const auto names = ChaosGrainProcessor::engineNames();
    for (int i = 0; i < 3; ++i)
    {
        const bool on = i == eng;
        drawMiniButton (c, engineRects[i], on, col::led);
        c.small (names[i].toUpperCase(), engineRects[i].getCentreX(), 104, on ? col::black : col::label, true);
    }

    // --- Knobs ---
    c.bevel (6, 114, 116, 50, col::panel, 3, false);
    for (size_t i = 0; i < knobSpecs.size(); ++i)
    {
        const auto& k = knobSpecs[i];
        const float v = (float) sliders[i]->valueToProportionOfLength (sliders[i]->getValue());
        drawKnob (c, k.cx, k.cy, k.r, k.red ? col::red : col::blue, v);
        c.text (k.label, k.cx, (int) (k.cy + k.r + (k.red ? 5 : 6)), col::label, true);
    }

    for (auto pt : { juce::Point<int> (3, 3), { lowW - 4, 3 }, { 3, lowH - 4 }, { lowW - 4, lowH - 4 } })
    {
        c.rect (pt.x - 1, pt.y - 1, 3, 3, col::caseDk);
        c.rect (pt.x - 1, pt.y, 3, 1, col::caseHi);
    }
    juce::ignoreUnused (shape);
}

void UnitView::loadViaChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample", juce::File(), proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  proc.loadSample (fc.getResult());
                          });
}

void UnitView::showMenu()
{
    juce::PopupMenu presets;
    const auto names = ChaosGrainProcessor::getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presets.addItem (names[i], [this, i] { proc.applyPreset (i); });

    const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    const char* holdId = kIsFx ? "freeze" : "hold";
    const bool hold = proc.apvts.getRawParameterValue (holdId)->load() > 0.5f;

    juce::PopupMenu m;
    if (! kIsFx)
    {
        m.addItem ("Load sample...", [this] { loadViaChooser(); });
        m.addSubMenu ("Presets", presets);
    }
    else
    {
        juce::PopupMenu modes;
        const auto modeNames = ChaosGrainProcessor::modeNames();
        const int current = (int) proc.apvts.getRawParameterValue ("mode")->load();
        for (int i = 0; i < modeNames.size(); ++i)
            modes.addItem (modeNames[i], true, i == current, [this, i]
            {
                auto* mode = proc.apvts.getParameter ("mode");
                mode->setValueNotifyingHost (mode->convertTo0to1 ((float) i));
            });
        m.addSubMenu ("Mode", modes);
    }
    m.addItem ("Randomize", [this] { proc.randomize(); });
    m.addSeparator();
    m.addItem (kIsFx ? "Freeze" : "Hold (drone)", true, hold, [this, hold, holdId] { proc.apvts.getParameter (holdId)->setValueNotifyingHost (hold ? 0.0f : 1.0f); });
    m.addItem ("Tempo sync", true, sync, [this, sync] { proc.apvts.getParameter ("sync")->setValueNotifyingHost (sync ? 0.0f : 1.0f); });
    const bool lock = proc.apvts.getRawParameterValue ("lock")->load() > 0.5f;
    m.addItem ("Lock pattern", true, lock, [this, lock] { proc.apvts.getParameter ("lock")->setValueNotifyingHost (lock ? 0.0f : 1.0f); });
    if (kIsFx)
    {
        const bool follow = proc.apvts.getRawParameterValue ("follow")->load() > 0.5f;
        m.addItem ("Follow (bursts on notes)", true, follow, [this, follow] { proc.apvts.getParameter ("follow")->setValueNotifyingHost (follow ? 0.0f : 1.0f); });
    }

    m.addSeparator();
    m.addItem ("Large UI", true, scale == 4, [this] { setScale (4); });
    m.addItem ("Small UI", true, scale == 3, [this] { setScale (3); });

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

bool UnitView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (kIsFx) return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void UnitView::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    for (auto& f : files)
        if (proc.loadSample (juce::File (f)))
            break;
}
