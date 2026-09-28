// Renderer lifecycle contract (P6-XPROC-01), no GPU.
//
// Runs the production reset (RecompFrontend rt64_presentation_history.h) on the
// real RT64::SharedQueueResources left behind by a warm process, then checks the
// present/workload handshake that RT64's queues assume. run.py verifies that the
// pinned rt64_present_queue.cpp still has the rules modeled here.
#include "rt64_presentation_history.h"

#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// PresentQueue::threadPresent: an interpolation candidate presents
// frameCounters.count frames, and presentId is published only at i == 0.
bool publishes_present_id(const RT64::InterpolatedFrameCounters& counters, bool interpolation_candidate) {
    const uint32_t frames = interpolation_candidate ? counters.count : 1;
    return frames >= 1;
}

// WorkloadQueue::renderThreadLoop with the same counter set: waits until
// (presented == 0) || (presented >= available).
bool workload_may_reuse(const RT64::InterpolatedFrameCounters& counters) {
    return counters.presented == 0 || counters.presented >= counters.available;
}

// The restored frame's present (id p, built by present_restored_frame) is the
// one the first post-restore workload waits on. Either counter set may be
// current when the present queue consumes it.
bool restored_present_unblocks_next_workload(const RT64::SharedQueueResources& shared, bool candidate) {
    for (const auto& counters : shared.interpolatedFrames) {
        if (!publishes_present_id(counters, candidate) || !workload_may_reuse(counters)) return false;
    }
    return true;
}

// A process that already presented interpolated frames of its own scene.
void warm_process(RT64::SharedQueueResources& shared, uint32_t vi_fb) {
    shared.colorImageAddressVector = {vi_fb};
    shared.colorImageAddressSet = {vi_fb};
    shared.interpolatedFrames[0] = {2, 2, 2, false};
    shared.interpolatedFrames[1] = {1, 1, 1, false};
    shared.interpolatedFramesIndex = 1;
}
} // namespace

int main() {
    constexpr uint32_t restored_vi_fb = 0x3DA800;

    // Negative control: the previous reset zeroed the counters but left the
    // warm process's color images, so the restored VI framebuffer was still an
    // interpolation candidate. The contract must reject that state.
    {
        RT64::SharedQueueResources shared;
        warm_process(shared, restored_vi_fb);
        shared.interpolatedFrames[0] = {};
        shared.interpolatedFrames[1] = {};
        check(!restored_present_unblocks_next_workload(shared, true),
            "control: zeroed counters with a stale candidate deadlock the handshake");
    }

    // Warm process (same-process restore or a fresh process that already ran
    // its own scenes): the stale candidate must not strand the restored present.
    {
        RT64::SharedQueueResources shared;
        warm_process(shared, restored_vi_fb);
        recompui::renderer::reset_shared_presentation_history(shared);
        check(shared.colorImageAddressVector.empty() && shared.colorImageAddressSet.empty(),
            "warm: pre-restore color images are discarded");
        check(shared.interpolatedColorTargets.empty(), "warm: interpolated targets are released");
        // Framebuffer::interpolationEnabled can stay stale on a reused framebuffer,
        // so the candidate path must also be safe.
        check(restored_present_unblocks_next_workload(shared, true),
            "warm: restored present publishes its id on the interpolation path");
        check(restored_present_unblocks_next_workload(shared, false),
            "warm: restored present publishes its id on the plain path");
        bool single_frame = true;
        for (const auto& counters : shared.interpolatedFrames) {
            single_frame = single_frame && counters.count == 1 && counters.available == 0 &&
                counters.presented == 0 && !counters.skipped;
        }
        check(single_frame, "warm: each counter set describes one non-interpolated frame");
    }

    // Fresh RT64 instance that never presented (fresh-process restore before
    // any game frame): the same post-restore state is reached.
    {
        RT64::SharedQueueResources shared;
        recompui::renderer::reset_shared_presentation_history(shared);
        check(restored_present_unblocks_next_workload(shared, true),
            "fresh: restored present publishes its id on the interpolation path");
    }

    // Restores repeat without drift.
    {
        RT64::SharedQueueResources shared;
        warm_process(shared, restored_vi_fb);
        for (int i = 0; i < 100; ++i) {
            recompui::renderer::reset_shared_presentation_history(shared);
            shared.colorImageAddressVector = {restored_vi_fb};
        }
        recompui::renderer::reset_shared_presentation_history(shared);
        check(restored_present_unblocks_next_workload(shared, true), "repeat: 100 restores keep the contract");
    }

    std::printf("renderer lifecycle contract: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
