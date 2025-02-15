// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/autofill_assistant/ui_controller_android.h"

#include <map>
#include <memory>
#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/metrics/field_trial_params.h"
#include "base/task/post_task.h"
#include "base/time/time.h"
#include "chrome/browser/android/chrome_feature_list.h"
#include "chrome/browser/autofill/android/personal_data_manager_android.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/common/channel_info.h"
#include "components/autofill/core/browser/data_model/autofill_profile.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill_assistant/browser/controller.h"
#include "components/autofill_assistant/browser/features.h"
#include "components/autofill_assistant/browser/metrics.h"
#include "components/autofill_assistant/browser/rectf.h"
#include "components/signin/core/browser/account_info.h"
#include "components/strings/grit/components_strings.h"
#include "components/version_info/channel.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "google_apis/google_api_keys.h"
#include "jni/AssistantDetailsModel_jni.h"
#include "jni/AssistantDetails_jni.h"
#include "jni/AssistantFormInput_jni.h"
#include "jni/AssistantFormModel_jni.h"
#include "jni/AssistantHeaderModel_jni.h"
#include "jni/AssistantInfoBoxModel_jni.h"
#include "jni/AssistantInfoBox_jni.h"
#include "jni/AssistantModel_jni.h"
#include "jni/AssistantOverlayModel_jni.h"
#include "jni/AssistantPaymentRequestModel_jni.h"
#include "jni/AutofillAssistantUiController_jni.h"
#include "services/identity/public/cpp/identity_manager.h"
#include "ui/base/l10n/l10n_util.h"

using base::android::AttachCurrentThread;
using base::android::JavaParamRef;
using base::android::JavaRef;

namespace autofill_assistant {

namespace {

// How long to leave the UI showing after AA has stopped.
static constexpr base::TimeDelta kGracefulShutdownDelay =
    base::TimeDelta::FromSeconds(5);

}  // namespace

// static
std::unique_ptr<UiControllerAndroid> UiControllerAndroid::CreateFromWebContents(
    content::WebContents* web_contents) {
  JNIEnv* env = AttachCurrentThread();
  auto jactivity = Java_AutofillAssistantUiController_findAppropriateActivity(
      env, web_contents->GetJavaWebContents());
  if (!jactivity) {
    return nullptr;
  }
  return std::make_unique<UiControllerAndroid>(env, jactivity);
}

UiControllerAndroid::UiControllerAndroid(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jactivity)
    : overlay_delegate_(this),
      header_delegate_(this),
      payment_request_delegate_(this),
      form_delegate_(this),
      weak_ptr_factory_(this) {
  java_object_ = Java_AutofillAssistantUiController_create(
      env, jactivity,
      /* allowTabSwitching= */
      base::FeatureList::IsEnabled(features::kAutofillAssistantChromeEntry),
      reinterpret_cast<intptr_t>(this));

  // Register overlay_delegate_ as delegate for the overlay.
  Java_AssistantOverlayModel_setDelegate(env, GetOverlayModel(),
                                         overlay_delegate_.GetJavaObject());

  // Register header_delegate_ as delegate for clicks on header buttons.
  Java_AssistantHeaderModel_setDelegate(env, GetHeaderModel(),
                                        header_delegate_.GetJavaObject());

  // Register payment_request_delegate_ as delegate for the payment request UI.
  Java_AssistantPaymentRequestModel_setDelegate(
      env, GetPaymentRequestModel(), payment_request_delegate_.GetJavaObject());
}

void UiControllerAndroid::Attach(content::WebContents* web_contents,
                                 Client* client,
                                 UiDelegate* ui_delegate) {
  DCHECK(web_contents);
  DCHECK(client);
  DCHECK(ui_delegate);

  client_ = client;
  ui_delegate_ = ui_delegate;
  captured_debug_context_.clear();

  JNIEnv* env = AttachCurrentThread();
  auto java_web_contents = web_contents->GetJavaWebContents();
  Java_AutofillAssistantUiController_setWebContents(env, java_object_,
                                                    java_web_contents);
  Java_AssistantPaymentRequestModel_setWebContents(
      env, GetPaymentRequestModel(), java_web_contents);
  Java_AssistantOverlayModel_setWebContents(env, GetOverlayModel(),
                                            java_web_contents);
  if (ui_delegate->GetState() != AutofillAssistantState::INACTIVE) {
    // The UI was created for an existing Controller.
    OnStatusMessageChanged(ui_delegate->GetStatusMessage());
    OnProgressChanged(ui_delegate->GetProgress());
    OnProgressVisibilityChanged(ui_delegate->GetProgressVisible());
    OnInfoBoxChanged(ui_delegate_->GetInfoBox());
    OnDetailsChanged(ui_delegate->GetDetails());
    OnSuggestionsChanged(ui_delegate->GetSuggestions());
    UpdateActions();
    OnPaymentRequestOptionsChanged(ui_delegate->GetPaymentRequestOptions());
    OnPaymentRequestInformationChanged(
        ui_delegate->GetPaymentRequestInformation());

    std::vector<RectF> area;
    ui_delegate->GetTouchableArea(&area);
    OnTouchableAreaChanged(area);
    OnResizeViewportChanged(ui_delegate->GetResizeViewport());
    OnPeekModeChanged(ui_delegate->GetPeekMode());
    OnFormChanged(ui_delegate->GetForm());

    UiDelegate::OverlayColors colors;
    ui_delegate->GetOverlayColors(&colors);
    OnOverlayColorsChanged(colors);

    OnStateChanged(ui_delegate->GetState());
  }

  SetVisible(true);
}

UiControllerAndroid::~UiControllerAndroid() {
  Java_AutofillAssistantUiController_clearNativePtr(AttachCurrentThread(),
                                                    java_object_);
}

base::android::ScopedJavaLocalRef<jobject> UiControllerAndroid::GetModel() {
  return Java_AutofillAssistantUiController_getModel(AttachCurrentThread(),
                                                     java_object_);
}

// Header related methods.

base::android::ScopedJavaLocalRef<jobject>
UiControllerAndroid::GetHeaderModel() {
  return Java_AssistantModel_getHeaderModel(AttachCurrentThread(), GetModel());
}

void UiControllerAndroid::OnStateChanged(AutofillAssistantState new_state) {
  if (!Java_AssistantModel_getVisible(AttachCurrentThread(), GetModel())) {
    // Leave the UI alone as long as it's invisible. Missed state changes will
    // be recovered by SetVisible(true).
    return;
  }
  SetupForState();
}

void UiControllerAndroid::SetupForState() {
  UpdateActions();
  AutofillAssistantState state = ui_delegate_->GetState();
  switch (state) {
    case AutofillAssistantState::STARTING:
      SetOverlayState(OverlayState::FULL);
      AllowShowingSoftKeyboard(false);
      SetSpinPoodle(true);
      return;

    case AutofillAssistantState::RUNNING:
      SetOverlayState(OverlayState::FULL);
      AllowShowingSoftKeyboard(false);
      SetSpinPoodle(true);
      return;

    case AutofillAssistantState::AUTOSTART_FALLBACK_PROMPT:
      SetOverlayState(OverlayState::HIDDEN);
      AllowShowingSoftKeyboard(true);
      SetSpinPoodle(false);

      // user interaction is needed.
      ExpandBottomSheet();
      return;

    case AutofillAssistantState::PROMPT:
      SetOverlayState(OverlayState::PARTIAL);
      AllowShowingSoftKeyboard(true);
      SetSpinPoodle(false);

      // user interaction is needed.
      ExpandBottomSheet();
      return;

    case AutofillAssistantState::MODAL_DIALOG:
      SetOverlayState(OverlayState::FULL);
      AllowShowingSoftKeyboard(true);
      SetSpinPoodle(true);
      return;

    case AutofillAssistantState::STOPPED:
      SetOverlayState(OverlayState::HIDDEN);
      AllowShowingSoftKeyboard(true);
      SetSpinPoodle(false);

      // make sure user sees the error message.
      ExpandBottomSheet();
      return;

    case AutofillAssistantState::INACTIVE:
      // TODO(crbug.com/806868): Add support for switching back to the inactive
      // state, which is the initial state.
      return;
  }
  NOTREACHED() << "Unknown state: " << static_cast<int>(state);
}

void UiControllerAndroid::OnStatusMessageChanged(const std::string& message) {
  if (!message.empty()) {
    JNIEnv* env = AttachCurrentThread();
    Java_AssistantHeaderModel_setStatusMessage(
        env, GetHeaderModel(),
        base::android::ConvertUTF8ToJavaString(env, message));
  }
}

void UiControllerAndroid::OnProgressChanged(int progress) {
  Java_AssistantHeaderModel_setProgress(AttachCurrentThread(), GetHeaderModel(),
                                        progress);
}

void UiControllerAndroid::OnProgressVisibilityChanged(bool visible) {
  Java_AssistantHeaderModel_setProgressVisible(AttachCurrentThread(),
                                               GetHeaderModel(), visible);
}

void UiControllerAndroid::OnResizeViewportChanged(bool resize_viewport) {
  Java_AutofillAssistantUiController_setResizeViewport(
      AttachCurrentThread(), java_object_, resize_viewport);
}

void UiControllerAndroid::OnPeekModeChanged(
    ConfigureBottomSheetProto::PeekMode peek_mode) {
  Java_AutofillAssistantUiController_setPeekMode(AttachCurrentThread(),
                                                 java_object_, peek_mode);
}

void UiControllerAndroid::OnOverlayColorsChanged(
    const UiDelegate::OverlayColors& colors) {
  JNIEnv* env = AttachCurrentThread();
  auto overlay_model = GetOverlayModel();
  if (!Java_AssistantOverlayModel_setBackgroundColor(
          env, overlay_model,
          base::android::ConvertUTF8ToJavaString(env, colors.background))) {
    DVLOG(1) << __func__
             << ": Ignoring invalid overlay color: " << colors.background;
  }
  if (!Java_AssistantOverlayModel_setHighlightBorderColor(
          env, overlay_model,
          base::android::ConvertUTF8ToJavaString(env,
                                                 colors.highlight_border))) {
    DVLOG(1) << __func__ << ": Ignoring invalid highlight border color: "
             << colors.highlight_border;
  }
}

void UiControllerAndroid::AllowShowingSoftKeyboard(bool enabled) {
  Java_AssistantModel_setAllowSoftKeyboard(AttachCurrentThread(), GetModel(),
                                           enabled);
}

void UiControllerAndroid::ExpandBottomSheet() {
  Java_AutofillAssistantUiController_expandBottomSheet(AttachCurrentThread(),
                                                       java_object_);
}

void UiControllerAndroid::SetSpinPoodle(bool enabled) {
  Java_AssistantHeaderModel_setSpinPoodle(AttachCurrentThread(),
                                          GetHeaderModel(), enabled);
}

void UiControllerAndroid::OnFeedbackButtonClicked() {
  JNIEnv* env = AttachCurrentThread();
  Java_AutofillAssistantUiController_showFeedback(
      env, java_object_,
      base::android::ConvertUTF8ToJavaString(env, GetDebugContext()));
}

void UiControllerAndroid::Shutdown(Metrics::DropOutReason reason) {
  client_->Shutdown(reason);
}

void UiControllerAndroid::ShowSnackbar(const std::string& message,
                                       base::OnceCallback<void()> action) {
  JNIEnv* env = AttachCurrentThread();
  auto jmodel = GetModel();
  if (!Java_AssistantModel_getVisible(env, jmodel)) {
    // If the UI is not visible, execute the action immediately.
    std::move(action).Run();
    return;
  }
  SetVisible(false);
  snackbar_action_ = std::move(action);
  Java_AutofillAssistantUiController_showSnackbar(
      env, java_object_, base::android::ConvertUTF8ToJavaString(env, message));
}

void UiControllerAndroid::SnackbarResult(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    jboolean undo) {
  base::OnceCallback<void()> action = std::move(snackbar_action_);
  if (!action) {
    NOTREACHED();
    return;
  }
  if (undo) {
    SetVisible(true);
    return;
  }
  std::move(action).Run();
}

std::string UiControllerAndroid::GetDebugContext() {
  if (captured_debug_context_.empty() && ui_delegate_) {
    return ui_delegate_->GetDebugContext();
  }
  return captured_debug_context_;
}

void UiControllerAndroid::DestroySelf() {
  client_->DestroyUI();
}

void UiControllerAndroid::SetVisible(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jcaller,
    jboolean visible) {
  SetVisible(visible);
}

void UiControllerAndroid::SetVisible(bool visible) {
  Java_AssistantModel_setVisible(AttachCurrentThread(), GetModel(), visible);
  if (visible) {
    // Recover possibly state changes missed by OnStateChanged()
    SetupForState();
  } else {
    SetOverlayState(OverlayState::HIDDEN);
  }
}

// Suggestions and actions carousels related methods.

void UiControllerAndroid::OnSuggestionsChanged(
    const std::vector<Chip>& suggestions) {
  JNIEnv* env = AttachCurrentThread();
  std::vector<int> icons(suggestions.size());
  std::vector<std::string> texts(suggestions.size());
  bool disabled[suggestions.size()];
  for (size_t i = 0; i < suggestions.size(); i++) {
    icons[i] = suggestions[i].icon;
    texts[i] = suggestions[i].text;
    disabled[i] = suggestions[i].disabled;
  }
  Java_AutofillAssistantUiController_setSuggestions(
      env, java_object_, base::android::ToJavaIntArray(env, icons),
      base::android::ToJavaArrayOfStrings(env, texts),
      base::android::ToJavaBooleanArray(env, disabled, suggestions.size()));
}

void UiControllerAndroid::UpdateActions() {
  DCHECK(ui_delegate_);

  JNIEnv* env = AttachCurrentThread();

  bool has_close_or_cancel = false;
  auto chips = Java_AutofillAssistantUiController_createChipList(env);
  const auto& actions = ui_delegate_->GetActions();
  int action_count = static_cast<int>(actions.size());
  for (int i = 0; i < action_count; i++) {
    const auto& action = actions[i];
    auto text = base::android::ConvertUTF8ToJavaString(env, action.text);
    int icon = action.icon;
    switch (action.type) {
      case HIGHLIGHTED_ACTION:
        Java_AutofillAssistantUiController_addHighlightedActionButton(
            env, java_object_, chips, icon, text, i, action.disabled,
            action.sticky);
        break;

      case NORMAL_ACTION:
        Java_AutofillAssistantUiController_addActionButton(
            env, java_object_, chips, icon, text, i, action.disabled,
            action.sticky);
        break;

      case CANCEL_ACTION:
        // A Cancel button sneaks in an UNDO snackbar before executing the
        // action, while a close button behaves like a normal button.
        Java_AutofillAssistantUiController_addCancelButton(
            env, java_object_, chips, icon, text, i, action.disabled,
            action.sticky);
        has_close_or_cancel = true;
        break;

      case CLOSE_ACTION:
        Java_AutofillAssistantUiController_addActionButton(
            env, java_object_, chips, icon, text, i, action.disabled,
            action.sticky);
        has_close_or_cancel = true;
        break;

      case DONE_ACTION:
        Java_AutofillAssistantUiController_addHighlightedActionButton(
            env, java_object_, chips, icon, text, i, action.disabled,
            action.sticky);
        has_close_or_cancel = true;
        break;

      case SUGGESTION:         // Suggestions are handled separately.
      case UNKNOWN_CHIP_TYPE:  // Ignore unknown types
        break;

        // Default intentionally left out to cause a compilation error when a
        // new type is added.
    }
  }

  if (!has_close_or_cancel) {
    if (ui_delegate_->GetState() == AutofillAssistantState::STOPPED) {
      Java_AutofillAssistantUiController_addCloseButton(
          env, java_object_, chips, ICON_CLEAR,
          base::android::ConvertUTF8ToJavaString(env, ""),
          /* disabled= */ false, /* sticky= */ true);
    } else if (ui_delegate_->GetState() != AutofillAssistantState::INACTIVE) {
      Java_AutofillAssistantUiController_addCancelButton(
          env, java_object_, chips, ICON_CLEAR,
          base::android::ConvertUTF8ToJavaString(env, ""), -1,
          /* disabled= */ false, /* sticky= */ true);
    }
  }

  Java_AutofillAssistantUiController_setActions(env, java_object_, chips);
}

void UiControllerAndroid::OnActionsChanged(const std::vector<Chip>& actions) {
  UpdateActions();
}

void UiControllerAndroid::OnSuggestionSelected(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jcaller,
    jint index) {
  if (ui_delegate_)
    ui_delegate_->SelectSuggestion(index);
}

void UiControllerAndroid::OnActionSelected(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jcaller,
    jint index) {
  if (ui_delegate_)
    ui_delegate_->SelectAction(index);
}

void UiControllerAndroid::OnCancelButtonClicked(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jcaller,
    jint index) {
  OnCancelButtonWithActionIndexClicked(index);
}

void UiControllerAndroid::OnCancelButtonClicked() {
  OnCancelButtonWithActionIndexClicked(-1);
}

void UiControllerAndroid::OnCancelButtonWithActionIndexClicked(
    int action_index) {
  ShowSnackbar(l10n_util::GetStringUTF8(IDS_AUTOFILL_ASSISTANT_STOPPED),
               base::BindOnce(&UiControllerAndroid::OnCancel,
                              weak_ptr_factory_.GetWeakPtr(), action_index));
}

void UiControllerAndroid::OnCancel(int action_index) {
  if (action_index == -1 || !ui_delegate_) {
    Shutdown(Metrics::SHEET_CLOSED);
    return;
  }
  ui_delegate_->SelectAction(action_index);
}

void UiControllerAndroid::OnCloseButtonClicked(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jcaller) {
  DestroySelf();
}

// Overlay related methods.
base::android::ScopedJavaLocalRef<jobject>
UiControllerAndroid::GetOverlayModel() {
  return Java_AssistantModel_getOverlayModel(AttachCurrentThread(), GetModel());
}

void UiControllerAndroid::SetOverlayState(OverlayState state) {
  Java_AssistantOverlayModel_setState(AttachCurrentThread(), GetOverlayModel(),
                                      state);
  Java_AssistantModel_setAllowTalkbackOnWebsite(
      AttachCurrentThread(), GetModel(), state != OverlayState::FULL);
}

void UiControllerAndroid::OnTouchableAreaChanged(
    const std::vector<RectF>& areas) {
  JNIEnv* env = AttachCurrentThread();
  std::vector<float> flattened;
  for (const auto& rect : areas) {
    flattened.emplace_back(rect.left);
    flattened.emplace_back(rect.top);
    flattened.emplace_back(rect.right);
    flattened.emplace_back(rect.bottom);
  }
  Java_AssistantOverlayModel_setTouchableArea(
      env, GetOverlayModel(), base::android::ToJavaFloatArray(env, flattened));
}

void UiControllerAndroid::OnUnexpectedTaps() {
  ShowSnackbar(
      l10n_util::GetStringUTF8(IDS_AUTOFILL_ASSISTANT_MAYBE_GIVE_UP),
      base::BindOnce(&UiControllerAndroid::Shutdown,
                     weak_ptr_factory_.GetWeakPtr(), Metrics::SHEET_CLOSED));
}

void UiControllerAndroid::UpdateTouchableArea() {
  if (ui_delegate_)
    ui_delegate_->UpdateTouchableArea();
}

void UiControllerAndroid::OnUserInteractionInsideTouchableArea() {
  if (ui_delegate_)
    ui_delegate_->OnUserInteractionInsideTouchableArea();
}

// Other methods.

void UiControllerAndroid::ShowOnboarding(
    JNIEnv* env,
    const base::android::JavaParamRef<jstring>& jexperiment_ids,
    const base::android::JavaParamRef<jobject>& on_accept) {
  Java_AutofillAssistantUiController_onShowOnboarding(
      env, java_object_, jexperiment_ids, on_accept);
}

void UiControllerAndroid::WillShutdown(Metrics::DropOutReason reason) {
  if (reason == Metrics::CUSTOM_TAB_CLOSED) {
    Java_AutofillAssistantUiController_scheduleCloseCustomTab(
        AttachCurrentThread(), java_object_);
  }

  // Capture the debug context, for including into a feedback possibly sent
  // later, after the delegate has been possibly destroyed.
  DCHECK(ui_delegate_);
  captured_debug_context_ = ui_delegate_->GetDebugContext();
  AutofillAssistantState final_state = ui_delegate_->GetState();
  ui_delegate_ = nullptr;

  // Get rid of the UI, either immediately, or with a delay, to give time to
  // users to read any message.
  if (final_state != AutofillAssistantState::STOPPED) {
    DestroySelf();
  } else {
    base::PostDelayedTaskWithTraits(
        FROM_HERE, {content::BrowserThread::UI},
        base::BindOnce(&UiControllerAndroid::DestroySelf,
                       weak_ptr_factory_.GetWeakPtr()),
        kGracefulShutdownDelay);
  }
}

// Payment request related methods.

base::android::ScopedJavaLocalRef<jobject>
UiControllerAndroid::GetPaymentRequestModel() {
  return Java_AssistantModel_getPaymentRequestModel(AttachCurrentThread(),
                                                    GetModel());
}

void UiControllerAndroid::OnShippingAddressChanged(
    std::unique_ptr<autofill::AutofillProfile> address) {
  ui_delegate_->SetShippingAddress(std::move(address));
}

void UiControllerAndroid::OnBillingAddressChanged(
    std::unique_ptr<autofill::AutofillProfile> address) {
  ui_delegate_->SetBillingAddress(std::move(address));
}

void UiControllerAndroid::OnContactInfoChanged(std::string name,
                                               std::string phone,
                                               std::string email) {
  ui_delegate_->SetContactInfo(name, phone, email);
}

void UiControllerAndroid::OnCreditCardChanged(
    std::unique_ptr<autofill::CreditCard> card) {
  ui_delegate_->SetCreditCard(std::move(card));
}

void UiControllerAndroid::OnTermsAndConditionsChanged(
    TermsAndConditionsState state) {
  ui_delegate_->SetTermsAndConditions(state);
}

void UiControllerAndroid::OnPaymentRequestOptionsChanged(
    const PaymentRequestOptions* payment_options) {
  JNIEnv* env = AttachCurrentThread();
  auto jmodel = GetPaymentRequestModel();
  if (!payment_options) {
    Java_AssistantPaymentRequestModel_setVisible(env, jmodel, false);
    return;
  }

  Java_AssistantPaymentRequestModel_setRequestName(
      env, jmodel, payment_options->request_payer_name);
  Java_AssistantPaymentRequestModel_setRequestEmail(
      env, jmodel, payment_options->request_payer_email);
  Java_AssistantPaymentRequestModel_setRequestPhone(
      env, jmodel, payment_options->request_payer_phone);
  Java_AssistantPaymentRequestModel_setRequestShippingAddress(
      env, jmodel, payment_options->request_shipping);
  Java_AssistantPaymentRequestModel_setRequestPayment(
      env, jmodel, payment_options->request_payment_method);
  Java_AssistantPaymentRequestModel_setSupportedBasicCardNetworks(
      env, jmodel,
      base::android::ToJavaArrayOfStrings(
          env, payment_options->supported_basic_card_networks));

  Java_AssistantPaymentRequestModel_setVisible(env, jmodel, true);
}

void UiControllerAndroid::OnPaymentRequestInformationChanged(
    const PaymentInformation* state) {
  JNIEnv* env = AttachCurrentThread();
  auto jmodel = GetPaymentRequestModel();
  if (!state) {
    return;
  }

  // TODO(crbug.com/806868): Add |setContactDetails|, |setShippingAddress| and
  // |setPaymentMethod|.
  Java_AssistantPaymentRequestModel_setTermsStatus(env, jmodel,
                                                   state->terms_and_conditions);
}

// FormProto related methods.
base::android::ScopedJavaLocalRef<jobject> UiControllerAndroid::GetFormModel() {
  return Java_AssistantModel_getFormModel(AttachCurrentThread(), GetModel());
}

void UiControllerAndroid::OnFormChanged(const FormProto* form) {
  JNIEnv* env = AttachCurrentThread();

  if (!form) {
    Java_AssistantFormModel_clearInputs(env, GetFormModel());
    return;
  }

  auto jinput_list = Java_AssistantFormModel_createInputList(env);
  for (int i = 0; i < form->inputs_size(); i++) {
    const FormInputProto input = form->inputs(i);

    switch (input.input_type_case()) {
      case FormInputProto::InputTypeCase::kCounter: {
        CounterInputProto counter_input = input.counter();

        auto jcounters = Java_AssistantFormInput_createCounterList(env);
        for (const CounterInputProto::Counter counter :
             counter_input.counters()) {
          Java_AssistantFormInput_addCounter(
              env, jcounters,
              Java_AssistantFormInput_createCounter(
                  env,
                  base::android::ConvertUTF8ToJavaString(env, counter.label()),
                  base::android::ConvertUTF8ToJavaString(env,
                                                         counter.subtext()),
                  counter.initial_value(), counter.min_value(),
                  counter.max_value()));
        }

        Java_AssistantFormModel_addInput(
            env, jinput_list,
            Java_AssistantFormInput_createCounterInput(
                env, i,
                base::android::ConvertUTF8ToJavaString(env,
                                                       counter_input.label()),
                base::android::ConvertUTF8ToJavaString(
                    env, counter_input.expand_text()),
                base::android::ConvertUTF8ToJavaString(
                    env, counter_input.minimize_text()),
                jcounters, counter_input.minimized_count(),
                counter_input.min_counters_sum(),
                counter_input.max_counters_sum(),
                form_delegate_.GetJavaObject()));
        break;
      }
      case FormInputProto::InputTypeCase::kSelection: {
        SelectionInputProto selection_input = input.selection();

        auto jchoices = Java_AssistantFormInput_createChoiceList(env);
        for (const SelectionInputProto::Choice choice :
             selection_input.choices()) {
          Java_AssistantFormInput_addChoice(
              env, jchoices,
              Java_AssistantFormInput_createChoice(
                  env,
                  base::android::ConvertUTF8ToJavaString(env, choice.label()),
                  choice.selected()));
        }

        Java_AssistantFormModel_addInput(
            env, jinput_list,
            Java_AssistantFormInput_createSelectionInput(
                env, i,
                base::android::ConvertUTF8ToJavaString(env,
                                                       selection_input.label()),
                jchoices, selection_input.allow_multiple(),
                form_delegate_.GetJavaObject()));
        break;
      }
      case FormInputProto::InputTypeCase::INPUT_TYPE_NOT_SET:
        NOTREACHED();
        break;
        // Intentionally no default case to make compilation fail if a new value
        // was added to the enum but not to this list.
    }

    Java_AssistantFormModel_setInputs(env, GetFormModel(), jinput_list);
  }
}

void UiControllerAndroid::OnCounterChanged(int input_index,
                                           int counter_index,
                                           int value) {
  ui_delegate_->SetCounterValue(input_index, counter_index, value);
}

void UiControllerAndroid::OnChoiceSelectionChanged(int input_index,
                                                   int choice_index,
                                                   bool selected) {
  ui_delegate_->SetChoiceSelected(input_index, choice_index, selected);
}

// Details related method.

base::android::ScopedJavaLocalRef<jobject>
UiControllerAndroid::GetDetailsModel() {
  return Java_AssistantModel_getDetailsModel(AttachCurrentThread(), GetModel());
}

void UiControllerAndroid::OnDetailsChanged(const Details* details) {
  JNIEnv* env = AttachCurrentThread();
  auto jmodel = GetDetailsModel();
  if (!details) {
    Java_AssistantDetailsModel_clearDetails(env, jmodel);
    return;
  }

  const DetailsProto& proto = details->detailsProto();
  const DetailsChangesProto& changes = details->changes();

  auto jdetails = Java_AssistantDetails_create(
      env, base::android::ConvertUTF8ToJavaString(env, proto.title()),
      base::android::ConvertUTF8ToJavaString(env, proto.image_url()),
      proto.image_clickthrough_data().allow_clickthrough(),
      base::android::ConvertUTF8ToJavaString(
          env, proto.image_clickthrough_data().description()),
      base::android::ConvertUTF8ToJavaString(
          env, proto.image_clickthrough_data().positive_text()),
      base::android::ConvertUTF8ToJavaString(
          env, proto.image_clickthrough_data().negative_text()),
      base::android::ConvertUTF8ToJavaString(
          env, proto.image_clickthrough_data().clickthrough_url()),
      proto.show_image_placeholder(),
      base::android::ConvertUTF8ToJavaString(env, proto.total_price_label()),
      base::android::ConvertUTF8ToJavaString(env, proto.total_price()),
      base::android::ConvertUTF8ToJavaString(env, details->GetDatetime()),
      proto.datetime().date().year(), proto.datetime().date().month(),
      proto.datetime().date().day(), proto.datetime().time().hour(),
      proto.datetime().time().minute(), proto.datetime().time().second(),
      base::android::ConvertUTF8ToJavaString(env, proto.description_line_1()),
      base::android::ConvertUTF8ToJavaString(env, proto.description_line_2()),
      base::android::ConvertUTF8ToJavaString(env, proto.description_line_3()),
      changes.user_approval_required(), changes.highlight_title(),
      changes.highlight_line1(), changes.highlight_line2(),
      changes.highlight_line3(), proto.animate_placeholders());
  Java_AssistantDetailsModel_setDetails(env, jmodel, jdetails);
}

// InfoBox related method.

base::android::ScopedJavaLocalRef<jobject>
UiControllerAndroid::GetInfoBoxModel() {
  return Java_AssistantModel_getInfoBoxModel(AttachCurrentThread(), GetModel());
}

void UiControllerAndroid::OnInfoBoxChanged(const InfoBox* info_box) {
  JNIEnv* env = AttachCurrentThread();
  auto jmodel = GetInfoBoxModel();
  if (!info_box) {
    Java_AssistantInfoBoxModel_clearInfoBox(env, jmodel);
    return;
  }

  const InfoBoxProto& proto = info_box->proto().info_box();
  auto jinfo_box = Java_AssistantInfoBox_create(
      env, base::android::ConvertUTF8ToJavaString(env, proto.image_path()),
      base::android::ConvertUTF8ToJavaString(env, proto.explanation()));
  Java_AssistantInfoBoxModel_setInfoBox(env, jmodel, jinfo_box);
}

void UiControllerAndroid::Stop(JNIEnv* env,
                               const base::android::JavaParamRef<jobject>& obj,
                               int jreason) {
  client_->Shutdown(static_cast<Metrics::DropOutReason>(jreason));
}

void UiControllerAndroid::OnFatalError(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    const base::android::JavaParamRef<jstring>& jmessage,
    int jreason) {
  if (!ui_delegate_)
    return;
  ui_delegate_->OnFatalError(
      base::android::ConvertJavaStringToUTF8(env, jmessage),
      static_cast<Metrics::DropOutReason>(jreason));
}
}  // namespace autofill_assistant.
