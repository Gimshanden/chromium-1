// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/public/cpp/app_list/app_list_metrics.h"

#include "ash/public/cpp/app_list/app_list_types.h"
#include "base/metrics/histogram_macros.h"

namespace {

const char kAppListSearchResultOpenTypeHistogram[] =
    "Apps.AppListSearchResultOpenTypeV2";
const char kAppListSearchResultOpenTypeHistogramInTablet[] =
    "Apps.AppListSearchResultOpenTypeV2.TabletMode";
const char kAppListSearchResultOpenTypeHistogramInClamshell[] =
    "Apps.AppListSearchResultOpenTypeV2.ClamshellMode";
const char kAppListSuggestionChipOpenTypeHistogramInClamshell[] =
    "Apps.AppListSuggestedChipOpenType.ClamshellMode";
const char kAppListSuggestionChipOpenTypeHistogramInTablet[] =
    "Apps.AppListSuggestedChipOpenType.TabletMode";
const char kAppListZeroStateSuggestionOpenTypeHistogram[] =
    "Apps.AppListZeroStateSuggestionOpenType";

}  // namespace

namespace app_list {

void RecordSearchResultOpenTypeHistogram(
    ash::AppListLaunchedFrom launch_location,
    SearchResultType type,
    bool is_tablet_mode) {
  if (type == SEARCH_RESULT_TYPE_BOUNDARY) {
    NOTREACHED();
    return;
  }

  switch (launch_location) {
    case ash::AppListLaunchedFrom::kLaunchedFromSearchBox:
      UMA_HISTOGRAM_ENUMERATION(kAppListSearchResultOpenTypeHistogram, type,
                                SEARCH_RESULT_TYPE_BOUNDARY);
      if (is_tablet_mode) {
        UMA_HISTOGRAM_ENUMERATION(kAppListSearchResultOpenTypeHistogramInTablet,
                                  type, SEARCH_RESULT_TYPE_BOUNDARY);
      } else {
        UMA_HISTOGRAM_ENUMERATION(
            kAppListSearchResultOpenTypeHistogramInClamshell, type,
            SEARCH_RESULT_TYPE_BOUNDARY);
      }
      break;
    case ash::AppListLaunchedFrom::kLaunchedFromSuggestionChip:
      if (is_tablet_mode) {
        UMA_HISTOGRAM_ENUMERATION(
            kAppListSuggestionChipOpenTypeHistogramInTablet, type,
            SEARCH_RESULT_TYPE_BOUNDARY);
      } else {
        UMA_HISTOGRAM_ENUMERATION(
            kAppListSuggestionChipOpenTypeHistogramInClamshell, type,
            SEARCH_RESULT_TYPE_BOUNDARY);
      }
      break;
    case ash::AppListLaunchedFrom::kLaunchedFromShelf:
    case ash::AppListLaunchedFrom::kLaunchedFromGrid:
      // Search results don't live in the shelf or the app grid.
      NOTREACHED();
      break;
  }
}

void RecordZeroStateSuggestionOpenTypeHistogram(SearchResultType type) {
  UMA_HISTOGRAM_ENUMERATION(kAppListZeroStateSuggestionOpenTypeHistogram, type,
                            SEARCH_RESULT_TYPE_BOUNDARY);
}

}  // namespace app_list
