/**
 * @file test_analysis_selection.cpp
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "test_support.hpp"

#include "apps/app_vibescope/analysis_selection.h"

using vibescope::AnalysisSelection;
using vibescope::AnalysisView;

void run_analysis_selection_tests()
{
    AnalysisSelection selection;
    EXPECT_TRUE(selection.count() == 0);
    EXPECT_FALSE(selection.selected(AnalysisView::Spectrum));

    EXPECT_TRUE(selection.toggle(AnalysisView::Spectrum));
    EXPECT_TRUE(selection.count() == 1);
    EXPECT_TRUE(selection.selected(AnalysisView::Spectrum));

    EXPECT_TRUE(selection.toggle(AnalysisView::Trend));
    EXPECT_TRUE(selection.count() == 2);
    EXPECT_TRUE(selection.at(0) == AnalysisView::Spectrum);
    EXPECT_TRUE(selection.at(1) == AnalysisView::Trend);

    /* 第三个挤掉最早的;顺序保持点击顺序 */
    EXPECT_TRUE(selection.toggle(AnalysisView::Waterfall));
    EXPECT_TRUE(selection.count() == 2);
    EXPECT_FALSE(selection.selected(AnalysisView::Spectrum));
    EXPECT_TRUE(selection.at(0) == AnalysisView::Trend);
    EXPECT_TRUE(selection.at(1) == AnalysisView::Waterfall);

    /* 取消选中回到单图 */
    EXPECT_FALSE(selection.toggle(AnalysisView::Trend));
    EXPECT_TRUE(selection.count() == 1);
    EXPECT_TRUE(selection.at(0) == AnalysisView::Waterfall);

    /* 重复 toggle 幂等 */
    EXPECT_TRUE(selection.toggle(AnalysisView::Trend));
    EXPECT_FALSE(selection.toggle(AnalysisView::Trend));
    EXPECT_TRUE(selection.count() == 1);
    EXPECT_TRUE(selection.at(0) == AnalysisView::Waterfall);
}
