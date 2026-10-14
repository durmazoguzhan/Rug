// Checks that a rug restored from saved vertex positions (rug_set_points) is still interactive: it can be
// hit-tested and grabbed, exactly like a rug that settled from a fresh drop. Written to pin the "restored rug
// cannot be moved" bug (2026-10-14): the settle/restore path has no GUI test, so it slipped past.
// Usage: test.exe <rug_native.dll> <rug picture>
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1200;
constexpr float kDt = 1.0f / 60.0f;

struct Api {
    HMODULE module = nullptr;
    int (*init)(int, int);
    int (*load_texture)(const wchar_t*);
    void (*rebuild)(float, float);
    void (*step)(float);
    int (*mouse_down)(float, float);
    void (*mouse_drag)(float, float);
    void (*mouse_up)();
    int (*hit_test)(float, float);
    void (*get_pose)(float*);
    int (*point_count)();
    int (*get_points)(float*, int);
    int (*set_points)(const float*, int);
    int (*is_settled)();
    void (*shutdown)();

    template <class F> void bind(F& fn, const char* name) {
        fn = reinterpret_cast<F>(GetProcAddress(module, name));
        if (!fn) { std::printf("missing export %s\n", name); std::exit(2); }
    }
    explicit Api(const wchar_t* path) {
        module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) { std::wprintf(L"cannot load %ls (%lu)\n", path, GetLastError()); std::exit(2); }
        bind(init, "rug_init"); bind(load_texture, "rug_load_texture"); bind(rebuild, "rug_rebuild");
        bind(step, "rug_step"); bind(mouse_down, "rug_mouse_down"); bind(mouse_drag, "rug_mouse_drag");
        bind(mouse_up, "rug_mouse_up"); bind(hit_test, "rug_hit_test"); bind(get_pose, "rug_get_pose");
        bind(point_count, "rug_point_count"); bind(get_points, "rug_get_points"); bind(set_points, "rug_set_points");
        bind(is_settled, "rug_is_settled"); bind(shutdown, "rug_shutdown");
    }
};

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::printf("usage: test.exe <rug_native.dll> <rug picture>\n"); return 2; }
    Api api(argv[1]);
    if (!api.init(kWidth, kHeight)) { std::printf("rug_init failed\n"); return 2; }
    if (!api.load_texture(argv[2])) { std::printf("rug_load_texture failed\n"); return 2; }

    // A fresh drop that settles: this is the known-good interactive state.
    api.rebuild(0.46f, 90.0f);
    for (int i = 0; i < 1200; ++i) api.step(kDt);
    float pose[3] = {};
    api.get_pose(pose);
    std::printf("fresh settled: pose=(%.1f, %.1f) settled=%d\n", pose[0], pose[1], api.is_settled());
    check(api.hit_test(pose[0], pose[1]) != 0, "fresh rug is hit-testable at its centre");

    // Save the drape, then restore it onto a fresh rebuild, like a restart does.
    int n = api.point_count();
    std::vector<float> buf(static_cast<size_t>(n) * 3);
    check(api.get_points(buf.data(), n) == n, "get_points returns the full vertex count");

    api.rebuild(0.46f, 90.0f);               // reset to a fresh centred drop
    int set = api.set_points(buf.data(), n);
    check(set == 1, "set_points accepts the saved drape");

    float rpose[3] = {};
    api.get_pose(rpose);
    std::printf("restored: pose=(%.1f, %.1f) settled=%d\n", rpose[0], rpose[1], api.is_settled());

    // The core checks: the restored rug must be hit-testable and grabbable at its own centre.
    check(api.hit_test(rpose[0], rpose[1]) != 0, "restored rug is hit-testable right after set_points");
    api.step(kDt);
    check(api.hit_test(rpose[0], rpose[1]) != 0, "restored rug is hit-testable after one step");

    int down = api.mouse_down(rpose[0], rpose[1]);
    check(down != 0, "restored rug can be grabbed (mouse_down)");
    // Drag the grabbed point sideways and let the cloth follow.
    for (int i = 0; i < 30; ++i) { api.mouse_drag(rpose[0] + 150.0f, rpose[1]); api.step(kDt); }
    api.mouse_up();
    float after[3] = {};
    api.get_pose(after);
    float moved = std::fabs(after[0] - rpose[0]);
    std::printf("after drag: pose=(%.1f, %.1f), centre moved %.1f px\n", after[0], after[1], moved);
    check(moved > 20.0f, "dragging the restored rug moves it");

    api.shutdown();
    std::printf("%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
