// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/picture_in_picture/picture_in_picture_controller_impl.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/picture_in_picture/picture_in_picture.mojom-blink.h"
#include "third_party/blink/public/platform/web_media_stream.h"
#include "third_party/blink/public/platform/web_media_stream_track.h"
#include "third_party/blink/public/web/web_media_player_action.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/frame/frame_test_helpers.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/html/media/html_media_test_helper.h"
#include "third_party/blink/renderer/core/html/media/html_video_element.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/core/testing/wait_for_event.h"
#include "third_party/blink/renderer/platform/heap/heap.h"
#include "third_party/blink/renderer/platform/testing/empty_web_media_player.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"

using ::testing::_;

namespace blink {

// The MockPictureInPictureSession implements a PictureInPicture session in the
// same process as the test and guarantees that the callbacks are called in
// order for the events to be fired.
class MockPictureInPictureSession
    : public mojom::blink::PictureInPictureSession {
 public:
  MockPictureInPictureSession(
      mojo::InterfaceRequest<mojom::blink::PictureInPictureSession> request)
      : binding_(this, std::move(request)) {
    ON_CALL(*this, Stop(_)).WillByDefault([](StopCallback callback) {
      std::move(callback).Run();
    });
  }
  ~MockPictureInPictureSession() override = default;

  MOCK_METHOD1(Stop, void(StopCallback));
  MOCK_METHOD5(Update,
               void(uint32_t,
                    const base::Optional<viz::SurfaceId>&,
                    const blink::WebSize&,
                    bool,
                    bool));

 private:
  mojo::Binding<mojom::blink::PictureInPictureSession> binding_;
};

// The MockPictureInPictureService implements the PictureInPicture service in
// the same process as the test and guarantees that the callbacks are called in
// order for the events to be fired.
class MockPictureInPictureService
    : public mojom::blink::PictureInPictureService {
 public:
  MockPictureInPictureService() : binding_(this) {
    // Setup default implementations.
    ON_CALL(*this, StartSession(_, _, _, _, _, _, _))
        .WillByDefault(testing::Invoke(
            this, &MockPictureInPictureService::StartSessionInternal));
  }
  ~MockPictureInPictureService() override = default;

  void Bind(mojo::ScopedMessagePipeHandle handle) {
    binding_.Bind(
        mojom::blink::PictureInPictureServiceRequest(std::move(handle)));

    session_.reset(
        new MockPictureInPictureSession(mojo::MakeRequest(&session_ptr_)));
  }

  MOCK_METHOD7(StartSession,
               void(uint32_t,
                    const base::Optional<viz::SurfaceId>&,
                    const blink::WebSize&,
                    bool,
                    bool,
                    mojom::blink::PictureInPictureSessionObserverPtr,
                    StartSessionCallback));

  MockPictureInPictureSession& Session() { return *session_.get(); }

  void StartSessionInternal(uint32_t,
                            const base::Optional<viz::SurfaceId>&,
                            const blink::WebSize&,
                            bool,
                            bool,
                            mojom::blink::PictureInPictureSessionObserverPtr,
                            StartSessionCallback callback) {
    std::move(callback).Run(std::move(session_ptr_), WebSize());
  }

 private:
  mojo::Binding<mojom::blink::PictureInPictureService> binding_;
  std::unique_ptr<MockPictureInPictureSession> session_;
  mojom::blink::PictureInPictureSessionPtr session_ptr_;

  DISALLOW_COPY_AND_ASSIGN(MockPictureInPictureService);
};

class PictureInPictureControllerFrameClient
    : public test::MediaStubLocalFrameClient {
 public:
  static PictureInPictureControllerFrameClient* Create(
      std::unique_ptr<WebMediaPlayer> player) {
    return MakeGarbageCollected<PictureInPictureControllerFrameClient>(
        std::move(player));
  }

  explicit PictureInPictureControllerFrameClient(
      std::unique_ptr<WebMediaPlayer> player)
      : test::MediaStubLocalFrameClient(std::move(player)),
        interface_provider_(new service_manager::InterfaceProvider()) {}

  service_manager::InterfaceProvider* GetInterfaceProvider() override {
    return interface_provider_.get();
  }

 private:
  std::unique_ptr<service_manager::InterfaceProvider> interface_provider_;

  DISALLOW_COPY_AND_ASSIGN(PictureInPictureControllerFrameClient);
};

class PictureInPictureControllerPlayer : public EmptyWebMediaPlayer {
 public:
  PictureInPictureControllerPlayer() = default;
  ~PictureInPictureControllerPlayer() final = default;

  double Duration() const final {
    if (infinity_duration_)
      return std::numeric_limits<double>::infinity();
    return EmptyWebMediaPlayer::Duration();
  }

  ReadyState GetReadyState() const final { return kReadyStateHaveMetadata; }
  bool HasVideo() const final { return true; }

  void set_infinity_duration(bool value) { infinity_duration_ = value; }

 private:
  bool infinity_duration_ = false;

  DISALLOW_COPY_AND_ASSIGN(PictureInPictureControllerPlayer);
};

class PictureInPictureControllerTest : public PageTestBase {
 public:
  void SetUp() override {
    PageTestBase::SetupPageWithClients(
        nullptr, PictureInPictureControllerFrameClient::Create(
                     std::make_unique<PictureInPictureControllerPlayer>()));

    service_manager::InterfaceProvider::TestApi test_api(
        GetFrame().Client()->GetInterfaceProvider());
    test_api.SetBinderForName(
        mojom::blink::PictureInPictureService::Name_,
        WTF::BindRepeating(&MockPictureInPictureService::Bind,
                           WTF::Unretained(&mock_service_)));

    video_ = MakeGarbageCollected<HTMLVideoElement>(GetDocument());
    video_->SetReadyState(HTMLMediaElement::ReadyState::kHaveMetadata);
    layer_ = cc::Layer::Create();
    video_->SetCcLayerForTesting(layer_.get());

    std::string test_name =
        testing::UnitTest::GetInstance()->current_test_info()->name();
    if (test_name.find("MediaSource") != std::string::npos) {
      blink::WebMediaStream web_media_stream;
      blink::WebVector<blink::WebMediaStreamTrack> dummy_tracks;
      web_media_stream.Initialize(dummy_tracks, dummy_tracks);
      Video()->SetSrcObject(web_media_stream);
    } else {
      video_->SetSrc("http://example.com/foo.mp4");
    }

    test::RunPendingTasks();
  }

  HTMLVideoElement* Video() const { return video_.Get(); }
  MockPictureInPictureService& Service() { return mock_service_; }

 private:
  Persistent<HTMLVideoElement> video_;
  MockPictureInPictureService mock_service_;
  scoped_refptr<cc::Layer> layer_;
};

TEST_F(PictureInPictureControllerTest, EnterPictureInPictureFiresEvent) {
  EXPECT_EQ(nullptr, PictureInPictureControllerImpl::From(GetDocument())
                         .PictureInPictureElement());

  WebMediaPlayer* player = Video()->GetWebMediaPlayer();
  EXPECT_CALL(Service(),
              StartSession(player->GetDelegateId(), player->GetSurfaceId(),
                           player->NaturalSize(), true, false, _, _));

  PictureInPictureControllerImpl::From(GetDocument())
      .EnterPictureInPicture(Video(), nullptr /* options */,
                             nullptr /* promise */);

  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kEnterpictureinpicture);

  EXPECT_NE(nullptr, PictureInPictureControllerImpl::From(GetDocument())
                         .PictureInPictureElement());
}

TEST_F(PictureInPictureControllerTest, ExitPictureInPictureFiresEvent) {
  EXPECT_EQ(nullptr, PictureInPictureControllerImpl::From(GetDocument())
                         .PictureInPictureElement());

  WebMediaPlayer* player = Video()->GetWebMediaPlayer();
  EXPECT_CALL(Service(),
              StartSession(player->GetDelegateId(), player->GetSurfaceId(),
                           player->NaturalSize(), true, false, _, _));

  PictureInPictureControllerImpl::From(GetDocument())
      .EnterPictureInPicture(Video(), nullptr /* options */,
                             nullptr /* promise */);

  EXPECT_CALL(Service().Session(), Stop(_));

  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kEnterpictureinpicture);

  PictureInPictureControllerImpl::From(GetDocument())
      .ExitPictureInPicture(Video(), nullptr);

  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kLeavepictureinpicture);

  EXPECT_EQ(nullptr, PictureInPictureControllerImpl::From(GetDocument())
                         .PictureInPictureElement());
}

TEST_F(PictureInPictureControllerTest, StartObserving) {
  EXPECT_FALSE(PictureInPictureControllerImpl::From(GetDocument())
                   .GetSessionObserverBindingForTesting()
                   .is_bound());

  WebMediaPlayer* player = Video()->GetWebMediaPlayer();
  EXPECT_CALL(Service(),
              StartSession(player->GetDelegateId(), player->GetSurfaceId(),
                           player->NaturalSize(), true, false, _, _));

  PictureInPictureControllerImpl::From(GetDocument())
      .EnterPictureInPicture(Video(), nullptr /* options */,
                             nullptr /* promise */);

  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kEnterpictureinpicture);

  EXPECT_TRUE(PictureInPictureControllerImpl::From(GetDocument())
                  .GetSessionObserverBindingForTesting()
                  .is_bound());
}

TEST_F(PictureInPictureControllerTest, StopObserving) {
  EXPECT_FALSE(PictureInPictureControllerImpl::From(GetDocument())
                   .GetSessionObserverBindingForTesting()
                   .is_bound());

  WebMediaPlayer* player = Video()->GetWebMediaPlayer();
  EXPECT_CALL(Service(),
              StartSession(player->GetDelegateId(), player->GetSurfaceId(),
                           player->NaturalSize(), true, false, _, _));

  PictureInPictureControllerImpl::From(GetDocument())
      .EnterPictureInPicture(Video(), nullptr /* options */,
                             nullptr /* promise */);

  EXPECT_CALL(Service().Session(), Stop(_));

  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kEnterpictureinpicture);

  PictureInPictureControllerImpl::From(GetDocument())
      .ExitPictureInPicture(Video(), nullptr);
  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kLeavepictureinpicture);

  EXPECT_FALSE(PictureInPictureControllerImpl::From(GetDocument())
                   .GetSessionObserverBindingForTesting()
                   .is_bound());
}

TEST_F(PictureInPictureControllerTest, PlayPauseButton_InfiniteDuration) {
  EXPECT_EQ(nullptr, PictureInPictureControllerImpl::From(GetDocument())
                         .PictureInPictureElement());

  Video()->DurationChanged(std::numeric_limits<double>::infinity(), false);

  WebMediaPlayer* player = Video()->GetWebMediaPlayer();
  EXPECT_CALL(Service(),
              StartSession(player->GetDelegateId(), player->GetSurfaceId(),
                           player->NaturalSize(), false, false, _, _));

  PictureInPictureControllerImpl::From(GetDocument())
      .EnterPictureInPicture(Video(), nullptr /* options */,
                             nullptr /* promise */);

  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kEnterpictureinpicture);
}

TEST_F(PictureInPictureControllerTest, PlayPauseButton_MediaSource) {
  EXPECT_EQ(nullptr, PictureInPictureControllerImpl::From(GetDocument())
                         .PictureInPictureElement());

  // The test automatically setup the WebMediaPlayer with a MediaSource based on
  // the test name.

  WebMediaPlayer* player = Video()->GetWebMediaPlayer();
  EXPECT_CALL(Service(),
              StartSession(player->GetDelegateId(), player->GetSurfaceId(),
                           player->NaturalSize(), false, false, _, _));

  PictureInPictureControllerImpl::From(GetDocument())
      .EnterPictureInPicture(Video(), nullptr /* options */,
                             nullptr /* promise */);

  MakeGarbageCollected<WaitForEvent>(Video(),
                                     event_type_names::kEnterpictureinpicture);
}

TEST_F(PictureInPictureControllerTest, PerformMediaPlayerAction) {
  frame_test_helpers::WebViewHelper helper;
  helper.Initialize();

  WebLocalFrameImpl* frame = helper.LocalMainFrame();
  Document* document = frame->GetFrame()->GetDocument();

  Persistent<HTMLVideoElement> video =
      MakeGarbageCollected<HTMLVideoElement>(*document);
  document->body()->AppendChild(video);

  IntPoint bounds = video->BoundsInViewport().Center();

  frame->PerformMediaPlayerAction(
      WebPoint(bounds.X(), bounds.Y()),
      WebMediaPlayerAction(WebMediaPlayerAction::Type::kPictureInPicture,
                           true));
}

}  // namespace blink
