#pragma once

#include "widget.h"
#include "grid.h"
#include <memory>
#include <vector>

namespace ui {

class Scene {
public:
    Scene() = default;
    
    Grid& grid() { return grid_; }
    const Grid& grid() const { return grid_; }
    
    Widget* add(std::unique_ptr<Widget> w);
    
    void remove(Widget* w);
    
    void flushRemovals();
    
    void clear();
    
    const std::vector<std::unique_ptr<Widget>>& roots() const { return roots_; }
    
    Widget* find(std::string_view id) const;
    
    void render(IDrawList* dl);
    
private:
    Grid grid_;
    std::vector<std::unique_ptr<Widget>> roots_;
    std::vector<Widget*> pending_removals_;
    
    void propagateScene(Widget* w);
    void detachScene(Widget* w);
};

} // namespace ui
