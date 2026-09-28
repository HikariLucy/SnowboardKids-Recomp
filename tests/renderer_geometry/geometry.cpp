// Presentation geometry regression (GRAPHICS-ASPECT-01), no GPU.
//
// Links the real RT64 decisions: WorkloadQueue::threadConfigurationUpdate
// (user aspect -> resolution scale), FramebufferPair::projectionCoversWidth
// (which 3D projections widen under Expand) and VIRenderer::getViewportAndScissor
// (outer presentation rectangle). The restore check runs the production
// presentation reset from RecompFrontend.
//
// Root cause modeled here: on the title screen the game draws its letterboxed
// 3D scene (scissor 16..304) and, every other blink phase, "PUSH START BUTTON"
// with a full-screen scissor into the same framebuffer pair. Coverage used to be
// judged against the union of scissor states, so the scene widened only while
// the text was hidden and the image grew and shrank. It is now judged against
// what the pair drew.
#include "hle/rt64_framebuffer_pair.h"
#include "hle/rt64_vi.h"
#include "hle/rt64_workload_queue.h"
#include "render/rt64_vi_renderer.h"
#include "rt64_presentation_history.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// RDP rectangles are in 10.2 fixed point.
RT64::FixedRect rect(int ulx, int uly, int lrx, int lry) { return {ulx * 4, uly * 4, lrx * 4, lry * 4}; }

struct SwapChain final : RenderSwapChain {
    uint32_t width, height;
    SwapChain(uint32_t w, uint32_t h) : width(w), height(h) {}
    bool present(uint32_t, RenderCommandSemaphore**, uint32_t) override { return true; }
    void wait() override {}
    bool resize() override { return true; }
    bool needsResize() const override { return false; }
    void setVsyncEnabled(bool) override {}
    bool isVsyncEnabled() const override { return false; }
    uint32_t getWidth() const override { return width; }
    uint32_t getHeight() const override { return height; }
    RenderTexture* getTexture(uint32_t) override { return nullptr; }
    uint32_t getTextureCount() const override { return 0; }
    bool acquireTexture(RenderCommandSemaphore*, uint32_t*) override { return false; }
    RenderWindow getWindow() const override { return {}; }
    bool isEmpty() const override { return false; }
    uint32_t getRefreshRate() const override { return 60; }
};

// NTSC 16-bit VI as the game programs it (values observed live). lines selects
// the visible height; origin is the double-buffered framebuffer.
RT64::VI vi(uint32_t lines, uint32_t origin) {
    RT64::VI v{};
    v.status.type = 2;
    v.width = 320;
    v.origin = origin;
    v.hRegion.hStart = 108;
    v.hRegion.hEnd = 748;
    v.vRegion.vStart = 37;
    v.vRegion.vEnd = 37 + (lines - 2) * 2;
    v.xTransform.xScale = 512;
    v.yTransform.yScale = 1024;
    return v;
}

struct Renderer {
    RT64::SharedQueueResources shared;
    std::unique_ptr<RT64::WorkloadQueue> queue = std::make_unique<RT64::WorkloadQueue>();

    Renderer(RT64::UserConfiguration::AspectRatio aspect, uint32_t w, uint32_t h) {
        shared.userConfig.aspectRatio = aspect;
        shared.userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
        shared.userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        queue->ext.sharedResources = &shared;
        resize(w, h);
    }
    // The workload queue drops size-dependent targets on a resize; there are none here.
    void resize(uint32_t w, uint32_t h) {
        shared.setSwapChainSize(w, h);
        shared.swapChainSizeChanged = false;
        shared.fbConfigChanged = false;
    }
    RT64::WorkloadQueue::WorkloadConfiguration configure(const RT64::VI& v) {
        RT64::WorkloadQueue::WorkloadConfiguration config;
        queue->threadConfigurationUpdate(v.fbSize(), config);
        return config;
    }
    // Rendered path: the presented target carries the workload's resolution scale.
    RenderViewport present(const RT64::VI& v) {
        const auto config = configure(v);
        const SwapChain swapChain(shared.swapChainWidth, shared.swapChainHeight);
        RenderViewport viewport;
        RenderRect scissor;
        RT64::VIRenderer::getViewportAndScissor(&swapChain, v, config.resolutionScale, 1, true, viewport, scissor);
        return viewport;
    }
};

bool same(const RenderViewport& a, const RenderViewport& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.width, b.width) && near(a.height, b.height);
}

void coverage() {
    // Title screen: one framebuffer pair per frame. Its 3D projection is the
    // letterbox viewport; odd frames add the blinking text with a full scissor.
    const RT64::FixedRect scene = rect(16, 32, 304, 208);
    const RT64::FixedRect text = rect(108, 186, 212, 196);
    const RT64::FixedRect full = rect(0, 0, 320, 240);
    std::vector<bool> widened, widenedByScissorUnion;
    for (int frame = 0; frame < 4; ++frame) {
        RT64::FramebufferPair pair;
        pair.reset();
        pair.scissorRect.merge(scene);
        pair.drawColorRect.merge(scene);
        if (frame & 1) {
            pair.scissorRect.merge(full);
            pair.drawColorRect.merge(text);
        }
        widened.push_back(pair.projectionCoversWidth(scene));
        widenedByScissorUnion.push_back(scene.ulx <= pair.scissorRect.ulx && scene.lrx >= pair.scissorRect.lrx);
    }
    check(widenedByScissorUnion == std::vector<bool>{true, false, true, false},
          "sequence reproduces the pulse under the old scissor-union rule");
    check(widened == std::vector<bool>{true, true, true, true},
          "letterboxed scene stays widened while overlay text blinks (A,B,A,B)");

    // Gameplay: HUD and 3D share the 16..304 scissor.
    RT64::FramebufferPair race;
    race.reset();
    race.scissorRect.merge(rect(16, 16, 304, 224));
    race.drawColorRect.merge(rect(16, 16, 304, 224));
    check(race.projectionCoversWidth(rect(16, 16, 304, 224)), "gameplay viewport still widened");

    // A 3D preview inside a menu whose 2D background covers the screen keeps 4:3.
    RT64::FramebufferPair menu;
    menu.reset();
    menu.scissorRect.merge(full);
    menu.drawColorRect.merge(full);
    check(!menu.projectionCoversWidth(rect(100, 60, 220, 180)), "3D preview box inside a full 2D menu not widened");

    // Nothing drawn: fall back to the scissor union, as before.
    RT64::FramebufferPair empty;
    empty.reset();
    empty.scissorRect.merge(full);
    check(!empty.projectionCoversWidth(scene) && empty.projectionCoversWidth(full), "empty pair falls back to scissor union");
    check(!empty.projectionCoversWidth(RT64::FixedRect()), "empty projection never covers");

    // Guest race_flow.c configures 2P as stacked full-width viewports and
    // 3P/4P as quadrants. A framebuffer pair can contain every player and
    // their HUD, so the drawn extent must be evaluated for each projection.
    RT64::FramebufferPair two;
    two.reset();
    const auto upper = rect(16, 16, 304, 120);
    const auto lower = rect(16, 120, 304, 224);
    two.drawColorRect.merge(upper);
    two.drawColorRect.merge(lower);
    two.drawColorRect.merge(rect(120, 20, 200, 36)); // player HUD
    check(two.projectionCoversWidth(upper) && two.projectionCoversWidth(lower),
          "2P stacked scene projections cover their drawn width");
    check(!two.projectionCoversWidth(rect(120, 20, 200, 36)),
          "2P HUD projection does not claim the scene width");

    for (int players : {3, 4}) {
        RT64::FramebufferPair pair;
        pair.reset();
        const auto p1 = rect(16, 16, 160, 120);
        const auto p2 = rect(16, 120, 160, 224);
        const auto p3 = rect(160, 16, 304, 120);
        pair.drawColorRect.merge(p1);
        pair.drawColorRect.merge(p2);
        pair.drawColorRect.merge(p3);
        if (players == 4) pair.drawColorRect.merge(rect(160, 120, 304, 224));
        check(!pair.projectionCoversWidth(p1) && !pair.projectionCoversWidth(p2) &&
              !pair.projectionCoversWidth(p3),
              players == 3 ? "3P quadrants stay separate in Expand" :
                             "4P quadrants stay separate in Expand");
    }
}

void presentation() {
    using AR = RT64::UserConfiguration::AspectRatio;
    const RT64::VI a = vi(240, 0x3B4000), b = vi(240, 0x3D9800), c = vi(224, 0x3B4000);
    check(a.fbSize().x == 320 && a.fbSize().y == 240 && c.fbSize().y == 224, "VI fixtures produce 320x240 and 320x224");

    Renderer original(AR::Original, 1280, 720);
    const auto oa = original.present(a);
    const auto ob = original.present(b);
    check(same(oa, ob) && same(original.present(a), oa), "Original: double-buffered VI A,B,A keeps the outer rect");
    check(near(oa.width / oa.height, 4.0f / 3.0f) && near(oa.height, 720.0f) && near(oa.x, 160.0f),
          "Original: 4:3 pillarboxed canvas in a 16:9 window");
    const auto config = original.configure(a);
    check(near(config.aspectRatioScale, 1.0f) && near(config.resolutionScale[0], 3.0f) && near(config.resolutionScale[1], 3.0f),
          "Original: no aspect scale, integer resolution scale 3");
    const auto oc = original.present(c);
    check(near(oc.width / oc.height, 320.0f / 224.0f) && same(original.present(c), oc),
          "Original: a different guest visible region is presented as that region, deterministically");

    Renderer expand(AR::Expand, 1280, 720);
    const auto ea = expand.present(a);
    bool stable = true;
    for (const auto& v : {b, c, a, c, b, a}) stable = stable && same(expand.present(v), ea);
    check(stable, "Expand: outer rect stable across VI A,B,C,A,C,B,A");
    check(near(ea.x, 0.0f) && near(ea.width, 1280.0f) && near(ea.height, 720.0f), "Expand: canvas fills the 16:9 window");
    const auto ec = expand.configure(a);
    check(near(ec.aspectRatioTarget, 16.0f / 9.0f) && near(ec.resolutionScale[0], 4.0f) && near(ec.resolutionScale[1], 3.0f),
          "Expand: target 16:9, resolution scale 4x3");

    expand.resize(1920, 1080);
    const auto big = expand.present(a);
    check(near(big.width, 1920.0f) && near(big.height, 1080.0f) && near(expand.configure(a).resolutionScale[1], 5.0f),
          "Expand resize to 1080p: canvas follows the window, scale 5");
    expand.resize(800, 800);
    const auto square = expand.present(a);
    check(near(square.width / square.height, 4.0f / 3.0f) && near(square.width, 800.0f) && near(square.y, 100.0f),
          "Expand in a window narrower than 4:3: letterboxed 4:3, never narrower than the guest");

    Renderer manual(AR::Original, 1280, 720);
    manual.shared.userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
    manual.shared.userConfig.resolutionMultiplier = 4.5;
    const auto mc = manual.configure(a);
    check(near(mc.resolutionScale[0], 4.5f) && near(mc.resolutionScale[1], 4.5f) && same(manual.present(a), oa),
          "resolution preset changes the internal scale, not the outer rect");

    // Restore: the production reset of host presentation history leaves the
    // user's aspect/resolution configuration and the resulting geometry intact.
    Renderer restored(AR::Expand, 1280, 720);
    const auto before = restored.present(a);
    const RT64::UserConfiguration user = restored.shared.userConfig;
    recompui::renderer::reset_shared_presentation_history(restored.shared);
    check(restored.shared.userConfig.aspectRatio == user.aspectRatio &&
          restored.shared.userConfig.resolution == user.resolution &&
          restored.shared.userConfig.resolutionMultiplier == user.resolutionMultiplier,
          "renderer restore keeps the user aspect and resolution");
    check(same(restored.present(a), before), "renderer restore keeps the presentation rect");
}
}

int main() {
    coverage();
    presentation();
    std::printf("%s renderer geometry (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
