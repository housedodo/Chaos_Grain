#pragma once

// Tiny "PS1-era" graphics toolkit: low-res rendering, ordered dithering,
// 15-bit colour, crisp pixel text and a flat-shaded triangle mesh renderer
// with vertex snapping (the classic wobbly look).

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace retro
{
namespace pal
{
    const juce::Colour bgTop    { 0xff1d1450 };
    const juce::Colour bgBottom { 0xff05040f };
    const juce::Colour panel    { 0xff2b2566 };
    const juce::Colour panelHi  { 0xff7b70d8 };
    const juce::Colour panelLo  { 0xff0c0a24 };
    const juce::Colour inset    { 0xff0a0820 };
    const juce::Colour magenta  { 0xffff4f9a };
    const juce::Colour cyan     { 0xff3ef0d0 };
    const juce::Colour yellow   { 0xffffd166 };
    const juce::Colour text     { 0xfff2ecff };
    const juce::Colour dim      { 0xff9a90d8 };
}

constexpr int px = 3; // screen pixels per "retro" pixel

// PS1 had 15-bit colour (5 bits per channel).
inline juce::Colour to15bit (juce::Colour c)
{
    auto q = [] (juce::uint8 v) { return (juce::uint8) ((v & 0xF8) | (v >> 5)); };
    return juce::Colour (q (c.getRed()), q (c.getGreen()), q (c.getBlue()), c.getAlpha());
}

inline float bayer4 (int x, int y)
{
    static const int m[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    return ((float) m[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
}

// Posterised vertical gradient with ordered dithering between the bands.
inline void ditherGradient (juce::Image& img, juce::Rectangle<int> r, juce::Colour top, juce::Colour bottom, int steps = 6)
{
    r = r.getIntersection (img.getBounds());
    if (r.isEmpty()) return;
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);

    for (int y = r.getY(); y < r.getBottom(); ++y)
    {
        const float t = (float) (y - r.getY()) / (float) juce::jmax (1, r.getHeight() - 1);
        const float s = t * (float) (steps - 1);
        const int lo = (int) std::floor (s);
        const float frac = s - (float) lo;

        for (int x = r.getX(); x < r.getRight(); ++x)
        {
            const int idx = juce::jmin (steps - 1, lo + (frac > bayer4 (x, y) ? 1 : 0));
            bd.setPixelColour (x, y, to15bit (top.interpolatedWith (bottom, (float) idx / (float) (steps - 1))));
        }
    }
}

// Hard alpha threshold: removes anti-aliasing so pixels stay chunky.
inline void crispen (juce::Image& img, float threshold = 0.45f)
{
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            bd.setPixelColour (x, y, c.getFloatAlpha() >= threshold ? to15bit (c.withAlpha (1.0f)) : juce::Colours::transparentBlack);
        }
}

inline void blitLowRes (juce::Graphics& g, const juce::Image& img, int x, int y, int scale)
{
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (img, x, y, img.getWidth() * scale, img.getHeight() * scale, 0, 0, img.getWidth(), img.getHeight());
}

// Hand-made 5x7 bitmap font (uppercase, digits, common symbols).
inline const juce::uint8* glyph5x7 (juce::juce_wchar ch)
{
    struct G { char c; juce::uint8 rows[7]; };
    static const G font[] = {
        { 'A', { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 } }, { 'B', { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E } },
        { 'C', { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E } }, { 'D', { 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E } },
        { 'E', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F } }, { 'F', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 } },
        { 'G', { 0x0E,0x11,0x10,0x17,0x11,0x11,0x0F } }, { 'H', { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 } },
        { 'I', { 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E } }, { 'J', { 0x07,0x02,0x02,0x02,0x02,0x12,0x0C } },
        { 'K', { 0x11,0x12,0x14,0x18,0x14,0x12,0x11 } }, { 'L', { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F } },
        { 'M', { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 } }, { 'N', { 0x11,0x11,0x19,0x15,0x13,0x11,0x11 } },
        { 'O', { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E } }, { 'P', { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 } },
        { 'Q', { 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D } }, { 'R', { 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 } },
        { 'S', { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E } }, { 'T', { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 } },
        { 'U', { 0x11,0x11,0x11,0x11,0x11,0x11,0x0E } }, { 'V', { 0x11,0x11,0x11,0x11,0x11,0x0A,0x04 } },
        { 'W', { 0x11,0x11,0x11,0x15,0x15,0x15,0x0A } }, { 'X', { 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11 } },
        { 'Y', { 0x11,0x11,0x0A,0x04,0x04,0x04,0x04 } }, { 'Z', { 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F } },
        { '0', { 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E } }, { '1', { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E } },
        { '2', { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F } }, { '3', { 0x1F,0x02,0x04,0x02,0x01,0x11,0x0E } },
        { '4', { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 } }, { '5', { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E } },
        { '6', { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E } }, { '7', { 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 } },
        { '8', { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E } }, { '9', { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C } },
        { ' ', { 0,0,0,0,0,0,0 } },                       { '.', { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C } },
        { ',', { 0x00,0x00,0x00,0x00,0x0C,0x04,0x08 } }, { '%', { 0x18,0x19,0x02,0x04,0x08,0x13,0x03 } },
        { '/', { 0x00,0x01,0x02,0x04,0x08,0x10,0x00 } }, { '+', { 0x00,0x04,0x04,0x1F,0x04,0x04,0x00 } },
        { '-', { 0x00,0x00,0x00,0x1F,0x00,0x00,0x00 } }, { ':', { 0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00 } },
        { '>', { 0x08,0x04,0x02,0x01,0x02,0x04,0x08 } }, { '<', { 0x02,0x04,0x08,0x10,0x08,0x04,0x02 } },
        { '(', { 0x02,0x04,0x08,0x08,0x08,0x04,0x02 } }, { ')', { 0x08,0x04,0x02,0x02,0x02,0x04,0x08 } },
        { '_', { 0x00,0x00,0x00,0x00,0x00,0x00,0x1F } }, { '\'', { 0x0C,0x04,0x08,0x00,0x00,0x00,0x00 } },
        { '!', { 0x04,0x04,0x04,0x04,0x04,0x00,0x04 } }, { '?', { 0x0E,0x11,0x01,0x02,0x04,0x00,0x04 } },
        { '#', { 0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A } }, { '*', { 0x00,0x04,0x15,0x0E,0x15,0x04,0x00 } },
        { '=', { 0x00,0x00,0x1F,0x00,0x1F,0x00,0x00 } }, { '[', { 0x0E,0x08,0x08,0x08,0x08,0x08,0x0E } },
        { ']', { 0x0E,0x02,0x02,0x02,0x02,0x02,0x0E } }, { '&', { 0x0C,0x12,0x14,0x08,0x15,0x12,0x0D } },
    };
    const auto up = juce::CharacterFunctions::toUpperCase (ch);
    for (auto& g : font)
        if ((juce::juce_wchar) g.c == up) return g.rows;
    for (auto& g : font)
        if (g.c == '?') return g.rows;
    return nullptr;
}

// Crisp pixel-font text. `scale` = screen pixels per font pixel.
inline void pixelText (juce::Graphics& g, juce::String text, juce::Rectangle<int> area, juce::Colour colour,
                       juce::Justification just, int scale = 2, bool shadow = true)
{
    const int adv = 6 * scale;
    const int maxChars = juce::jmax (0, (area.getWidth() + scale) / adv);
    if (text.length() > maxChars)
        text = maxChars > 1 ? text.substring (0, maxChars - 1) + "." : text.substring (0, maxChars);
    if (text.isEmpty()) return;

    const int tw = text.length() * adv - scale, th = 7 * scale;
    int x = area.getX(), y = area.getY();
    if (just.testFlags (juce::Justification::horizontallyCentred)) x += (area.getWidth() - tw) / 2;
    else if (just.testFlags (juce::Justification::right))          x += area.getWidth() - tw;
    if (just.testFlags (juce::Justification::verticallyCentred))   y += (area.getHeight() - th) / 2;
    else if (just.testFlags (juce::Justification::bottom))         y += area.getHeight() - th;

    auto draw = [&] (int ox, int oy, juce::Colour c)
    {
        g.setColour (c);
        for (int i = 0; i < text.length(); ++i)
            if (auto* rows = glyph5x7 (text[i]))
                for (int r = 0; r < 7; ++r)
                    for (int b = 0; b < 5; ++b)
                        if (rows[r] & (0x10 >> b))
                            g.fillRect (ox + i * adv + b * scale, oy + r * scale, scale, scale);
    };

    if (shadow) draw (x + scale, y + scale, pal::panelLo);
    draw (x, y, colour);
}

// Chunky bevelled box in the style of late-90s console menus (low-res coordinates).
inline void bevelBox (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour face, bool raised = true, int edge = 1)
{
    const auto hi = raised ? face.brighter (0.9f) : pal::panelLo;
    const auto lo = raised ? pal::panelLo : face.brighter (0.6f);
    g.setColour (pal::panelLo);
    g.fillRect (r);
    r = r.reduced (1);
    g.setColour (lo);
    g.fillRect (r);
    g.setColour (hi);
    g.fillRect (r.withTrimmedRight (edge).withTrimmedBottom (edge));
    g.setColour (to15bit (face));
    g.fillRect (r.reduced (edge));
}

//==============================================================================
struct Vec3 { float x = 0, y = 0, z = 0; };
inline Vec3 operator+ (Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator- (Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator* (Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float dot (Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross (Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline Vec3 normalise (Vec3 a) { const float l = std::sqrt (dot (a, a)); return l > 0 ? a * (1.0f / l) : a; }
inline Vec3 rotX (Vec3 p, float a) { const float c = std::cos (a), s = std::sin (a); return { p.x, p.y * c - p.z * s, p.y * s + p.z * c }; }
inline Vec3 rotY (Vec3 p, float a) { const float c = std::cos (a), s = std::sin (a); return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c }; }
inline Vec3 rotZ (Vec3 p, float a) { const float c = std::cos (a), s = std::sin (a); return { p.x * c - p.y * s, p.x * s + p.y * c, p.z }; }

struct Mesh
{
    struct Face { int a, b, c; juce::Colour colour; };
    std::vector<Vec3> verts;
    std::vector<Face> faces;
};

struct Camera
{
    float cx, cy;       // screen centre (low-res pixels)
    float scale;        // world unit -> pixels
    float dist = 5.0f;  // camera distance (perspective strength)
};

// Flat-shaded, painter-sorted, vertex-snapped triangle rendering. No z-buffer,
// no perspective correction, integer vertices: authentically wobbly.
template <typename Transform>
void drawMesh (juce::Graphics& g, const Mesh& m, Transform&& xf, const Camera& cam,
               Vec3 light = normalise ({ -0.4f, 0.6f, -0.7f }), float ambient = 0.3f)
{
    std::vector<Vec3> t;
    t.reserve (m.verts.size());
    Vec3 centre;
    for (auto& v : m.verts) { t.push_back (xf (v)); centre = centre + t.back(); }
    centre = centre * (1.0f / (float) juce::jmax<size_t> (1, t.size()));

    auto project = [&cam] (Vec3 p)
    {
        const float f = cam.dist / juce::jmax (0.1f, cam.dist + p.z);
        return juce::Point<float> (std::round (cam.cx + p.x * cam.scale * f), std::round (cam.cy - p.y * cam.scale * f));
    };

    struct Draw { float depth; juce::Point<float> a, b, c; juce::Colour col; };
    std::vector<Draw> list;
    const Vec3 camPos { 0, 0, -cam.dist };

    for (auto& f : m.faces)
    {
        const Vec3 a = t[(size_t) f.a], b = t[(size_t) f.b], c = t[(size_t) f.c];
        const Vec3 mid = (a + b + c) * (1.0f / 3.0f);
        Vec3 n = normalise (cross (b - a, c - a));
        if (dot (n, mid - centre) < 0) n = n * -1.0f;   // make normals point outward
        if (dot (n, mid - camPos) >= 0) continue;        // back-face cull

        const float shade = ambient + (1.0f - ambient) * juce::jmax (0.0f, dot (n, light));
        const auto col = juce::Colour::fromFloatRGBA (f.colour.getFloatRed() * shade, f.colour.getFloatGreen() * shade,
                                                      f.colour.getFloatBlue() * shade, 1.0f);
        list.push_back ({ mid.z, project (a), project (b), project (c), to15bit (col) });
    }

    std::sort (list.begin(), list.end(), [] (const Draw& l, const Draw& r) { return l.depth > r.depth; });

    for (auto& d : list)
    {
        juce::Path p;
        p.addTriangle (d.a, d.b, d.c);
        g.setColour (d.col);
        g.fillPath (p);
    }
}

// Knob: faceted cylinder with a shallow cone on top, axis pointing at the viewer (-z).
inline const Mesh& knobMesh (juce::Colour cap, juce::Colour side, juce::Colour pointer)
{
    static std::vector<std::pair<juce::uint32, Mesh>> cache;
    const auto key = cap.getARGB() ^ (side.getARGB() * 31u) ^ (pointer.getARGB() * 131u);
    for (auto& e : cache) if (e.first == key) return e.second;

    Mesh m;
    const int n = 10;
    const float h = 0.55f;
    const float step = juce::MathConstants<float>::twoPi / (float) n;
    const float start = juce::MathConstants<float>::halfPi - step * 0.5f; // facet 0 centred at 12 o'clock

    for (int i = 0; i < n; ++i)
    {
        const float a = start + step * (float) i;
        m.verts.push_back ({ std::cos (a), std::sin (a), -h });               // top ring
        m.verts.push_back ({ std::cos (a) * 1.12f, std::sin (a) * 1.12f, 0 }); // bottom ring (flared)
    }
    const int apex = (int) m.verts.size();
    m.verts.push_back ({ 0, 0, -h - 0.35f });

    for (int i = 0; i < n; ++i)
    {
        const int t0 = i * 2, b0 = i * 2 + 1, t1 = ((i + 1) % n) * 2, b1 = ((i + 1) % n) * 2 + 1;
        const auto capCol = i == 0 ? pointer : cap;
        const auto sideCol = i == 0 ? pointer.darker (0.3f) : side;
        m.faces.push_back ({ apex, t0, t1, capCol });
        m.faces.push_back ({ t0, b0, b1, sideCol });
        m.faces.push_back ({ t0, b1, t1, sideCol });
    }

    cache.emplace_back (key, std::move (m));
    return cache.back().second;
}

inline const Mesh& cubeMesh()
{
    static const Mesh m = []
    {
        Mesh c;
        for (int i = 0; i < 8; ++i)
            c.verts.push_back ({ (i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f });
        const int q[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
        for (auto& f : q)
        {
            c.faces.push_back ({ f[0], f[1], f[2], juce::Colours::white });
            c.faces.push_back ({ f[0], f[2], f[3], juce::Colours::white });
        }
        return c;
    }();
    return m;
}

// Two-tone faceted gem (logo).
inline const Mesh& gemMesh()
{
    static const Mesh m = []
    {
        Mesh g;
        const int n = 6;
        g.verts.push_back ({ 0, 1.5f, 0 });   // top
        g.verts.push_back ({ 0, -1.5f, 0 });  // bottom
        for (int i = 0; i < n; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) n;
            g.verts.push_back ({ std::cos (a), 0.15f, std::sin (a) });
        }
        for (int i = 0; i < n; ++i)
        {
            const int a = 2 + i, b = 2 + (i + 1) % n;
            g.faces.push_back ({ 0, a, b, (i % 2) ? pal::magenta : pal::cyan });
            g.faces.push_back ({ 1, a, b, (i % 2) ? pal::cyan.darker (0.2f) : pal::yellow });
        }
        return g;
    }();
    return m;
}

inline Mesh tinted (const Mesh& src, juce::Colour c)
{
    Mesh m = src;
    for (auto& f : m.faces) f.colour = c;
    return m;
}
} // namespace retro
