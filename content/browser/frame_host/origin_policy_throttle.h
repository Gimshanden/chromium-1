// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_ORIGIN_POLICY_THROTTLE_H_
#define CONTENT_BROWSER_FRAME_HOST_ORIGIN_POLICY_THROTTLE_H_

#include <map>
#include <memory>
#include <string>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_throttle.h"
#include "services/network/public/mojom/origin_policy_manager.mojom.h"

class GURL;

namespace url {
class Origin;
}

namespace content {
class NavigationHandle;
enum class OriginPolicyErrorReason;

// Constant derived from the spec, https://github.com/WICG/origin-policy
static constexpr const char* kDefaultOriginPolicyVersion = "0";

// The OriginPolicyThrottle is responsible for deciding whether an origin
// policy should be fetched, and doing so when that is positive.
//
// The intended use is that the navigation request will
// - call OriginPolicyThrottle::ShouldRequestOriginPolicy to determine whether
//   a policy should be requested, and add the appropriate SecOriginPolicy:
//   header.
// - call OriginPolicyThrottle::MaybeCreateThrottleFor a given navigation.
//   This will use presence of the header to decide whether to create a
//   throttle or not.
class CONTENT_EXPORT OriginPolicyThrottle : public NavigationThrottle {
 public:
  struct PolicyVersionAndReportTo {
    std::string policy_version;
    std::string report_to;
  };

  // Determine whether to request a policy (or advertise origin policy
  // support). Returns whether the policy header should be sent.
  static bool ShouldRequestOriginPolicy(const GURL& url);

  // Create a throttle (if the request contains the appropriate header.
  // The throttle will handle fetching of the policy and updating the
  // navigation request with the result.
  static std::unique_ptr<NavigationThrottle> MaybeCreateThrottleFor(
      NavigationHandle* handle);

  // Adds an exception for the given url, despite it serving a broken (or
  // otherwise invalid) policy. This is meant to be called by the security
  // interstitial.
  // This will exempt the entire origin, rather than only the given URL.
  static void AddExceptionFor(BrowserContext* browser_context, const GURL& url);

  ~OriginPolicyThrottle() override;

  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillProcessResponse() override;
  const char* GetNameForLogging() override;

  using KnownVersionMap = std::map<url::Origin, std::string>;
  static KnownVersionMap& GetKnownVersionsForTesting();

 private:
  explicit OriginPolicyThrottle(NavigationHandle* handle);

  // TODO(andypaicu): Remove when we have moved reporting logic to the network
  // service.
  PolicyVersionAndReportTo GetRequestedPolicyAndReportGroupFromHeader() const;
  static PolicyVersionAndReportTo
  GetRequestedPolicyAndReportGroupFromHeaderString(const std::string& header);

  const url::Origin GetRequestOrigin() const;

  void CancelNavigation(OriginPolicyErrorReason reason, const GURL& policy_url);

  void Report(OriginPolicyErrorReason reason, const GURL& policy_url);

  void OnOriginPolicyManagerRetrieveDone(
      const network::mojom::OriginPolicyPtr origin_policy);

  DISALLOW_COPY_AND_ASSIGN(OriginPolicyThrottle);
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_ORIGIN_POLICY_THROTTLE_H_
