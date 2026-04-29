#pragma once
#include "widget.h"
#include "ui/core/scene.h"
#include <memory>
#include <vector>

namespace visual {

struct RenderContext;

class Scene : public ui::Scene {
public:
    Scene() = default;

    ui::Widget* add(std::unique_ptr<ui::Widget> w) override;

    void flushRemovals() override {
        ui::Scene::flushRemovals();
        crossings_dirty_ = true;
    }

    void clear() override {
        ui::Scene::clear();
        crossings_dirty_ = true;
    }

    Widget* find(std::string_view id) const {
        return static_cast<Widget*>(ui::Scene::find(id));
    }

    void render(IDrawList* dl, const RenderContext& ctx);

    bool crossings_dirty() const { return crossings_dirty_; }
    void mark_crossings_dirty() { crossings_dirty_ = true; }
    void clear_crossings_dirty() { crossings_dirty_ = false; }

protected:
    void propagateScene(ui::Widget* w) override;
    void detachScene(ui::Widget* w) override;

private:
    bool crossings_dirty_ = true;
};

} // namespace visual
