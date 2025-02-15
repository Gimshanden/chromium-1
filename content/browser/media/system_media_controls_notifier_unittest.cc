// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/system_media_controls_notifier.h"

#include <memory>
#include <utility>

#include "services/media_session/public/mojom/media_session.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/win/system_media_controls/mock_system_media_controls_service.h"

namespace content {

using ABI::Windows::Media::MediaPlaybackStatus;
using media_session::mojom::MediaPlaybackState;
using media_session::mojom::MediaSessionInfo;
using media_session::mojom::MediaSessionInfoPtr;
using testing::_;
using testing::Expectation;

class SystemMediaControlsNotifierTest : public testing::Test {
 public:
  SystemMediaControlsNotifierTest() = default;
  ~SystemMediaControlsNotifierTest() override = default;

  void SetUp() override {
    notifier_ =
        std::make_unique<SystemMediaControlsNotifier>(/*connector=*/nullptr);
    notifier_->SetSystemMediaControlsServiceForTesting(
        &mock_system_media_controls_service_);
    notifier_->Initialize();
  }

 protected:
  void SimulatePlaying() {
    MediaSessionInfoPtr session_info(MediaSessionInfo::New());
    session_info->playback_state = MediaPlaybackState::kPlaying;
    notifier_->MediaSessionInfoChanged(std::move(session_info));
  }

  void SimulatePaused() {
    MediaSessionInfoPtr session_info(MediaSessionInfo::New());
    session_info->playback_state = MediaPlaybackState::kPaused;
    notifier_->MediaSessionInfoChanged(std::move(session_info));
  }

  void SimulateStopped() { notifier_->MediaSessionInfoChanged(nullptr); }

  void SimulateMetadataChanged(bool empty,
                               base::string16 title,
                               base::string16 artist) {
    if (!empty) {
      media_session::MediaMetadata metadata;
      metadata.title = title;
      metadata.artist = artist;
      notifier_->MediaSessionMetadataChanged(
          base::Optional<media_session::MediaMetadata>(metadata));
    } else {
      notifier_->MediaSessionMetadataChanged(base::nullopt);
    }
  }

  void SimulateImageChanged() {
    // Need a non-empty SkBitmap so MediaControllerImageChanged doesn't try to
    // get the icon from ChromeContentBrowserClient.
    SkBitmap bitmap;
    bitmap.allocN32Pixels(1, 1);
    notifier_->MediaControllerImageChanged(
        media_session::mojom::MediaSessionImageType::kArtwork, bitmap);
  }

  SystemMediaControlsNotifier& notifier() { return *notifier_; }
  system_media_controls::testing::MockSystemMediaControlsService&
  mock_system_media_controls_service() {
    return mock_system_media_controls_service_;
  }

 private:
  std::unique_ptr<SystemMediaControlsNotifier> notifier_;
  system_media_controls::testing::MockSystemMediaControlsService
      mock_system_media_controls_service_;

  DISALLOW_COPY_AND_ASSIGN(SystemMediaControlsNotifierTest);
};

TEST_F(SystemMediaControlsNotifierTest, ProperlyUpdatesPlaybackState) {
  Expectation playing = EXPECT_CALL(
      mock_system_media_controls_service(),
      SetPlaybackStatus(MediaPlaybackStatus::MediaPlaybackStatus_Playing));
  Expectation paused =
      EXPECT_CALL(
          mock_system_media_controls_service(),
          SetPlaybackStatus(MediaPlaybackStatus::MediaPlaybackStatus_Paused))
          .After(playing);
  Expectation stopped =
      EXPECT_CALL(
          mock_system_media_controls_service(),
          SetPlaybackStatus(MediaPlaybackStatus::MediaPlaybackStatus_Stopped))
          .After(paused);
  EXPECT_CALL(mock_system_media_controls_service(), ClearMetadata())
      .After(stopped);

  SimulatePlaying();
  SimulatePaused();
  SimulateStopped();
}

TEST_F(SystemMediaControlsNotifierTest, ProperlyUpdatesMetadata) {
  base::string16 title = L"title";
  base::string16 artist = L"artist";

  EXPECT_CALL(mock_system_media_controls_service(), SetTitle(title));
  EXPECT_CALL(mock_system_media_controls_service(), SetArtist(artist));
  EXPECT_CALL(mock_system_media_controls_service(), ClearMetadata()).Times(0);
  EXPECT_CALL(mock_system_media_controls_service(), UpdateDisplay());

  SimulateMetadataChanged(false, title, artist);
}

TEST_F(SystemMediaControlsNotifierTest, ProperlyUpdatesNullMetadata) {
  EXPECT_CALL(mock_system_media_controls_service(), SetTitle(_)).Times(0);
  EXPECT_CALL(mock_system_media_controls_service(), SetArtist(_)).Times(0);
  EXPECT_CALL(mock_system_media_controls_service(), ClearMetadata());

  SimulateMetadataChanged(true, L"", L"");
}

TEST_F(SystemMediaControlsNotifierTest, ProperlyUpdatesImage) {
  EXPECT_CALL(mock_system_media_controls_service(), SetThumbnail(_));

  SimulateImageChanged();
}

}  // namespace content
