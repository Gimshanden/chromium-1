// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REMOTE_COCOA_BROWSER_APPLICATION_HOST_H_
#define COMPONENTS_REMOTE_COCOA_BROWSER_APPLICATION_HOST_H_

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "components/remote_cocoa/browser/remote_cocoa_browser_export.h"
#include "components/remote_cocoa/common/application.mojom.h"

namespace remote_cocoa {

// This class is the browser-side component corresponding to the NSApplication
// running in an app shim process. There exists one ApplicationHost per app shim
// process.
class REMOTE_COCOA_BROWSER_EXPORT ApplicationHost {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnApplicationHostDestroying(ApplicationHost* host) = 0;

   protected:
    ~Observer() override {}
  };

  ApplicationHost(mojom::ApplicationAssociatedRequest* request);
  ~ApplicationHost();

  mojom::Application* GetApplication();

  void AddObserver(Observer* observer);
  void RemoveObserver(const Observer* observer);

 private:
  mojom::ApplicationAssociatedPtr application_ptr_;
  base::ObserverList<Observer> observers_;
};

}  // namespace remote_cocoa

#endif  // COMPONENTS_REMOTE_COCOA_BROWSER_APPLICATION_HOST_H_
