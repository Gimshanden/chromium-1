// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ELEMENT_AREA_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ELEMENT_AREA_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/autofill_assistant/browser/client_settings.h"
#include "components/autofill_assistant/browser/rectf.h"
#include "components/autofill_assistant/browser/selector.h"

namespace autofill_assistant {
class ScriptExecutorDelegate;

// A helper that keeps track of the area on the screen that correspond to an
// changeable set of elements.
class ElementArea {
 public:
  // |delegate| and |settings| must remain valid for the lifetime of this
  // instance.
  explicit ElementArea(ScriptExecutorDelegate* delegate);
  ~ElementArea();

  // Clears the area. Stops scheduled updates.
  void Clear();

  // Updates the area and keep checking for the element position and reporting
  // it until the area is cleared.
  //
  // The area is updated asynchronously, so Contains will not work right away.
  void SetFromProto(const ElementAreaProto& proto);

  // Forces an out-of-schedule update of the positions right away.
  //
  // This method is never strictly necessary. It is useful to call it when
  // there's a reason to think the positions might have changed, to speed up
  // updates.
  //
  // Does nothing if the area is empty.
  void UpdatePositions();

  // Defines a callback that'll be run every time the set of element coordinates
  // changes.
  //
  // The argument reports the areas that corresponds to currently known
  // elements, which might be empty.
  void SetOnUpdate(
      base::RepeatingCallback<void(const std::vector<RectF>& rectangles)> cb) {
    on_update_ = cb;
  }

  // Gets the position on the screen of all the rectangles that correspond to
  // the configured area.
  //
  // Each element in the vector corresponds to a rectangle, which might or might
  // not be empty.
  //
  // Note that the vector is not cleared before rectangles are added.
  void GetRectangles(std::vector<RectF>* area);

 private:
  // A rectangle that corresponds to the area of the visual viewport covered by
  // an element. Coordinates are values between 0 and 1, relative to the size of
  // the visible viewport.
  struct ElementPosition {
    // Selector. Might be empty.
    Selector selector;

    // Rectangle that corresponds to the selector. Might be empty.
    RectF rect;

    // If true, we're waiting for an updated rectangle for this
    // position.
    bool pending_update = false;

    ElementPosition();
    ElementPosition(const ElementPosition& orig);
    ~ElementPosition();
  };

  // A rectangular area, defined by its elements.
  struct Rectangle {
    std::vector<ElementPosition> positions;
    bool full_width = false;

    Rectangle();
    Rectangle(const Rectangle& orig);
    ~Rectangle();

    // A rectangle is pending if at least one ElementPosition is pending.
    bool IsPending() const;

    // Fills the given rectangle from the current state, if possible.
    void FillRect(RectF* rect) const;
  };

  void KeepUpdatingElementPositions();
  void OnGetElementPosition(const Selector& selector,
                            bool found,
                            const RectF& rect);
  void ReportUpdate();

  ScriptExecutorDelegate* const delegate_;
  std::vector<Rectangle> rectangles_;

  // If true, regular updates are currently scheduled.
  bool scheduled_update_;

  base::RepeatingCallback<void(const std::vector<RectF>& areas)> on_update_;

  base::WeakPtrFactory<ElementArea> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ElementArea);
};

}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ELEMENT_AREA_H_
