// Drives two builds of rug_native.dll with the same scripted input.
//   exact: every frame must be bit-identical (pixels, pose, bounds, corners, hit tests). Run it against a
//          candidate built with RUG_EXACT_STEPS, which keeps every skip except the sleep threshold.
//   sleep: the cloth rests on icon bumps for 50 s; reports how far the sleeping candidate's pixels are
//          from the reference, next to how much the reference's own consecutive frames differ.
// Usage: equivalence.exe exact|sleep <reference dll> <candidate dll> <rug picture> <second rug picture>
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1200;
constexpr float kFrameSeconds = 1.0f / 32.0f;

struct Api {
    HMODULE module = nullptr;
    bool renderReportsChange = false;
    int (*init)(int, int);
    int (*load_texture)(const wchar_t*);
    void (*rebuild)(float, float);
    void (*set_bumps)(const float*, int);
    void (*step)(float);
    void* render;
    int (*mouse_down)(float, float);
    void (*mouse_drag)(float, float);
    void (*mouse_up)();
    int (*hit_test)(float, float);
    void (*rotate)(float);
    void (*resize)(float);
    void (*get_pose)(float*);
    void (*get_corners)(float*);
    void (*get_bounds)(float*);
    void (*hold_begin)();
    void (*hold_end)();
    void (*layout)(float, float, float, float);
    void (*shutdown)();

    template <class F> void bind(F& fn, const char* name) {
        fn = reinterpret_cast<F>(GetProcAddress(module, name));
        if (!fn) { std::printf("missing export %s\n", name); std::exit(2); }
    }

    explicit Api(const wchar_t* path) {
        module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) { std::wprintf(L"cannot load %ls (%lu)\n", path, GetLastError()); std::exit(2); }
        bind(init, "rug_init"); bind(load_texture, "rug_load_texture"); bind(rebuild, "rug_rebuild");
        bind(set_bumps, "rug_set_bumps"); bind(step, "rug_step"); bind(render, "rug_render");
        bind(mouse_down, "rug_mouse_down"); bind(mouse_drag, "rug_mouse_drag"); bind(mouse_up, "rug_mouse_up");
        bind(hit_test, "rug_hit_test"); bind(rotate, "rug_rotate"); bind(resize, "rug_resize");
        bind(get_pose, "rug_get_pose"); bind(get_corners, "rug_get_corners"); bind(get_bounds, "rug_get_bounds");
        bind(hold_begin, "rug_hold_begin"); bind(hold_end, "rug_hold_end"); bind(layout, "rug_layout");
        bind(shutdown, "rug_shutdown");
        // The candidate exports rug_invalidate and returns whether rug_render drew anything.
        renderReportsChange = GetProcAddress(module, "rug_invalidate") != nullptr;
    }

    // Returns true when the buffer may have changed.
    bool Render(std::vector<unsigned char>& dst, int* rect) {
        if (renderReportsChange)
            return reinterpret_cast<int (*)(unsigned char*, int, int*)>(render)(dst.data(), kWidth * 4, rect) != 0;
        reinterpret_cast<void (*)(unsigned char*, int, int*)>(render)(dst.data(), kWidth * 4, rect);
        return true;
    }
};

struct Side {
    Api api;
    std::vector<unsigned char> pixels = std::vector<unsigned char>(size_t(kWidth) * kHeight * 4, 0);
    double seconds = 0;
    int drawn = 0;
    explicit Side(const wchar_t* path) : api(path) {}
};

int g_failures = 0;
int g_frame = 0;
bool g_bitExact = true;
std::string g_phase;

void Fail(const char* what) {
    if (++g_failures <= 20) std::printf("MISMATCH frame %d (%s): %s\n", g_frame, g_phase.c_str(), what);
}

void CompareFloats(const char* what, const float* a, const float* b, int n) {
    if (std::memcmp(a, b, sizeof(float) * n) != 0) Fail(what);
}

// One frame of the overlay loop: step, render, then read back what Overlay.cs reads.
void Frame(Side& ref, Side& cand, float cursorX, float cursorY) {
    ++g_frame;
    int rect[4];
    for (Side* s : { &ref, &cand }) {
        auto t0 = std::chrono::steady_clock::now();
        s->api.step(kFrameSeconds);
        if (s->api.Render(s->pixels, rect)) ++s->drawn;
        s->seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    if (!g_bitExact) return;
    if (ref.pixels != cand.pixels) Fail("pixels");
    float a[8], b[8];
    ref.api.get_pose(a); cand.api.get_pose(b); CompareFloats("pose", a, b, 3);
    ref.api.get_bounds(a); cand.api.get_bounds(b); CompareFloats("bounds", a, b, 4);
    ref.api.get_corners(a); cand.api.get_corners(b); CompareFloats("corners", a, b, 8);
    if (ref.api.hit_test(cursorX, cursorY) != cand.api.hit_test(cursorX, cursorY)) Fail("hit test");
}

template <class F> void Both(Side& ref, Side& cand, F&& f) { f(ref.api); f(cand.api); }

struct PixelDiff {
    int pixels = 0;
    int maxChannel = 0;
};

PixelDiff Diff(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    PixelDiff d;
    for (size_t i = 0; i < a.size(); i += 4) {
        int m = 0;
        for (int k = 0; k < 4; ++k) m = std::max(m, std::abs(int(a[i + k]) - int(b[i + k])));
        if (m) { ++d.pixels; d.maxChannel = std::max(d.maxChannel, m); }
    }
    return d;
}

std::vector<float> IconBumps(float shiftX) {
    // Bumps in the layout Overlay.ApplyBumps sends: x, y (up), radius x, radius y, height.
    std::vector<float> b;
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 8; ++row) {
            float x = 47.0f + 95.0f * col + shiftX, y = kHeight - (37.0f + 125.0f * row);
            b.insert(b.end(), { x, y, 30.08f, 30.08f, 22.0f });
        }
    b.insert(b.end(), { 712.0f + shiftX, kHeight - 412.0f, 30.08f, 30.08f, 22.0f });
    b.insert(b.end(), { 960.0f, 600.0f, 30.08f, 30.08f, 22.0f });
    return b;
}

void Idle(Side& ref, Side& cand, int frames, const char* phase) {
    g_phase = phase;
    int drawnBefore = cand.drawn, lastDrawn = -1;
    for (int i = 0; i < frames; ++i) {
        int before = cand.drawn;
        Frame(ref, cand, 960.0f, 600.0f);
        if (cand.drawn != before) lastDrawn = i;
    }
    std::printf("  %-26s candidate drew %3d of %3d frames, last at %d\n", phase, cand.drawn - drawnBefore, frames, lastDrawn);
}

}

int RestScenario(Side& ref, Side& cand) {
    // The user's desktop: three columns of icons and one icon under the dropped rug.
    std::vector<float> bumps;
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 8; ++row)
            bumps.insert(bumps.end(), { 47.0f + 95.0f * col, kHeight - (37.0f + 125.0f * row), 30.08f, 30.08f, 22.0f });
    bumps.insert(bumps.end(), { 712.0f, kHeight - 412.0f, 30.08f, 30.08f, 22.0f });
    Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });

    g_bitExact = false;
    const int frames = 1600, measureFrom = 960;
    std::vector<unsigned char> previous = ref.pixels;
    PixelDiff worstSelf, worstCand;
    int lastDrawn = -1, selfChangedFrames = 0;
    double refSeconds = 0, candSeconds = 0;
    for (int i = 0; i < frames; ++i) {
        double r0 = ref.seconds, c0 = cand.seconds;
        int before = cand.drawn;
        Frame(ref, cand, 960.0f, 600.0f);
        if (cand.drawn != before) lastDrawn = i;
        if (i < measureFrom) { previous = ref.pixels; continue; }
        refSeconds += ref.seconds - r0;
        candSeconds += cand.seconds - c0;
        PixelDiff self = Diff(previous, ref.pixels), vs = Diff(ref.pixels, cand.pixels);
        if (self.pixels) ++selfChangedFrames;
        if (self.pixels > worstSelf.pixels) worstSelf.pixels = self.pixels;
        worstSelf.maxChannel = std::max(worstSelf.maxChannel, self.maxChannel);
        if (vs.pixels > worstCand.pixels) worstCand.pixels = vs.pixels;
        worstCand.maxChannel = std::max(worstCand.maxChannel, vs.maxChannel);
        previous = ref.pixels;
    }
    int measured = frames - measureFrom;
    std::printf("candidate last drew at frame %d (%.1f s after the drop)\n", lastDrawn, (lastDrawn + 1) * kFrameSeconds);
    std::printf("last %d frames (%.0f s):\n", measured, measured * kFrameSeconds);
    std::printf("  reference vs its previous frame: changed in %d frames, worst %d pixels, max channel diff %d\n",
                selfChangedFrames, worstSelf.pixels, worstSelf.maxChannel);
    std::printf("  candidate vs reference:          worst %d pixels, max channel diff %d\n", worstCand.pixels, worstCand.maxChannel);
    std::printf("  time per frame: reference %.2f ms, candidate %.3f ms\n", refSeconds * 1000 / measured, candSeconds * 1000 / measured);
    // The sleeping candidate passes when it stays within the reference's own frame-to-frame change.
    const bool asleep = lastDrawn < measureFrom;
    const bool within = worstCand.pixels <= worstSelf.pixels && worstCand.maxChannel <= worstSelf.maxChannel;
    if (!asleep) std::printf("FAIL: the candidate was still drawing during the measured window\n");
    if (!within) std::printf("FAIL: the candidate differs from the reference more than the reference differs from itself\n");
    return asleep && within ? 0 : 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 6) { std::printf("usage: equivalence exact|sleep <reference dll> <candidate dll> <rug> <second rug>\n"); return 2; }
    const bool sleepMode = std::wstring(argv[1]) == L"sleep";
    Side ref(argv[2]), cand(argv[3]);
    if (!ref.api.init(kWidth, kHeight) || !cand.api.init(kWidth, kHeight)) { std::printf("init failed\n"); return 2; }
    if (!ref.api.load_texture(argv[4]) || !cand.api.load_texture(argv[4])) { std::printf("texture failed\n"); return 2; }
    Both(ref, cand, [](Api& a) { a.rebuild(0.46f, 90.0f); });
    if (sleepMode) return RestScenario(ref, cand);
    auto bumps = IconBumps(0);
    Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });

    Idle(ref, cand, 240, "drop and settle");

    g_phase = "drag";
    float pose[3];
    ref.api.get_pose(pose);
    float gx = pose[0] + 300.0f, gy = pose[1] + 120.0f;
    int downRef = ref.api.mouse_down(gx, gy), downCand = cand.api.mouse_down(gx, gy);
    if (downRef != downCand) Fail("mouse down");
    for (int i = 0; i < 90; ++i) {
        float t = i / 90.0f * 6.2831853f;
        float x = gx + 220.0f * std::sin(t), y = gy + 140.0f * (1.0f - std::cos(t));
        Both(ref, cand, [&](Api& a) { a.mouse_drag(x, y); });
        Frame(ref, cand, x, y);
    }
    Both(ref, cand, [](Api& a) { a.mouse_up(); });
    Idle(ref, cand, 150, "after drag");

    g_phase = "wheel rotate";
    for (int i = 0; i < 12; ++i) { Both(ref, cand, [](Api& a) { a.rotate(0.08f); }); Frame(ref, cand, 960, 600); }
    Idle(ref, cand, 120, "after rotate");

    g_phase = "resize";
    Both(ref, cand, [](Api& a) { a.resize(0.56f); });
    Idle(ref, cand, 150, "after resize");

    g_phase = "alt hold";
    Both(ref, cand, [](Api& a) { a.hold_begin(); });
    Idle(ref, cand, 40, "held");
    Both(ref, cand, [](Api& a) { a.rotate(-0.3f); });
    Idle(ref, cand, 20, "held after rotate");
    Both(ref, cand, [](Api& a) { a.hold_end(); });
    Idle(ref, cand, 180, "after hold");

    g_phase = "icons moved";
    bumps = IconBumps(380.0f);
    Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });
    Idle(ref, cand, 160, "icons moved");
    for (int i = 0; i < 3; ++i) {
        Both(ref, cand, [&](Api& a) { a.set_bumps(bumps.data(), int(bumps.size() / 5)); });
        Idle(ref, cand, 64, "same icons again");
    }

    g_phase = "drop again";
    ref.api.get_pose(pose);
    Both(ref, cand, [&](Api& a) { a.layout(pose[0], pose[1], pose[2], 110.0f); });
    Idle(ref, cand, 240, "after drop again");

    g_phase = "center";
    Both(ref, cand, [](Api& a) { a.layout(kWidth / 2.0f, kHeight / 2.0f, 0.0f, 60.0f); });
    Idle(ref, cand, 200, "after center");

    g_phase = "change rug";
    if (!ref.api.load_texture(argv[5]) || !cand.api.load_texture(argv[5])) Fail("second texture");
    Both(ref, cand, [](Api& a) { a.resize(0.46f); });
    Idle(ref, cand, 200, "after change rug");

    g_phase = "missing picture";
    if (ref.api.load_texture(L"C:\\no\\such\\picture.jpg") != cand.api.load_texture(L"C:\\no\\such\\picture.jpg"))
        Fail("missing picture result");
    Idle(ref, cand, 30, "after missing picture");

    Both(ref, cand, [](Api& a) { a.shutdown(); });
    std::printf("frames %d, mismatches %d\n", g_frame, g_failures);
    std::printf("reference: %.2f ms/frame, drew %d frames\n", ref.seconds * 1000 / g_frame, ref.drawn);
    std::printf("candidate: %.2f ms/frame, drew %d frames\n", cand.seconds * 1000 / g_frame, cand.drawn);
    return g_failures == 0 ? 0 : 1;
}
