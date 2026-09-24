#include "execution.hpp"
#include "hle.hpp"
#include "runtime_owner.hpp"
#include "ultramodern/ultramodern.hpp"
#include "quiescence/quiescence.hpp"
#include <cstdio>
#include <memory>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace sbk::continuation {
namespace {
thread_local bool dispatching = false;
std::unique_ptr<Execution> startup;
bool startup_retired = false;
std::atomic<uint64_t> g_total_dispatches{0};
std::atomic<unsigned> reached_milestones{0};
enum Milestone : unsigned {
    Pak = 1, Menu = 2, Character = 4, Course = 8, Active = 16,
    Demo = 32, Scene = 64, DemoPlayers = 128, Save = 256, Rumble = 512,
    Finish = 1024
};

void print_manual_summary() {
    const unsigned reached = reached_milestones.load(std::memory_order_relaxed);
    // There is no native suspendable fallback path: unresolved transfers and
    // generated native reentry are rejected, never executed as a fallback.
    std::fprintf(stderr,
        "P4A SUMMARY backend=continuation total_dispatches=%llu "
        "native_suspendable_fallbacks=0 live_owners=%zu total_owners=%llu\n",
        (unsigned long long)g_total_dispatches.load(std::memory_order_relaxed),
        live_owner_count(), (unsigned long long)total_registered_owners());
    std::fprintf(stderr,
        "P4A SUMMARY milestones menu_navigation=%d character_select=%d "
        "course_select=%d race_active=%d race_finish=%d\n",
        !!(reached & Menu), !!(reached & Character), !!(reached & Course),
        !!(reached & Active), !!(reached & Finish));
    std::fflush(stderr);
}

void bind_cpu(Execution& execution) {
    auto& cpu=execution.cpu;
    cpu.f_odd=cpu.mips3_float_mode?&cpu.f1.u32l:&cpu.f0.u32h;
    if(std::fesetround(execution.rounding_mode)) throw std::runtime_error("Cannot bind guest rounding mode");
}
}

uint64_t total_dispatch_count() {
    return g_total_dispatches.load(std::memory_order_relaxed);
}

bool startup_is_retired() {
    return startup_retired;
}

void run_execution(uint8_t* rdram,Execution& e,uint64_t root) {
    if(dispatching) throw std::runtime_error("Native generated reentry is forbidden");
    struct DispatchScope { DispatchScope(){dispatching=true;} ~DispatchScope(){dispatching=false;} } scope;
    if(!e.started) {
        e.frames.push_back(make_frame(root));
        e.started=true;
        std::fprintf(stderr,"P4A owner start root=%llu backend=continuation\n",(unsigned long long)root);
    }
    while(!e.frames.empty()) {
        bind_cpu(e);
        if(e.blocked.phase!=BlockedPhase::None) {
            auto result=advance_hle(rdram,e);
            e.rounding_mode=std::fegetround();
            if(result==HleResult::WaitNext) {
                ultramodern::run_next_thread_and_wait(rdram);
            } else if(result==HleResult::CheckQueue) {
                if(ultramodern::this_thread()) ultramodern::check_running_queue(rdram);
            } else {
                const bool tail=e.blocked.tail;
                e.blocked={};
                if(tail) e.frames.pop_back();
            }
            continue;
        }
        auto action=step(rdram,&e.cpu,e.frames.back());
        ++e.dispatch_count;
        g_total_dispatches.fetch_add(1, std::memory_order_relaxed);
        e.rounding_mode=std::fegetround();
        if(action.kind==ActionKind::Lookup) {
            auto* token=get_function(int32_t(action.target));
            if(auto hle=hle_id_for_token(token)) {
                action.kind=ActionKind::Hle;action.target=hle;
            } else {
                action.kind=ActionKind::Call;action.target=descriptor_for_token(token).id;
            }
        }
        if(action.kind==ActionKind::Call) {
            // Manual telemetry never installs an input override.
            static const bool live_navigation = std::getenv("SBK_P4A_LIVE_NAVIGATION") != nullptr;
            static const bool manual = std::getenv("SBK_P4A_MANUAL") != nullptr;
            if (live_navigation || manual) {
                static uint64_t input_frame = 0;
                // Current interactive path, separate from the cumulative report.
                static unsigned path = 0;
                const char* name = descriptor(action.target).name;
                auto mark = [&](unsigned bit, const char* milestone) {
                    if (!(reached_milestones.fetch_or(bit, std::memory_order_relaxed) & bit)) {
                        std::fprintf(stderr, "P4A milestone: %s (%s)\n", milestone, name);
                    }
                };
                if (name) {
                    if (live_navigation && !std::strcmp(name, "updateControllerInputState")) {
                        std::fprintf(stderr, "P4A input_frame=%llu\n", (unsigned long long)++input_frame);
                    }
                    if (!std::strcmp(name, "initControllerPakReplaySaveMessageFlow")) mark(Pak, "controller_pak");
                    if (!std::strcmp(name, "initTitleDemoRaceIntro") ||
                        !std::strcmp(name, "initMainMenuDemoRaceIntro")) {
                        path = 0;
                        mark(Demo, "title_demo_entered");
                    }
                    if (!std::strcmp(name, "initMainMenu")) {
                        path = Menu;
                        mark(Menu, "menu_navigation");
                    }
                    if (!std::strcmp(name, "initCharacterSelectMenu") && (path & Menu)) {
                        path = Menu | Character;
                        mark(Character, "character_select");
                    }
                    if ((!std::strcmp(name, "initCourseSelectMenu") ||
                         !std::strcmp(name, "initMultiplayerCourseSelectMenu")) && (path & Character)) {
                        path = Menu | Character | Course;
                        mark(Course, "course_select");
                    }
                    if (!std::strcmp(name, "initRaceSceneFlow") && (path & Course)) {
                        path = Menu | Character | Course | Scene;
                        mark(Scene, "interactive_race_scene");
                    }
                    if (!std::strcmp(name, "updateRaceGameplayFlow") && (path & Scene)) {
                        path |= Active;
                        mark(Active, "race_active");
                    }
                    // Reached only after areRacePlayersFinished(), not pause/quit.
                    if (!std::strcmp(name, "waitRaceFinishResultsFlow") && (path & Active)) {
                        mark(Finish, "race_finish");
                    }
                    if (!std::strcmp(name, "initRacePlayers") && !(path & Scene)) mark(DemoPlayers, "demo_race_players");
                    if (!std::strcmp(name, "initRaceSetupSaveMenu")) mark(Save, "save_select");
                    if (!std::strcmp(name, "updateRaceSetupRumblePrompt")) mark(Rumble, "rumble_prompt");
                }
            }
        }

        switch(action.kind) {
        case ActionKind::Return:e.frames.pop_back();break;
        case ActionKind::Call:
            if(action.tail) e.frames.back()=make_frame(action.target);
            else e.frames.push_back(make_frame(action.target));
            break;
        case ActionKind::Hle:
            e.blocked.hle_id=action.target;
            e.blocked.phase=BlockedPhase::Begin;
            e.blocked.tail=action.tail;
            e.blocked.args[0]=e.cpu.r4;e.blocked.args[1]=e.cpu.r5;
            e.blocked.args[2]=e.cpu.r6;e.blocked.args[3]=e.cpu.r7;
            break;
        case ActionKind::Yield:
            sbk::quiescence::game_safepoint();break;
        case ActionKind::Pause:
            ultramodern::wait_for_external_message(rdram);
            if(ultramodern::this_thread()) ultramodern::check_running_queue(rdram);
            break;
        default:
            std::fprintf(stderr, "P4A rejected unresolved continuation transfer: kind=%d target=%llu\n", (int)action.kind, (unsigned long long)action.target);
            throw std::runtime_error("Unresolved continuation transfer");
        }
    }
    e.finished=true;
    std::fprintf(stderr,"P4A owner finished steps=%llu\n",(unsigned long long)e.dispatch_count);
}
void enter(uint64_t id,uint8_t* rdram,recomp_context* context) {
    if(dispatching || current_execution()) throw std::runtime_error("Native generated callback is forbidden");
    if(startup || startup_retired) throw std::runtime_error("Startup continuation cannot reenter");
    // Register after static owner storage is constructed, so this runs before
    // its destruction on normal window close / std::exit. No signal handlers.
    if (std::getenv("SBK_P4A_MANUAL") || std::getenv("SBK_P4A_LIVE_NAVIGATION")) {
        if (std::atexit(print_manual_summary) != 0) {
            throw std::runtime_error("Cannot register P4A exit summary");
        }
    }
    startup=std::make_unique<Execution>();
    startup->cpu=*context;
    startup->rounding_mode=std::fegetround();
    std::fprintf(stderr,"P4A startup registered backend=continuation\n");
    run_execution(rdram,*startup,id);
    *context=startup->cpu;
    context->f_odd=context->mips3_float_mode?&context->f1.u32l:&context->f0.u32h;
    startup.reset();startup_retired=true;
    std::fprintf(stderr,"P4A startup permanently retired\n");
}
}
