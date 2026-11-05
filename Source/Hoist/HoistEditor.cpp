#include "HoistEditor.h"
#include "../RetroGfx.h"

namespace
{
    // the game's own colours (HUD: cream ink, navy plaques, gold, brass, dark wood, oxblood title)
    namespace col
    {
        const juce::Colour wood    { 0xff3a2210 };
        const juce::Colour woodDk  { 0xff2a1608 };
        const juce::Colour woodHi  { 0xff5a3a1e };
        const juce::Colour brass   { 0xffc9a85c };
        const juce::Colour brassHi { 0xffead8a8 };
        const juce::Colour brassDk { 0xff6a4a26 };
        const juce::Colour gold    { 0xffffd23a };
        const juce::Colour ink     { 0xfffff4d8 };
        const juce::Colour navy    { 0xff17112e };
        const juce::Colour navyEdge{ 0xff8a7f9c };
        const juce::Colour oxblood { 0xff8a1a1a };
        const juce::Colour parch   { 0xfff4ead0 };
        const juce::Colour pink    { 0xffff70d0 };
        const juce::Colour black   { 0xff0b0820 };
        const juce::Colour caseDk  { 0xff1a0f08 };
        const juce::Colour label   { 0xff2a1608 };
        const juce::Colour knob    { 0xffe2cf9a };
    }

    juce::Colour shade (juce::Colour c, float f)
    {
        auto ch = [f] (juce::uint8 v) { return (juce::uint8) juce::jlimit (0, 255, (int) ((float) v * f)); };
        return retro::to15bit (juce::Colour (ch (c.getRed()), ch (c.getGreen()), ch (c.getBlue())));
    }
    juce::Colour mixc (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }

    struct Canvas
    {
        explicit Canvas (juce::Image& i) : bd (i, juce::Image::BitmapData::readWrite), W (i.getWidth()), H (i.getHeight()) {}
        juce::Image::BitmapData bd;
        int W, H;
        void px (int x, int y, juce::Colour c) { if (x >= 0 && y >= 0 && x < W && y < H) bd.setPixelColour (x, y, c); }
        void px (float x, float y, juce::Colour c) { px ((int) std::floor (x), (int) std::floor (y), c); }
        void rect (int x, int y, int w, int h, juce::Colour c) { for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) px (i, j, c); }
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
            for (int i = 0; i < s.length(); ++i)
                if (auto* g = retro::glyph5x7 (s[i]))
                    for (int r = 0; r < 7; ++r)
                        for (int b = 0; b < 5; ++b)
                            if (g[r] & (0x10 >> b)) px ((int) X + i * 6 + b, Y + r, c);
        }
        void small (juce::String s, float X, int Y, juce::Colour c, bool centre = false)
        {
            static const std::pair<char, std::array<juce::uint8, 5>> f3[] = {
                { '0', { 7,5,5,5,7 } }, { '1', { 2,6,2,2,7 } }, { '2', { 7,1,7,4,7 } }, { '3', { 7,1,3,1,7 } }, { '4', { 5,5,7,1,1 } },
                { '5', { 7,4,7,1,7 } }, { '6', { 7,4,7,5,7 } }, { '7', { 7,1,1,2,2 } }, { '8', { 7,5,7,5,7 } }, { '9', { 7,5,7,1,7 } },
                { '.', { 0,0,0,0,2 } }, { '-', { 0,0,7,0,0 } }, { '+', { 0,2,7,2,0 } }, { '%', { 5,1,2,4,5 } }, { '/', { 1,1,2,4,4 } },
                { '\'', { 2,2,0,0,0 } }, { ':', { 0,2,0,2,0 } },
                { 'A', { 2,5,7,5,5 } }, { 'B', { 6,5,6,5,6 } }, { 'C', { 3,4,4,4,3 } }, { 'D', { 6,5,5,5,6 } }, { 'E', { 7,4,6,4,7 } },
                { 'F', { 7,4,6,4,4 } }, { 'G', { 3,4,5,5,3 } }, { 'H', { 5,5,7,5,5 } }, { 'I', { 7,2,2,2,7 } }, { 'J', { 1,1,1,5,2 } },
                { 'K', { 5,5,6,5,5 } }, { 'L', { 4,4,4,4,7 } }, { 'M', { 5,7,7,5,5 } }, { 'N', { 6,5,5,5,5 } }, { 'O', { 2,5,5,5,2 } },
                { 'P', { 6,5,6,4,4 } }, { 'Q', { 2,5,5,6,3 } }, { 'R', { 6,5,6,5,5 } }, { 'S', { 3,4,2,1,6 } }, { 'T', { 7,2,2,2,2 } },
                { 'U', { 5,5,5,5,7 } }, { 'V', { 5,5,5,5,2 } }, { 'W', { 5,5,7,7,5 } }, { 'X', { 5,5,2,5,5 } }, { 'Y', { 5,5,2,2,2 } },
                { 'Z', { 7,1,2,4,7 } }, { ' ', { 0,0,0,0,0 } },
            };
            s = s.toUpperCase();
            if (centre) X -= (float) (s.length() * 4 - 1) / 2.0f;
            for (int i = 0; i < s.length(); ++i)
                for (auto& g : f3)
                    if ((juce::juce_wchar) g.first == s[i])
                    {
                        for (int r = 0; r < 5; ++r)
                            for (int b = 0; b < 3; ++b)
                                if (g.second[(size_t) r] & (4 >> b)) px ((int) X + i * 4 + b, Y + r, c);
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


    const juce::Rectangle<int> anchorRect { 5, 4, 15, 14 };
    constexpr int pcx = 64, pcy = 56, pR = 33, glassR = 28;          // the porthole
    struct KnobSpec { const char* id; const char* name; float cx, cy, r; };
    const KnobSpec knobs[3] = { { "tide", "TIDE", 23.0f, 137.0f, 12.0f },
                                { "dread", "DREAD", 64.0f, 137.0f, 15.0f },
                                { "mix", "MIX", 105.0f, 137.0f, 9.0f } };

    // the game's five dread palettes (sky, sea, sun ...)
    struct Pal { juce::Colour skyTop, skyHor, fog, deep, shallow, sun, cloud; float stars; };
    const Pal pal[5] = {
        { juce::Colour (0xff3a86e0), juce::Colour (0xffbfeaff), juce::Colour (0xffbfeaff), juce::Colour (0xff1b86c6), juce::Colour (0xff4fe0d0), juce::Colour (0xfffff2a8), juce::Colour (0xffffffff), 0.0f },
        { juce::Colour (0xff5490d0), juce::Colour (0xffd6e8e0), juce::Colour (0xffd2e4dc), juce::Colour (0xff1d7a9e), juce::Colour (0xff58c4b8), juce::Colour (0xfffff6c8), juce::Colour (0xfff2f4f0), 0.0f },
        { juce::Colour (0xff5a5f9c), juce::Colour (0xffc4d196), juce::Colour (0xffa9bb86), juce::Colour (0xff1c5a6c), juce::Colour (0xff3f8c78), juce::Colour (0xffe8ffb8), juce::Colour (0xffc9c8b8), 0.0f },
        { juce::Colour (0xff24243e), juce::Colour (0xff6a7c76), juce::Colour (0xff4a5a58), juce::Colour (0xff0e2c3c), juce::Colour (0xff285856), juce::Colour (0xffd4dccf), juce::Colour (0xff59606a), 0.5f },
        { juce::Colour (0xff06030e), juce::Colour (0xff5a1a50), juce::Colour (0xff2a1034), juce::Colour (0xff12123a), juce::Colour (0xff52287c), juce::Colour (0xff000000), juce::Colour (0xff2a1030), 1.0f } };
    Pal palAt (float d)
    {
        const float x = juce::jlimit (0.0f, 1.0f, d) * 4.0f;
        const int i = juce::jmin (3, (int) x);
        const float f = x - (float) i;
        const auto& a = pal[i]; const auto& b = pal[i + 1];
        return { mixc (a.skyTop, b.skyTop, f), mixc (a.skyHor, b.skyHor, f), mixc (a.fog, b.fog, f), mixc (a.deep, b.deep, f),
                 mixc (a.shallow, b.shallow, f), mixc (a.sun, b.sun, f), mixc (a.cloud, b.cloud, f), a.stars + (b.stars - a.stars) * f };
    }
    // lantern colour per stage (from each stage's palette)
    const juce::Colour lanternLit[5] = { juce::Colour (0xffffd23a), juce::Colour (0xfffff2a8), juce::Colour (0xffc4d196),
                                         juce::Colour (0xffbfe0e8), juce::Colour (0xffff70d0) };

    void plaque (Canvas& c, int x, int y, int w, int h)
    {
        c.rect (x, y, w, h, col::navy);
        for (int i = x; i < x + w; ++i) { c.px (i, y, col::navyEdge); c.px (i, y + h - 1, col::navyEdge); }
        for (int j = y; j < y + h; ++j) { c.px (x, j, col::navyEdge); c.px (x + w - 1, j, col::navyEdge); }
        c.px (x + 1, y + h, col::black); for (int i = x + 1; i <= x + w; ++i) c.px (i, y + h, juce::Colour (0x88000000));
    }

    void rivet (Canvas& c, int x, int y) { c.px (x, y, col::brassHi); c.px (x + 1, y, col::brass); c.px (x, y + 1, col::brass); c.px (x + 1, y + 1, col::brassDk); }

    void anchorGlyph (Canvas& c, int x, int y, juce::Colour k)
    {
        c.rect (x + 4, y, 3, 1, k); c.px (x + 4, y + 1, k); c.px (x + 6, y + 1, k); c.rect (x + 4, y + 2, 3, 1, k);   // ring
        c.rect (x + 5, y + 3, 1, 7, k);                                                                             // shank
        c.rect (x + 2, y + 4, 7, 1, k);                                                                             // stock
        c.px (x + 1, y + 7, k); c.px (x + 1, y + 8, k); c.px (x + 2, y + 9, k); c.px (x + 3, y + 10, k); c.px (x + 4, y + 10, k);
        c.px (x + 9, y + 7, k); c.px (x + 9, y + 8, k); c.px (x + 8, y + 9, k); c.px (x + 7, y + 10, k); c.px (x + 6, y + 10, k);
        c.px (x, y + 7, k); c.px (x + 10, y + 7, k);                                                                // flukes
    }

    void lantern (Canvas& c, int x, int y, juce::Colour glow, float lit)
    {
        c.rect (x + 1, y, 3, 1, col::brassDk); c.px (x + 2, y - 1, col::brassDk);              // handle / cap
        c.rect (x, y + 1, 5, 1, col::brass);
        const auto glass = mixc (juce::Colour (0xff2a2018), glow, lit);
        c.rect (x, y + 2, 5, 5, col::brassDk);
        c.rect (x + 1, y + 2, 3, 5, glass);
        if (lit > 0.5f) c.px (x + 2, y + 4, glow.brighter (0.6f));
        c.rect (x, y + 7, 5, 1, col::brass);
        if (lit > 0.3f)                                                                          // halo on the wood
            for (int dy = -2; dy <= 9; ++dy)
                for (int dx = -3; dx <= 7; ++dx)
                    if ((dx < 0 || dx > 4 || dy < 0 || dy > 7) && retro::bayer4 (x + dx, y + dy) < 0.18f * lit)
                        c.px (x + dx, y + dy, mixc (col::wood, glow, 0.35f));
    }

    // TIDE: a little ship's wheel
    void helm (Canvas& c, float cx, float cy, float r, float value)
    {
        const float rot = value * juce::MathConstants<float>::twoPi * 0.75f;
        const juce::Colour rimDk (0xff6a4422), rimLt (0xffb07a44), spoke (0xffc89058);
        for (float a = 0.0f; a < juce::MathConstants<float>::twoPi; a += 0.04f)                  // rim
            for (float rr = r - 2.0f; rr <= r; rr += 0.5f)
                c.px (cx + std::cos (a) * rr, cy + std::sin (a) * rr, std::sin (a - 2.3f) > 0.2f ? rimLt : rimDk);
        for (int k = 0; k < 8; ++k)                                                              // spokes + handles
        {
            const float a = rot + (float) k * juce::MathConstants<float>::pi / 4.0f;
            c.line (cx + std::cos (a) * 3.0f, cy + std::sin (a) * 3.0f, cx + std::cos (a) * (r + 2.0f), cy + std::sin (a) * (r + 2.0f), spoke);
            const float hx = cx + std::cos (a) * (r + 3.0f), hy = cy + std::sin (a) * (r + 3.0f);
            c.px (hx, hy, k == 0 ? col::gold : spoke);
            c.px (hx + 1, hy, k == 0 ? col::gold : rimDk);
        }
        for (int dy = -3; dy <= 3; ++dy)                                                         // brass hub
            for (int dx = -3; dx <= 3; ++dx)
                if (dx * dx + dy * dy <= 9) c.px ((int) cx + dx, (int) cy + dy, dx + dy < 0 ? col::brassHi : col::brass);
        c.px ((int) cx, (int) cy, col::brassDk);
    }

    // DREAD: a brass barometer with the five stages on its face
    void barometer (Canvas& c, float cx, float cy, float r, float value)
    {
        for (int dy = -(int) r - 2; dy <= (int) r + 2; ++dy)
            for (int dx = -(int) r - 2; dx <= (int) r + 2; ++dx)
            {
                const float d = std::sqrt ((float) (dx * dx + dy * dy));
                if (d > r + 2.0f) continue;
                juce::Colour k = d > r ? (dx + dy < 0 ? col::brassHi : col::brassDk) : d > r - 2.0f ? col::brass : col::parch;
                if (d <= r - 2.0f && retro::bayer4 ((int) cx + dx, (int) cy + dy) < 0.12f) k = shade (col::parch, 0.92f);
                c.px ((int) cx + dx, (int) cy + dy, k);
            }
        const float a0 = juce::degreesToRadians (135.0f), span = juce::degreesToRadians (270.0f);
        for (int s = 0; s < 5; ++s)                                                              // coloured stage arc
            for (float f = 0.0f; f < 1.0f; f += 0.08f)
            {
                const float a = a0 + span * ((float) s + f) / 5.0f;
                for (float rr = r - 5.0f; rr <= r - 3.0f; rr += 1.0f)
                    c.px (cx + std::cos (a) * rr, cy + std::sin (a) * rr, s == 4 ? col::pink.darker (0.2f) : mixc (lanternLit[s], col::brassDk, 0.25f));
            }
        for (int s = 0; s <= 5; ++s)                                                             // ticks
        {
            const float a = a0 + span * (float) s / 5.0f;
            c.line (cx + std::cos (a) * (r - 6.0f), cy + std::sin (a) * (r - 6.0f), cx + std::cos (a) * (r - 8.0f), cy + std::sin (a) * (r - 8.0f), col::label);
        }
        const float a = a0 + span * value;                                                       // needle
        c.line (cx, cy, cx + std::cos (a) * (r - 4.0f), cy + std::sin (a) * (r - 4.0f), col::oxblood);
        c.line (cx + 1, cy, cx + 1 + std::cos (a) * (r - 6.0f), cy + std::sin (a) * (r - 6.0f), col::oxblood);
        c.rect ((int) cx - 1, (int) cy - 1, 3, 3, col::brassDk);
        c.px ((int) cx, (int) cy, col::brassHi);
    }
}

HoistEditor::HoistEditor (HoistProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    scale = (int) proc.apvts.state.getProperty ("uiScale", 4);
    if (scale != 3 && scale != 4) scale = 4;
    setSize (lowW * scale, lowH * scale);
    startTimerHz (30);
}

HoistEditor::~HoistEditor() { stopTimer(); }

void HoistEditor::setScale (int s)
{
    scale = s;
    proc.apvts.state.setProperty ("uiScale", s, nullptr);
    setSize (lowW * scale, lowH * scale);
}

void HoistEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::black);
    juce::Image img (juce::Image::ARGB, lowW, lowH, true);
    render (img);
    retro::blitLowRes (g, img, 0, 0, scale);
}

void HoistEditor::render (juce::Image& img)
{
    Canvas c (img);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float t = (float) (nowMs * 0.001);
    auto get = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const float tide = get ("tide"), dread = get ("dread");
    const bool anchored = get ("anchor") > 0.5f;
    const float sway = proc.dsp.uiSway.load(), lift = proc.dsp.uiPump.load(), gust = proc.dsp.uiGust.load();
    const float level = proc.dsp.uiLevel.load();
    const float rough = juce::jlimit (0.0f, 1.0f, (tide - 0.6f) / 0.2f);

    // --- dark wood planks + brass frame ---
    for (int j = 0; j < lowH; ++j)
    {
        const int plank = j / 9;
        const float tone = 0.9f + 0.12f * (float) ((plank * 7) % 3) / 2.0f;
        for (int i = 0; i < lowW; ++i)
        {
            auto k = shade (col::wood, tone);
            if (j % 9 == 0) k = col::woodDk;
            else if (((i + plank * 37) % 61) == 0) k = col::woodDk;                       // butt joints
            else if (retro::bayer4 (i * 3 + plank, j) < 0.1f) k = shade (col::wood, tone * 0.85f);   // grain
            c.px (i, j, k);
        }
    }
    for (int i = 0; i < lowW; ++i) { c.px (i, 0, col::brassDk); c.px (i, 1, col::brass); c.px (i, lowH - 2, col::brass); c.px (i, lowH - 1, col::brassDk); }
    for (int j = 0; j < lowH; ++j) { c.px (0, j, col::brassDk); c.px (1, j, col::brass); c.px (lowW - 2, j, col::brass); c.px (lowW - 1, j, col::brassDk); }
    for (auto pt : { juce::Point<int> (3, 3), { lowW - 5, 3 }, { 3, lowH - 5 }, { lowW - 5, lowH - 5 }, { 3, 90 }, { lowW - 5, 90 } })
        rivet (c, pt.x, pt.y);

    // --- title plaque + anchor + lantern (activity) ---
    plaque (c, 34, 4, 60, 13);
    c.rect (35, 5, 58, 11, juce::Colour (0xff5a1414));        // the game's title colours: cream on oxblood
    c.text ("HOIST", 64, 7, col::ink, true);
    plaque (c, anchorRect.getX(), anchorRect.getY(), anchorRect.getWidth(), anchorRect.getHeight());
    anchorGlyph (c, anchorRect.getX() + 2, anchorRect.getY() + 1, anchored ? col::gold : col::brass.darker (0.3f));
    lantern (c, lowW - 15, 6, col::gold, juce::jlimit (0.0f, 1.0f, level * 8.0f));

    // --- the porthole ---
    const Pal P = palAt (dread);
    const int horizon = pcy - 3;
    const bool rain = rough > 0.2f;
    auto inGlass = [] (int x, int y) { const int dx = x - pcx, dy = y - pcy; return dx * dx + dy * dy <= glassR * glassR; };
    for (int y = pcy - glassR; y <= pcy + glassR; ++y)
        for (int x = pcx - glassR; x <= pcx + glassR; ++x)
        {
            if (! inGlass (x, y)) continue;
            juce::Colour k;
            if (y < horizon)
            {
                const float f = (float) (y - (pcy - glassR)) / (float) (horizon - (pcy - glassR));
                k = mixc (P.skyTop, P.skyHor, f + (retro::bayer4 (x, y) - 0.5f) * 0.25f);
                if (f > 0.75f && retro::bayer4 (x + (int) (t * 2.0f), y) < (f - 0.75f) * 2.0f) k = mixc (k, P.fog, 0.6f);   // haze
            }
            else
            {
                const float f = (float) (y - horizon) / (float) (pcy + glassR - horizon);
                k = mixc (P.shallow, P.deep, 0.35f + 0.65f * f + (retro::bayer4 (x, y) - 0.5f) * 0.25f);
            }
            c.px (x, y, retro::to15bit (k));
        }
    // stars
    if (P.stars > 0.0f)
        for (int s = 0; s < 18; ++s)
        {
            const int sx = pcx - 24 + (s * 37) % 48, sy = pcy - 25 + (s * 23) % 20;
            if (sy < horizon - 2 && inGlass (sx, sy) && (float) ((s * 13) % 10) / 10.0f < P.stars && std::fmod (t + (float) s * 0.37f, 3.0f) > 0.3f)
                c.px (sx, sy, juce::Colour (0xffe8e0ff));
        }
    // sun (sinking with dread) - black with a pink corona in the abyss
    {
        const float sunY = (float) pcy - 18.0f + dread * 10.0f, sunX = (float) pcx + 9.0f;
        const float sr = 4.0f + dread * 2.0f;
        for (int dy = -8; dy <= 8; ++dy)
            for (int dx = -8; dx <= 8; ++dx)
            {
                const float dd = std::sqrt ((float) (dx * dx + dy * dy));
                const int x = (int) sunX + dx, y = (int) sunY + dy;
                if (y >= horizon || ! inGlass (x, y)) continue;
                if (dd <= sr) c.px (x, y, P.sun);
                else if (dread > 0.8f && dd <= sr + 1.5f) c.px (x, y, col::pink);
            }
    }
    // clouds drifting
    for (int k = 0; k < 3; ++k)
    {
        const int cx0 = pcx - 30 + (int) std::fmod (t * (1.5f + (float) k) + (float) k * 23.0f, 70.0f), cy0 = pcy - 22 + k * 5;
        for (int dx = 0; dx < 9 - k; ++dx)
            for (int dy = 0; dy < 2; ++dy)
                if (inGlass (cx0 + dx, cy0 + dy) && cy0 + dy < horizon && ! (dy == 0 && (dx == 0 || dx == 8 - k))) c.px (cx0 + dx, cy0 + dy, P.cloud);
    }
    // waves (calm ripples .. swell .. rough white caps)
    const float amp = 0.4f + 1.6f * juce::jmin (1.0f, tide * 1.5f) + 1.6f * rough;
    for (int row = 0; row < 5; ++row)
    {
        const float y0 = (float) horizon + 2.0f + (float) row * 5.0f;
        for (int x = pcx - glassR; x <= pcx + glassR; ++x)
        {
            const float ph = (float) x * (0.22f + 0.05f * (float) row) - t * (1.0f + 0.4f * (float) row) * (1.0f + 1.5f * rough) + sway;
            float y = y0 + amp * (0.4f + 0.15f * (float) row) * std::sin (ph) + rough * gust * std::sin ((float) x * 0.8f + t * 7.0f);
            const int yi = (int) y;
            if (yi > horizon && inGlass (x, yi)) c.px (x, yi, mixc (P.shallow, juce::Colours::white, 0.45f - 0.2f * dread));
            if (rough > 0.4f && std::sin (ph) > 0.92f && inGlass (x, yi - 1)) c.px (x, yi - 1, juce::Colour (0xfff0f8ff));   // white caps
        }
    }
    // eyes below the surface in the abyss
    if (dread > 0.82f && std::fmod (t, 5.0f) < 4.4f)
    {
        const int ey = horizon + 16;
        c.rect (pcx - 14, ey, 2, 1, col::pink); c.rect (pcx - 9, ey, 2, 1, col::pink);
        c.rect (pcx + 13, ey + 5, 1, 1, col::pink); c.rect (pcx + 17, ey + 5, 1, 1, col::pink);
    }
    // the Merry Gull
    {
        const float rock = anchored ? 0.3f : 1.0f;
        const float bob = rock * sway * amp * 0.8f - (lift - 0.75f) * 6.0f;
        const float bx = (float) pcx - 6.0f, by = (float) horizon + 5.0f + bob;
        const float tilt = rock * sway * 1.8f;
        const juce::Colour hull = mixc (juce::Colour (0xff8a5a30), juce::Colour (0xff2a1608), dread * 0.8f);
        c.poly ({ { bx - 9, by - 2 + tilt }, { bx + 10, by - 2 - tilt }, { bx + 7, by + 2 - tilt }, { bx - 7, by + 2 + tilt } }, hull);
        c.line (bx - 8, by - 2 + tilt, bx + 9, by - 2 - tilt, mixc (juce::Colour (0xffc8a070), hull, 0.5f));
        c.line (bx + 1, by - 2, bx + 1, by - 17, juce::Colour (0xff3a2210));
        const juce::Colour sail = mixc (col::parch, juce::Colour (0xff3a3048), juce::jlimit (0.0f, 1.0f, (dread - 0.3f) * 1.3f));
        const float billow = 1.5f + 1.5f * sway + 2.0f * rough * gust;
        c.poly ({ { bx + 2, by - 16 }, { bx + 2, by - 4 }, { bx + 9 + billow, by - 5 } }, sail);
        c.poly ({ { bx, by - 14 }, { bx, by - 5 }, { bx - 6 - billow * 0.5f, by - 5 } }, shade (sail, 0.9f));
        c.px ((int) bx + 4, (int) by - 9, shade (sail, 0.75f)); c.px ((int) bx + 5, (int) by - 8, shade (sail, 0.75f));   // patches
        const float flap = std::sin (t * (4.0f + 6.0f * rough)) > 0.0f ? 1.0f : 0.0f;
        c.rect ((int) bx + 2, (int) by - 18, 3, 1, juce::Colour (0xffc8402a)); c.px ((int) bx + 5, (int) (by - 18 + flap), juce::Colour (0xffc8402a));   // pennant
        if (anchored)
        {
            const int ax = (int) bx - 6;
            for (int y = (int) by + 2; y < pcy + glassR - 5; ++y) if (inGlass (ax, y) && (y % 2 == 0)) c.px (ax, y, juce::Colour (0xffb0a890));
            anchorGlyph (c, ax - 5, pcy + glassR - 13, juce::Colour (0xffb0a890));
        }
    }
    // rain in rough weather
    if (rain)
        for (int k = 0; k < (int) (30.0f * rough); ++k)
        {
            const int rx = pcx - glassR + (int) std::fmod ((float) k * 17.3f + t * 40.0f, (float) (2 * glassR));
            const int ry = pcy - glassR + (int) std::fmod ((float) k * 29.1f + t * 90.0f, (float) (2 * glassR));
            for (int s = 0; s < 3; ++s) if (inGlass (rx - s, ry + s)) c.px (rx - s, ry + s, juce::Colour (0xffd8e8f0));
        }
    // glass glare + brass ring with bolts
    for (int a = 0; a < 14; ++a)
    {
        const float ang = 3.6f + (float) a * 0.06f;
        c.px ((float) pcx + std::cos (ang) * (float) (glassR - 4), (float) pcy + std::sin (ang) * (float) (glassR - 4), juce::Colour (0x66ffffff));
    }
    for (int y = pcy - pR - 1; y <= pcy + pR + 1; ++y)
        for (int x = pcx - pR - 1; x <= pcx + pR + 1; ++x)
        {
            const int dx = x - pcx, dy = y - pcy;
            const float d = std::sqrt ((float) (dx * dx + dy * dy));
            if (d > (float) glassR && d <= (float) pR + 0.5f)
            {
                const float light = (float) (-dx - dy) / (float) pR;
                c.px (x, y, d > (float) pR - 0.7f || d < (float) glassR + 1.0f ? col::brassDk : light > 0.25f ? col::brassHi : light < -0.35f ? shade (col::brass, 0.7f) : col::brass);
            }
            else if (d > (float) pR + 0.5f && d <= (float) pR + 1.5f) c.px (x, y, col::caseDk);
        }
    for (int b = 0; b < 8; ++b)
    {
        const float ang = (float) b * juce::MathConstants<float>::pi / 4.0f + 0.39f;
        rivet (c, pcx + (int) std::lround (std::cos (ang) * 30.5f), pcy + (int) std::lround (std::sin (ang) * 30.5f));
    }

    // --- read-out plaque (like the game's HUD) ---
    plaque (c, 8, 92, 112, 10);
    juce::String left, right;
    const int stage = juce::jlimit (0, 4, (int) std::floor (dread * 4.999f));
    if (touched >= 0 && nowMs - touchedMs < 1500.0)
        left = juce::String (knobs[touched].name) + " " + juce::String (juce::roundToInt (get (knobs[touched].id) * 100.0f)) + "%";
    else left = HoistProcessor::stageNames()[stage];
    if (anchored) right = std::fmod (nowMs, 1000.0) < 650.0 ? "ANCHORED" : "";
    else right = tide < 0.25f ? "CALM" : tide < 0.6f ? "SWELL" : "ROUGH SEA";
    c.small (left, 11.0f, 95, col::ink);
    c.small (right, (float) (117 - right.length() * 4), 95, col::gold);
    if (proc.uiTsDen.load() == 8) c.small (juce::String (proc.uiTsNum.load()) + "/8", (float) (pcx - 3), pcy + glassR - 7, col::ink);

    // --- five ship's lanterns: the DREAD stages ---
    c.small ("DREAD", 9.0f, 108, col::brassHi);
    for (int k = 0; k < 5; ++k)
    {
        const float lit = juce::jlimit (0.0f, 1.0f, dread * 4.0f - (float) k + 1.0f);
        lantern (c, 36 + k * 17, 104, lanternLit[k], lit * (k == stage ? 1.0f : 0.75f));
    }

    // --- controls: helm (TIDE), barometer (DREAD), brass knob (MIX) ---
    helm (c, knobs[0].cx, knobs[0].cy, knobs[0].r, tide);
    barometer (c, knobs[1].cx, knobs[1].cy, knobs[1].r, dread);
    drawKnob (c, knobs[2].cx, knobs[2].cy, knobs[2].r, col::brass, get ("mix"));
    for (int k = 0; k < 3; ++k)
    {
        const int w = (int) std::strlen (knobs[k].name) * 4 + 5;
        plaque (c, (int) knobs[k].cx - w / 2, 156, w, 9);
        c.small (knobs[k].name, knobs[k].cx, 158, col::ink, true);
    }
}

int HoistEditor::knobAt (juce::Point<int> p) const
{
    for (int k = 0; k < 3; ++k)
    {
        const float dx = (float) p.x - knobs[k].cx, dy = (float) p.y - knobs[k].cy;
        if (dx * dx + dy * dy <= (knobs[k].r + 4.0f) * (knobs[k].r + 4.0f)) return k;
    }
    return -1;
}

void HoistEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        const bool anch = proc.apvts.getRawParameterValue ("anchor")->load() > 0.5f;
        m.addItem ("Drop anchor (hold)", true, anch, [this, anch] { proc.apvts.getParameter ("anchor")->setValueNotifyingHost (anch ? 0.0f : 1.0f); });
        m.addSeparator();
        m.addItem ("Large UI", true, scale == 4, [this] { setScale (4); });
        m.addItem ("Small UI", true, scale == 3, [this] { setScale (3); });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }
    const auto p = e.getPosition() / scale;
    dragKnob = knobAt (p);
    if (dragKnob >= 0)
    {
        auto* prm = proc.apvts.getParameter (knobs[dragKnob].id);
        prm->beginChangeGesture();
        dragStartValue = prm->getValue();
        dragStartY = e.getPosition().y;
        touched = dragKnob; touchedMs = juce::Time::getMillisecondCounterHiRes();
        return;
    }
    const int dx = p.x - pcx, dy = p.y - pcy;
    if (anchorRect.expanded (2).contains (p) || dx * dx + dy * dy <= pR * pR)
    {
        auto* a = proc.apvts.getParameter ("anchor");
        a->setValueNotifyingHost (a->getValue() > 0.5f ? 0.0f : 1.0f);
    }
}

void HoistEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKnob < 0) return;
    auto* prm = proc.apvts.getParameter (knobs[dragKnob].id);
    const float sens = e.mods.isShiftDown() ? 0.0008f : 0.004f;
    prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartValue + (float) (dragStartY - e.getPosition().y) * sens / (float) scale * 4.0f));
    touched = dragKnob; touchedMs = juce::Time::getMillisecondCounterHiRes();
}

void HoistEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragKnob >= 0) proc.apvts.getParameter (knobs[dragKnob].id)->endChangeGesture();
    dragKnob = -1;
}

void HoistEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = knobAt (e.getPosition() / scale);
    if (k < 0) return;
    auto* prm = proc.apvts.getParameter (knobs[k].id);
    prm->setValueNotifyingHost (prm->getDefaultValue());
}
