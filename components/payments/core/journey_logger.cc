// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/journey_logger.h"

#include <algorithm>
#include <vector>

#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"

namespace payments {

namespace {

// Returns the JourneyLogger histograms name suffix based on the |section| and
// the |completion_status|.
std::string GetHistogramNameSuffix(
    int section,
    JourneyLogger::CompletionStatus completion_status) {
  std::string name_suffix;

  switch (section) {
    case JourneyLogger::SECTION_SHIPPING_ADDRESS:
      name_suffix = "ShippingAddress.";
      break;
    case JourneyLogger::SECTION_CONTACT_INFO:
      name_suffix = "ContactInfo.";
      break;
    case JourneyLogger::SECTION_PAYMENT_METHOD:
      name_suffix = "PaymentMethod.";
      break;
    default:
      break;
  }

  switch (completion_status) {
    case JourneyLogger::COMPLETION_STATUS_COMPLETED:
      name_suffix += "Completed";
      break;
    case JourneyLogger::COMPLETION_STATUS_USER_ABORTED:
      name_suffix += "UserAborted";
      break;
    case JourneyLogger::COMPLETION_STATUS_OTHER_ABORTED:
      name_suffix += "OtherAborted";
      break;
    default:
      break;
  }

  DCHECK(!name_suffix.empty());
  return name_suffix;
}

// Returns true when exactly one boolean value in the vector is true.
bool ValidateExclusiveBitVector(const std::vector<bool>& bit_vector) {
  bool seen_true_bit = false;
  for (auto bit : bit_vector) {
    if (!bit)
      continue;
    if (seen_true_bit)
      return false;
    seen_true_bit = true;
  }
  return seen_true_bit;
}

}  // namespace

JourneyLogger::JourneyLogger(bool is_incognito, ukm::SourceId source_id)
    : is_incognito_(is_incognito),
      events_(EVENT_INITIATED),
      source_id_(source_id) {}

JourneyLogger::~JourneyLogger() {
  UMA_HISTOGRAM_BOOLEAN("PaymentRequest.JourneyLoggerHasRecorded",
                        has_recorded_);
  if (!has_recorded_) {
    static base::debug::CrashKeyString* journey_logger_no_record =
        base::debug::AllocateCrashKeyString("journey_logger_no_record",
                                            base::debug::CrashKeySize::Size32);
    base::debug::SetCrashKeyString(journey_logger_no_record,
                                   base::StringPrintf("%d", events_));
    base::debug::DumpWithoutCrashing();
  }
}

void JourneyLogger::IncrementSelectionAdds(Section section) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_selection_adds_++;
}

void JourneyLogger::IncrementSelectionChanges(Section section) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_selection_changes_++;
}

void JourneyLogger::IncrementSelectionEdits(Section section) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_selection_edits_++;
}

void JourneyLogger::SetNumberOfSuggestionsShown(Section section,
                                                int number,
                                                bool has_complete_suggestion) {
  DCHECK_LT(section, SECTION_MAX);
  sections_[section].number_suggestions_shown_ = number;
  sections_[section].is_requested_ = true;
  sections_[section].has_complete_suggestion_ = has_complete_suggestion;
}

void JourneyLogger::SetCanMakePaymentValue(bool value) {
  // Do not log the outcome of canMakePayment in incognito mode.
  if (is_incognito_)
    return;

  SetEventOccurred(value ? EVENT_CAN_MAKE_PAYMENT_TRUE
                         : EVENT_CAN_MAKE_PAYMENT_FALSE);
}

void JourneyLogger::SetHasEnrolledInstrumentValue(bool value) {
  if (is_incognito_)
    return;

  SetEventOccurred(value ? EVENT_HAS_ENROLLED_INSTRUMENT_TRUE
                         : EVENT_HAS_ENROLLED_INSTRUMENT_FALSE);
}

void JourneyLogger::SetEventOccurred(Event event) {
  events_ |= event;
}

void JourneyLogger::SetRequestedInformation(bool requested_shipping,
                                            bool requested_email,
                                            bool requested_phone,
                                            bool requested_name) {
  // This method should only be called once per Payment Request.
  if (requested_shipping)
    SetEventOccurred(EVENT_REQUEST_SHIPPING);

  if (requested_email)
    SetEventOccurred(EVENT_REQUEST_PAYER_EMAIL);

  if (requested_phone)
    SetEventOccurred(EVENT_REQUEST_PAYER_PHONE);

  if (requested_name)
    SetEventOccurred(EVENT_REQUEST_PAYER_NAME);
}

void JourneyLogger::SetRequestedPaymentMethodTypes(
    bool requested_basic_card,
    bool requested_method_google,
    bool requested_method_other) {
  if (requested_basic_card)
    SetEventOccurred(EVENT_REQUEST_METHOD_BASIC_CARD);

  if (requested_method_google)
    SetEventOccurred(EVENT_REQUEST_METHOD_GOOGLE);

  if (requested_method_other)
    SetEventOccurred(EVENT_REQUEST_METHOD_OTHER);
}

void JourneyLogger::SetCompleted() {
  DCHECK(WasPaymentRequestTriggered());
  RecordJourneyStatsHistograms(COMPLETION_STATUS_COMPLETED);
}

void JourneyLogger::SetAborted(AbortReason reason) {
  // Always record the first abort reason regardless of whether the
  // PaymentRequest.show() was triggered or not.
  base::UmaHistogramEnumeration("PaymentRequest.CheckoutFunnel.Aborted", reason,
                                ABORT_REASON_MAX);

  if (reason == ABORT_REASON_ABORTED_BY_USER ||
      reason == ABORT_REASON_USER_NAVIGATION)
    RecordJourneyStatsHistograms(COMPLETION_STATUS_USER_ABORTED);
  else
    RecordJourneyStatsHistograms(COMPLETION_STATUS_OTHER_ABORTED);
}

void JourneyLogger::SetNotShown(NotShownReason reason) {
  DCHECK(!WasPaymentRequestTriggered());
  RecordJourneyStatsHistograms(COMPLETION_STATUS_COULD_NOT_SHOW);
  base::UmaHistogramEnumeration("PaymentRequest.CheckoutFunnel.NoShow", reason,
                                NOT_SHOWN_REASON_MAX);
}

void JourneyLogger::RecordJourneyStatsHistograms(
    CompletionStatus completion_status) {
  if (has_recorded_) {
    UMA_HISTOGRAM_BOOLEAN(
        "PaymentRequest.JourneyLoggerHasRecordedMultipleTimes", true);
    static base::debug::CrashKeyString* journey_logger_multiple_record =
        base::debug::AllocateCrashKeyString("journey_logger_multiple_record",
                                            base::debug::CrashKeySize::Size32);
    base::debug::SetCrashKeyString(journey_logger_multiple_record,
                                   base::StringPrintf("%d", events_));
    base::debug::DumpWithoutCrashing();
  }
  has_recorded_ = true;

  RecordEventsMetric(completion_status);

  // These following metrics only make sense if the Payment Request was
  // triggered.
  if (WasPaymentRequestTriggered()) {
    RecordSectionSpecificStats(completion_status);
  }
}

void JourneyLogger::RecordSectionSpecificStats(
    CompletionStatus completion_status) {
  for (int i = 0; i < NUMBER_OF_SECTIONS; ++i) {
    std::string name_suffix = GetHistogramNameSuffix(i, completion_status);
    // Only log the metrics for a section if it was requested by the merchant.
    if (sections_[i].is_requested_) {
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSelectionAdds." + name_suffix,
          std::min(sections_[i].number_selection_adds_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSelectionChanges." + name_suffix,
          std::min(sections_[i].number_selection_changes_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSelectionEdits." + name_suffix,
          std::min(sections_[i].number_selection_edits_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);
      base::UmaHistogramCustomCounts(
          "PaymentRequest.NumberOfSuggestionsShown." + name_suffix,
          std::min(sections_[i].number_suggestions_shown_, MAX_EXPECTED_SAMPLE),
          MIN_EXPECTED_SAMPLE, MAX_EXPECTED_SAMPLE, NUMBER_BUCKETS);
    }
  }
}

void JourneyLogger::RecordEventsMetric(CompletionStatus completion_status) {
  // Add the completion status to the events.
  switch (completion_status) {
    case COMPLETION_STATUS_COMPLETED:
      events_ |= EVENT_COMPLETED;
      break;
    case COMPLETION_STATUS_USER_ABORTED:
      events_ |= EVENT_USER_ABORTED;
      break;
    case COMPLETION_STATUS_OTHER_ABORTED:
      events_ |= EVENT_OTHER_ABORTED;
      break;
    case COMPLETION_STATUS_COULD_NOT_SHOW:
      events_ |= EVENT_COULD_NOT_SHOW;
      break;
    default:
      NOTREACHED();
  }

  // Add the whether the user had complete suggestions for all requested
  // sections to the events.
  bool user_had_complete_suggestions_for_requested_information = true;
  bool is_showing_suggestions = false;
  for (int i = 0; i < NUMBER_OF_SECTIONS; ++i) {
    if (sections_[i].is_requested_) {
      is_showing_suggestions = true;
      if (sections_[i].number_suggestions_shown_ == 0 ||
          !sections_[i].has_complete_suggestion_) {
        user_had_complete_suggestions_for_requested_information = false;
      }
    }
  }
  if (is_showing_suggestions &&
      user_had_complete_suggestions_for_requested_information) {
    events_ |= EVENT_HAD_NECESSARY_COMPLETE_SUGGESTIONS;
  }

  // Add whether the user had and initial form of payment to the events.
  if (sections_[SECTION_PAYMENT_METHOD].number_suggestions_shown_ > 0)
    events_ |= EVENT_HAD_INITIAL_FORM_OF_PAYMENT;

  // Record the events in UMA.
  ValidateEventBits();
  base::UmaHistogramSparse("PaymentRequest.Events", events_);

  if (source_id_ == ukm::kInvalidSourceId)
    return;

  // Record the events in UKM.
  ukm::builders::PaymentRequest_CheckoutEvents(source_id_)
      .SetCompletionStatus(completion_status)
      .SetEvents(events_)
      .Record(ukm::UkmRecorder::Get());
}

void JourneyLogger::ValidateEventBits() const {
  std::vector<bool> bit_vector;

  // Validate completion status.
  bit_vector.push_back(events_ & EVENT_COMPLETED);
  bit_vector.push_back(events_ & EVENT_OTHER_ABORTED);
  bit_vector.push_back(events_ & EVENT_USER_ABORTED);
  bit_vector.push_back(events_ & EVENT_COULD_NOT_SHOW);
  DCHECK(ValidateExclusiveBitVector(bit_vector));
  bit_vector.clear();
  if (events_ & EVENT_COMPLETED)
    DCHECK(events_ & EVENT_PAY_CLICKED);

  // Validate the user selected method.
  if (events_ & EVENT_COMPLETED) {
    bit_vector.push_back(events_ & EVENT_SELECTED_CREDIT_CARD);
    bit_vector.push_back(events_ & EVENT_SELECTED_GOOGLE);
    bit_vector.push_back(events_ & EVENT_SELECTED_OTHER);
    DCHECK(ValidateExclusiveBitVector(bit_vector));
    bit_vector.clear();
  }

  // Selected method should be requested.
  if (events_ & EVENT_SELECTED_CREDIT_CARD) {
    DCHECK(events_ & EVENT_REQUEST_METHOD_BASIC_CARD);
  } else if (events_ & EVENT_SELECTED_GOOGLE) {
    DCHECK(events_ & EVENT_REQUEST_METHOD_GOOGLE);
  } else if (events_ & EVENT_SELECTED_OTHER) {
    // It is possible that a service worker based app responds to "basic-card"
    // request.
    DCHECK(events_ & EVENT_REQUEST_METHOD_OTHER ||
           events_ & EVENT_REQUEST_METHOD_BASIC_CARD);
  }

  // Validate UI SHOWN status.
  if (events_ & EVENT_COMPLETED) {
    bit_vector.push_back(events_ & EVENT_SHOWN);
    bit_vector.push_back(events_ & EVENT_SKIPPED_SHOW);
    DCHECK(ValidateExclusiveBitVector(bit_vector));
    bit_vector.clear();
  }

  // Validate skipped UI show.
  if (events_ & EVENT_SKIPPED_SHOW) {
    // Built in autofill payment handler for basic card should not skip UI show.
    DCHECK(!(events_ & EVENT_SELECTED_CREDIT_CARD));
    // Payment sheet should not get skipped when any of the following info is
    // required.
    DCHECK(!(events_ & EVENT_REQUEST_SHIPPING));
    DCHECK(!(events_ & EVENT_REQUEST_PAYER_NAME));
    DCHECK(!(events_ & EVENT_REQUEST_PAYER_EMAIL));
    DCHECK(!(events_ & EVENT_REQUEST_PAYER_PHONE));
  }

  // Check that the two bits are not set at the same time.
  DCHECK(!(events_ & EVENT_CAN_MAKE_PAYMENT_TRUE) ||
         !(events_ & EVENT_CAN_MAKE_PAYMENT_FALSE));
}

bool JourneyLogger::WasPaymentRequestTriggered() {
  return (events_ & EVENT_SHOWN) > 0 || (events_ & EVENT_SKIPPED_SHOW) > 0;
}

}  // namespace payments
