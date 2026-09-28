#pragma once
#include <atomic>
namespace sbk::ui {
enum class MenuAction { None, QuickSave, QuickLoad };
// UI callbacks run with the rendering context. Savestate driver calls belong
// to the frontend polling thread; never mutate its request state here.
class MenuActions {
    std::atomic<MenuAction> pending{MenuAction::None};
public:
    bool submit(MenuAction action) {
        auto empty = MenuAction::None;
        return pending.compare_exchange_strong(empty, action);
    }
    MenuAction take() { return pending.exchange(MenuAction::None); }
};
}
