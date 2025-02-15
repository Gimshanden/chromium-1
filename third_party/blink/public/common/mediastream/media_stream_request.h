// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_PUBLIC_COMMON_MEDIASTREAM_MEDIA_STREAM_REQUEST_H_
#define THIRD_PARTY_BLINK_PUBLIC_COMMON_MEDIASTREAM_MEDIA_STREAM_REQUEST_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "media/base/audio_parameters.h"
#include "media/base/video_facing.h"
#include "media/capture/video/video_capture_device_descriptor.h"
#include "media/mojo/interfaces/display_media_information.mojom.h"
#include "third_party/blink/public/common/common_export.h"

namespace blink {

// Types of media streams. When updating this list, make sure to update the
// predicates declared below, e.g. IsVideoScreenCaptureMediaType().
enum MediaStreamType {
  MEDIA_NO_SERVICE = 0,

  // A device provided by the operating system (e.g., webcam input).
  MEDIA_DEVICE_AUDIO_CAPTURE,
  MEDIA_DEVICE_VIDEO_CAPTURE,

  // Below MEDIA_GUM_* types represent the streams generated by
  // getUserMedia() calls with constraints that are collected with legacy
  // APIs for desktop and tab capture.
  // Mirroring of a browser tab.
  MEDIA_GUM_TAB_AUDIO_CAPTURE,
  MEDIA_GUM_TAB_VIDEO_CAPTURE,
  // Desktop media sources.
  MEDIA_GUM_DESKTOP_VIDEO_CAPTURE,
  // Capture system audio (post-mix loopback stream).
  MEDIA_GUM_DESKTOP_AUDIO_CAPTURE,

  // Enables the capture of a user's display, generated by getDisplayMedia()
  // call.
  MEDIA_DISPLAY_VIDEO_CAPTURE,
  MEDIA_DISPLAY_AUDIO_CAPTURE,

  NUM_MEDIA_TYPES
};

// Types of media stream requests that can be made to the media controller.
enum MediaStreamRequestType {
  MEDIA_DEVICE_ACCESS = 0,
  MEDIA_DEVICE_UPDATE,
  MEDIA_GENERATE_STREAM,
  MEDIA_OPEN_DEVICE_PEPPER_ONLY  // Only used in requests made by Pepper.
};

// Convenience predicates to determine whether the given type represents some
// audio or some video device.
BLINK_COMMON_EXPORT bool IsAudioInputMediaType(MediaStreamType type);
BLINK_COMMON_EXPORT bool IsVideoInputMediaType(MediaStreamType type);
BLINK_COMMON_EXPORT bool IsScreenCaptureMediaType(MediaStreamType type);
// Whether the |type| captures anything on the screen.
BLINK_COMMON_EXPORT bool IsVideoScreenCaptureMediaType(MediaStreamType type);
BLINK_COMMON_EXPORT bool IsDesktopCaptureMediaType(MediaStreamType type);
BLINK_COMMON_EXPORT bool IsVideoDesktopCaptureMediaType(MediaStreamType type);
BLINK_COMMON_EXPORT bool IsTabCaptureMediaType(MediaStreamType type);
BLINK_COMMON_EXPORT bool IsDeviceMediaType(MediaStreamType type);

// TODO(xians): Change the structs to classes.
// Represents one device in a request for media stream(s).
struct BLINK_COMMON_EXPORT MediaStreamDevice {
  static const int kNoId;

  MediaStreamDevice();

  MediaStreamDevice(MediaStreamType type,
                    const std::string& id,
                    const std::string& name);

  MediaStreamDevice(
      MediaStreamType type,
      const std::string& id,
      const std::string& name,
      media::VideoFacingMode facing,
      const base::Optional<std::string>& group_id = base::nullopt);

  MediaStreamDevice(MediaStreamType type,
                    const std::string& id,
                    const std::string& name,
                    int sample_rate,
                    int channel_layout,
                    int frames_per_buffer);

  MediaStreamDevice(const MediaStreamDevice& other);

  ~MediaStreamDevice();

  MediaStreamDevice& operator=(const MediaStreamDevice& other);

  bool IsSameDevice(const MediaStreamDevice& other_device) const;

  // The device's type.
  MediaStreamType type;

  // The device's unique ID.
  std::string id;

  // The facing mode for video capture device.
  media::VideoFacingMode video_facing;

  // The device's group ID.
  base::Optional<std::string> group_id;

  // The device id of a matched output device if any (otherwise empty).
  // Only applicable to audio devices.
  base::Optional<std::string> matched_output_device_id;

  // The device's "friendly" name. Not guaranteed to be unique.
  std::string name;

  // Contains the device properties of the capture device. It's valid only when
  // the type of device is audio (i.e. IsAudioInputMediaType returns true).
  media::AudioParameters input =
      media::AudioParameters::UnavailableDeviceParams();

  // Id for this capture session. Unique for all sessions of the same type.
  int session_id = kNoId;

  // This field is optional and available only for display media devices.
  base::Optional<media::mojom::DisplayMediaInformationPtr> display_media_info;
};

using MediaStreamDevices = std::vector<MediaStreamDevice>;

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_COMMON_MEDIASTREAM_MEDIA_STREAM_REQUEST_H_
