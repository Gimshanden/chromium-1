// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SCRIPT_EXECUTOR_DELEGATE_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SCRIPT_EXECUTOR_DELEGATE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "components/autofill_assistant/browser/chip.h"
#include "components/autofill_assistant/browser/details.h"
#include "components/autofill_assistant/browser/info_box.h"
#include "components/autofill_assistant/browser/payment_request.h"
#include "components/autofill_assistant/browser/state.h"
#include "url/gurl.h"

namespace autofill {
class PersonalDataManager;
}  // namespace autofill

namespace content {
class WebContents;
}  // namespace content

namespace autofill_assistant {

class Service;
class UiController;
class WebController;
class ClientMemory;
struct ClientSettings;
struct TriggerContext;

class ScriptExecutorDelegate {
 public:
  class Listener {
   public:
    // The values returned by IsNavigatingToNewDocument() or
    // HasNavigationError() might have changed.
    virtual void OnNavigationStateChanged() = 0;
  };

  virtual const ClientSettings& GetSettings() = 0;
  virtual const GURL& GetCurrentURL() = 0;
  virtual const GURL& GetDeeplinkURL() = 0;
  virtual Service* GetService() = 0;
  virtual UiController* GetUiController() = 0;
  virtual WebController* GetWebController() = 0;
  virtual ClientMemory* GetClientMemory() = 0;
  virtual const TriggerContext* GetTriggerContext() = 0;
  virtual autofill::PersonalDataManager* GetPersonalDataManager() = 0;
  virtual content::WebContents* GetWebContents() = 0;
  virtual void EnterState(AutofillAssistantState state) = 0;

  // Make the area of the screen that correspond to the given elements
  // touchable.
  virtual void SetTouchableElementArea(const ElementAreaProto& element) = 0;
  virtual void SetStatusMessage(const std::string& message) = 0;
  virtual std::string GetStatusMessage() const = 0;
  virtual void SetDetails(std::unique_ptr<Details> details) = 0;
  virtual void SetInfoBox(const InfoBox& info_box) = 0;
  virtual void ClearInfoBox() = 0;
  virtual void SetPaymentRequestOptions(
      std::unique_ptr<PaymentRequestOptions> options) = 0;
  virtual void SetProgress(int progress) = 0;
  virtual void SetProgressVisible(bool visible) = 0;
  virtual void SetChips(std::unique_ptr<std::vector<Chip>> chips) = 0;
  virtual void SetResizeViewport(bool resize_viewport) = 0;
  virtual void SetPeekMode(ConfigureBottomSheetProto::PeekMode peek_mode) = 0;
  virtual bool SetForm(
      std::unique_ptr<FormProto> form,
      base::RepeatingCallback<void(const FormProto::Result*)> callback) = 0;

  // Makes no area of the screen touchable.
  void ClearTouchableElementArea() {
    SetTouchableElementArea(ElementAreaProto::default_instance());
  }

  // Returns true if a new document is being fetched for the main frame.
  //
  // Navigation ends once a response, with its associated URL has been
  // committed, whether the response is successful or not.
  //
  // Navigation of frames other than the main frame, loading of resource or
  // navigation to the same document aren't reported.
  //
  // Changes to this value is reported to Listener::OnNavigationStateChanged()
  virtual bool IsNavigatingToNewDocument() = 0;

  // Returns true if Chrome failed to fetch the response for its main document
  // during its last attempt.
  //
  // This is cleared once a page for the main document has been successfully
  // navigated to a new document.
  //
  // Navigation of frames other than the main frame, loading of resource or
  // navigation to the same document aren't taken into account for this value.
  //
  // Changes to this value is reported to Listener::OnNavigationStateChanged()
  virtual bool HasNavigationError() = 0;

  // Register a listener that can be told about changes. Duplicate calls are
  // ignored.
  virtual void AddListener(Listener* listener) = 0;

  // Removes a previously registered listener. Does nothing if no such listeners
  // exists.
  virtual void RemoveListener(Listener* listener) = 0;

 protected:
  virtual ~ScriptExecutorDelegate() {}
};
}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SCRIPT_EXECUTOR_DELEGATE_H_
