#include "HoistEditor.h"
#include "../RetroGfx.h"

namespace
{
    namespace col
    {
        const juce::Colour casing { 0xff8e9ea0 };   // weathered sea-grey
        const juce::Colour caseDk { 0xff3f4a4c };
        const juce::Colour panel  { 0xff7f8f91 };
        const juce::Colour label  { 0xff1f2a2c };
        const juce::Colour red    { 0xffc8323c };
        const juce::Colour teal   { 0xff1f7a80 };
        const juce::Colour brass  { 0xffc9a24a };
        const juce::Colour knob   { 0xffdcd8cc };
        const juce::Colour black  { 0xff121414 };
        const juce::Colour lamp   { 0xffffc85a };
        const juce::Colour lampOff{ 0xff3a3426 };
        const juce::Colour led    { 0xff7dff4f };
        const juce::Colour ledOff { 0xff2b4a22 };
        const juce::Colour text   { 0xffe8f4f0 };
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

    struct KnobSpec { const char* id; const char* name; float cx, cy, r; juce::Colour cap; };
    const KnobSpec knobs[3] = { { "tide", "TIDE", 24.0f, 132.0f, 9.0f, col::teal },
                                { "dread", "DREAD", 64.0f, 133.0f, 15.0f, col::red },
                                { "mix", "MIX", 104.0f, 132.0f, 9.0f, col::brass } };
    const juce::Rectangle<int> anchorRect { 8, 7, 12, 8 };
    constexpr int crtX = 6, crtY = 22, crtW = 116, crtH = 66;
    constexpr int scrX = crtX + 5, scrY = crtY + 5, scrW = crtW - 10, scrH = crtH - 10;

    // sky / sea colours along the DREAD journey (5 key frames)
    juce::Colour along (const juce::Colour* k, float d)
    {
        const float x = juce::jlimit (0.0f, 1.0f, d) * 4.0f;
        const int i = juce::jmin (3, (int) x);
        return mixc (k[i], k[i + 1], x - (float) i);
    }
    const juce::Colour skyTop[5]  = { juce::Colour (0xff2b1b5e), juce::Colour (0xff4a5260), juce::Colour (0xff2c3826), juce::Colour (0xff0b1028), juce::Colour (0xff030304) };
    const juce::Colour skyLow[5]  = { juce::Colour (0xffff7a59), juce::Colour (0xff9aa0a6), juce::Colour (0xff6f7d4a), juce::Colour (0xff1f2a4a), juce::Colour (0xff0d0707) };
    const juce::Colour seaTop[5]  = { juce::Colour (0xff1e8a9a), juce::Colour (0xff4f6e78), juce::Colour (0xff3a5144), juce::Colour (0xff0e1d33), juce::Colour (0xff050608) };
    const juce::Colour seaLow[5]  = { juce::Colour (0xff0b3f5c), juce::Colour (0xff2a3c46), juce::Colour (0xff1c2a22), juce::Colour (0xff060c18), juce::Colour (0xff020203) };
    const juce::Colour crest[5]   = { juce::Colour (0xffb8fff4), juce::Colour (0xffc9d3d6), juce::Colour (0xffa8b884), juce::Colour (0xff5d7aa8), juce::Colour (0xff2a1414) };
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
    g.fillAll (juce::Colour (0xff141618));
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
    const float sway = proc.dsp.uiSway.load(), pump = proc.dsp.uiPump.load(), wobble = proc.dsp.uiWobble.load();
    const float level = proc.dsp.uiLevel.load();

    // --- casing ---
    c.bevel (0, 0, (float) lowW, (float) lowH, col::casing, 5, true, 2);
    for (int j = 4; j < lowH - 4; ++j)
        for (int i = 4; i < lowW - 4; ++i)
            if ((float) j / (float) lowH > 0.55f + retro::bayer4 (i, j) * 0.6f) c.px (i, j, shade (col::casing, 0.94f));

    // --- ANCHOR button + LED + title ---
    c.bevel ((float) anchorRect.getX(), (float) anchorRect.getY(), 12, 8, shade (col::casing, 0.75f), 1, false);
    c.bevel ((float) anchorRect.getX() + 2, (float) anchorRect.getY() + (anchored ? 3 : 2), 8, 4,
             anchored ? shade (col::brass, 1.25f) : col::brass, 0, ! anchored);
    c.rect (23, 9, 5, 4, col::black);
    c.rect (24, 10, 3, 2, level > 0.01f ? col::led : col::ledOff);
    c.small ("ANCHOR", 14, 17, col::label, true);
    c.text ("HOIST", 96, 6, col::label, true);
    c.small ("TIDE + DREAD", 96, 15, col::caseDk, true);

    // --- screen: the sea ---
    c.bevel (crtX, crtY, crtW, crtH, juce::Colour (0xff6d7a7c), 4, false, 2);
    c.chamfer (scrX - 1, scrY - 1, scrW + 2, scrH + 2, 4, col::black);
    const int horizon = scrY + 26, footY = scrY + scrH - 8;
    const float storm = juce::jlimit (0.0f, 1.0f, (tide - 0.6f) / 0.3f);
    const bool flash = storm > 0.3f && wobble > 0.92f && std::fmod (t * 7.0f, 1.0f) < 0.25f;
    for (int j = scrY; j < horizon; ++j)
    {
        const float f = (float) (j - scrY) / (float) (horizon - scrY);
        for (int i = scrX; i < scrX + scrW; ++i)
        {
            auto colr = mixc (along (skyTop, dread), along (skyLow, dread), f + (retro::bayer4 (i, j) - 0.5f) * 0.25f);
            if (flash) colr = colr.brighter (0.8f);
            c.px (i, j, retro::to15bit (colr));
        }
    }
    // synthwave sun sinking with DREAD
    if (dread < 0.45f)
    {
        const float sunY = (float) horizon - 9.0f + dread * 30.0f, cx = scrX + scrW * 0.5f;
        for (int dy = -9; dy <= 0; ++dy)
            for (int dx = -9; dx <= 9; ++dx)
            {
                const int y = (int) sunY + dy;
                if (y >= horizon || dx * dx + dy * dy > 81) continue;
                if (dy > -5 && ((-dy) % 2 == 0)) continue;   // stripes
                c.px ((int) cx + dx, y, mixc (juce::Colour (0xffffe066), juce::Colour (0xffff4f8a), (float) (dy + 9) / 9.0f));
            }
    }
    // fog in the middle of the journey
    if (dread > 0.35f && dread < 0.8f)
    {
        const float fog = 1.0f - std::abs (dread - 0.55f) / 0.22f;
        for (int j = scrY + 8; j < horizon + 4; ++j)
            for (int i = scrX; i < scrX + scrW; ++i)
                if (retro::bayer4 (i + (int) (t * 3.0f), j) < fog * 0.45f) c.px (i, j, juce::Colour (0xff9fae8a));
    }
    // moon at night
    if (dread > 0.6f && dread < 0.9f)
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx)
                if (dx * dx + dy * dy <= 9 && (dx + 1) * (dx + 1) + dy * dy > 5) c.px (scrX + scrW - 16 + dx, scrY + 8 + dy, juce::Colour (0xffd8e0f0));
    // lightning bolt in the storm
    if (flash)
    {
        float x = scrX + 20.0f + std::fmod (t * 37.0f, 60.0f);
        for (int y = scrY + 1; y < horizon; y += 3) { const float nx = x + (std::fmod ((float) y * 7.3f, 5.0f) - 2.5f); c.line (x, (float) y, nx, (float) y + 3, juce::Colours::white); x = nx; }
    }

    // sea + waves
    const float amp = 0.6f + 2.6f * juce::jmin (1.0f, tide * 1.6f) + 1.5f * storm;
    for (int j = horizon; j < footY; ++j)
    {
        const float f = (float) (j - horizon) / (float) (footY - horizon);
        for (int i = scrX; i < scrX + scrW; ++i)
            c.px (i, j, retro::to15bit (mixc (along (seaTop, dread), along (seaLow, dread), f + (retro::bayer4 (i, j) - 0.5f) * 0.2f)));
    }
    for (int row = 0; row < 4; ++row)
    {
        const float y0 = (float) horizon + 2.0f + (float) row * 6.0f;
        const float a = amp * (0.4f + 0.2f * (float) row);
        const float speed = 1.2f + 2.5f * storm;
        for (int i = scrX; i < scrX + scrW; ++i)
        {
            const float ph = (float) i * (0.18f + 0.04f * (float) row) - t * speed * (1.0f + 0.3f * (float) row) + sway * 1.5f;
            float y = y0 + a * std::sin (ph);
            if (storm > 0.0f) y += storm * 1.2f * std::sin ((float) i * 0.9f + t * 9.0f);
            if ((int) y < footY && (int) y > horizon) c.px (i, (int) y, along (crest, dread));
        }
    }
    // eyes in the abyss
    if (dread > 0.88f && std::fmod (t, 4.0f) < 3.6f)
    {
        const juce::Colour eye (0xffff2a2a);
        c.rect (scrX + 22, horizon + 14, 2, 1, eye); c.rect (scrX + 27, horizon + 14, 2, 1, eye);
    }

    // the boat
    {
        const float bob = (anchored ? 0.3f : 1.0f) * (sway * amp * 0.7f) - (1.0f - pump) * 3.0f;
        const float bx = scrX + scrW * 0.5f, by = (float) horizon + 3.0f + bob;
        const float tilt = (anchored ? 0.3f : 1.0f) * sway * 2.0f;
        const juce::Colour hull = mixc (juce::Colour (0xff7a4a2a), juce::Colour (0xff201410), dread);
        c.poly ({ { bx - 10, by - 1 + tilt }, { bx + 10, by - 1 - tilt }, { bx + 7, by + 3 - tilt }, { bx - 7, by + 3 + tilt } }, hull);
        c.line (bx, by - 1, bx, by - 15, juce::Colour (0xff3a2a1a));
        const juce::Colour sail = mixc (juce::Colour (0xfff4efe0), juce::Colour (0xff30302c), juce::jlimit (0.0f, 1.0f, (dread - 0.3f) * 1.4f));
        const float billow = 2.0f + 2.0f * sway + 2.0f * storm * std::sin (t * 8.0f);
        c.poly ({ { bx + 1, by - 14 }, { bx + 1, by - 3 }, { bx + 8 + billow, by - 4 } }, sail);
        if (dread > 0.55f) for (int k = 0; k < 3; ++k) c.px ((int) (bx + 4 + k * 2), (int) (by - 6 - k), col::black);   // tattered
        if (anchored)
        {
            const int ax = (int) bx - 6;
            c.line ((float) ax, by + 2, (float) ax, (float) footY - 4, juce::Colour (0xffb0b0a8));
            c.rect (ax - 2, footY - 4, 5, 1, juce::Colour (0xffd0d0c8));
            c.px (ax - 2, footY - 5, juce::Colour (0xffd0d0c8)); c.px (ax + 2, footY - 5, juce::Colour (0xffd0d0c8));
        }
    }

    // footer: stage + tide, or a knob read-out
    c.rect (scrX, footY, scrW, 8, juce::Colour (0xff081010));
    juce::String left, right;
    const int stage = juce::jlimit (0, 4, (int) std::floor (dread * 4.999f));
    if (touched >= 0 && nowMs - touchedMs < 1500.0)
        left = juce::String (knobs[touched].name) + " " + juce::String (juce::roundToInt (get (knobs[touched].id) * 100.0f)) + "%";
    else left = HoistProcessor::stageNames()[stage];
    if (anchored) right = std::fmod (nowMs, 1000.0) < 650.0 ? "ANCHORED" : "";
    else right = tide < 0.3f ? "CALM" : tide < 0.6f ? "SWELL" : tide < 0.85f ? "CHOPPY" : "STORM";
    c.small (left, (float) scrX + 2, footY + 2, juce::Colour (0xff9fe8dc));
    c.small (right, (float) (scrX + scrW - 2 - right.length() * 4), footY + 2, anchored ? col::lamp : juce::Colour (0xffd8f8f0));
    if (proc.uiTsDen.load() == 8) c.small (juce::String (proc.uiTsNum.load()) + "/8", (float) scrX + 2, scrY + 2, juce::Colour (0xffd8f8f0));

    // --- DREAD stage lamps (the five stages) ---
    c.bevel (6, 91, 116, 21, col::panel, 2, false);
    c.small ("DREAD", 20, 99, col::label, true);
    for (int k = 0; k < 5; ++k)
    {
        const float w = juce::jlimit (0.0f, 1.0f, dread * 4.0f - (float) k + 1.0f);
        const int x = 38 + k * 16;
        c.rect (x - 1, 96, 7, 6, col::black);
        c.rect (x, 97, 5, 4, mixc (col::lampOff, k == 4 ? col::red : col::lamp, w));
        c.small (juce::String (k + 1), (float) x + 1, 104, col::label);
    }

    // --- knobs ---
    c.bevel (6, 114, 116, 50, col::panel, 3, false);
    for (int k = 0; k < 3; ++k)
    {
        const auto& ks = knobs[k];
        drawKnob (c, ks.cx, ks.cy, ks.r, ks.cap, get (ks.id));
        c.text (ks.name, ks.cx, (int) (ks.cy + ks.r + (k == 1 ? 5 : 6)), col::label, true);
    }
    for (auto pt : { juce::Point<int> (3, 3), { lowW - 4, 3 }, { 3, lowH - 4 }, { lowW - 4, lowH - 4 } })
        c.rect (pt.x - 1, pt.y - 1, 3, 3, col::caseDk);
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
        m.addItem ("Anchor (hold)", true, anch, [this, anch] { proc.apvts.getParameter ("anchor")->setValueNotifyingHost (anch ? 0.0f : 1.0f); });
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
    if (anchorRect.expanded (2).contains (p) || juce::Rectangle<int> (scrX, scrY, scrW, scrH).contains (p))
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
