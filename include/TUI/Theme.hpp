#pragma once

#include <ftxui/screen/color.hpp>

namespace Exchange::TUI {

namespace Theme {
    // Custom HSL/RGB colors matching the professional trading terminal dark theme
    inline const auto Background      = ftxui::Color::RGB(15, 16, 18);
    inline const auto Surface         = ftxui::Color::RGB(24, 26, 30);
    inline const auto Border          = ftxui::Color::RGB(45, 48, 56);
    inline const auto TextLight       = ftxui::Color::RGB(220, 224, 230);
    inline const auto TextMuted       = ftxui::Color::RGB(120, 125, 135);
    
    inline const auto ProfitGreen     = ftxui::Color::RGB(39, 174, 96);
    inline const auto LossRed         = ftxui::Color::RGB(192, 57, 43);
    inline const auto SelectionBlue   = ftxui::Color::RGB(41, 128, 185);
    inline const auto WarningYellow   = ftxui::Color::RGB(243, 156, 18);
    inline const auto InfoCyan        = ftxui::Color::RGB(52, 152, 219);
    
    inline const auto ActiveHeaderBg  = ftxui::Color::RGB(30, 34, 42);
}

} // namespace Exchange::TUI
