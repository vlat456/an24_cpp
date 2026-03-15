#include "ui/renderer/idraw_list.h"
#include <gtest/gtest.h>

TEST(UIRenderer, IDrawListExists) {
    struct MockDrawList : ui::IDrawList {
        void add_line(ui::Pt, ui::Pt, uint32_t, float) override {}
        void add_rect(ui::Pt, ui::Pt, uint32_t, float) override {}
        void add_rect_with_rounding_corners(ui::Pt, ui::Pt, uint32_t, float, int, float) override {}
        void add_rect_filled(ui::Pt, ui::Pt, uint32_t) override {}
        void add_rect_filled_with_rounding(ui::Pt, ui::Pt, uint32_t, float) override {}
        void add_rect_filled_with_rounding_corners(ui::Pt, ui::Pt, uint32_t, float, int) override {}
        void add_circle(ui::Pt, float, uint32_t, int) override {}
        void add_circle_filled(ui::Pt, float, uint32_t, int) override {}
        void add_text(ui::Pt, const char*, uint32_t, float) override {}
        void add_polyline(const ui::Pt*, size_t, uint32_t, float) override {}
        ui::Pt calc_text_size(const char*, float) const override { return ui::Pt{}; }
    };
    MockDrawList dl;
    SUCCEED();
}
