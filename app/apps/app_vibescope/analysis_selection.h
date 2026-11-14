/**
 * @file analysis_selection.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vibescope {

enum class AnalysisView : std::uint8_t { Spectrum = 0, Waterfall, Trend, Gyro };

/* 分析区页签选中状态:最多选二,保持点击顺序,超出挤掉最早一项(spec §4)。 */
class AnalysisSelection {
public:
    static constexpr std::size_t kMaxSelected = 2;

    bool toggle(AnalysisView view)
    {
        const std::size_t existing = index_of(view);
        if (existing < count_) {
            for (std::size_t i = existing; i + 1 < count_; ++i) order_[i] = order_[i + 1];
            --count_;
            return false;
        }
        if (count_ == kMaxSelected) {
            for (std::size_t i = 0; i + 1 < count_; ++i) order_[i] = order_[i + 1];
            --count_;
        }
        order_[count_++] = view;
        return true;
    }

    std::size_t count() const { return count_; }
    AnalysisView at(std::size_t index) const { return order_[index]; }
    bool selected(AnalysisView view) const { return index_of(view) < count_; }

private:
    std::size_t index_of(AnalysisView view) const
    {
        for (std::size_t i = 0; i < count_; ++i)
            if (order_[i] == view) return i;
        return kMaxSelected;
    }

    std::array<AnalysisView, kMaxSelected> order_{};
    std::size_t count_{0};
};

}  // namespace vibescope
