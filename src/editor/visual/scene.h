#pragma once
#include "widget.h"
#include "grid.h"
#include <memory>
#include <vector>

namespace visual {

struct RenderContext;

class Scene {
public:
    Scene() = default;
    
    Grid& grid() { return grid_; }
    const Grid& grid() const { return grid_; }
    
    Widget* add(std::unique_ptr<Widget> w);
    
    void remove(Widget* w);
    
    void flushRemovals();
    
    /// Remove all root widgets and clear the grid.
    void clear();
    
    const std::vector<std::unique_ptr<Widget>>& roots() const { return roots_; }
    
    Widget* find(std::string_view id) const;
    
    void render(IDrawList* dl, const RenderContext& ctx);

private:
    Grid grid_;
    std::vector<std::unique_ptr<Widget>> roots_;
    std::vector<Widget*> pending_removals_;
    
    void propagateScene(Widget* w);
    void detachScene(Widget* w);
};

} // namespace visual
