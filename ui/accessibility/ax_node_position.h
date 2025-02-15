// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_AX_NODE_POSITION_H_
#define UI_ACCESSIBILITY_AX_NODE_POSITION_H_

#include <stdint.h>

#include <vector>

#include "base/strings/string16.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_export.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_position.h"
#include "ui/accessibility/ax_tree.h"

namespace ui {

// AXNodePosition includes implementations of AXPosition methods which require
// knowledge of the AXPosition AXNodeType (which is unknown by AXPosition).
class AX_EXPORT AXNodePosition : public AXPosition<AXNodePosition, AXNode> {
 public:
  AXNodePosition();
  ~AXNodePosition() override;

  AXPositionInstance Clone() const override;

  base::string16 GetText() const override;

  static void SetTreeForTesting(AXTree* tree) { tree_ = tree; }

 protected:
  AXNodePosition(const AXNodePosition& other) = default;
  void AnchorChild(int child_index,
                   AXTreeID* tree_id,
                   int32_t* child_id) const override;
  int AnchorChildCount() const override;
  int AnchorIndexInParent() const override;
  base::stack<AXNode*> GetAncestorAnchors() const override;
  void AnchorParent(AXTreeID* tree_id, int32_t* parent_id) const override;
  AXNode* GetNodeInTree(AXTreeID tree_id, int32_t node_id) const override;
  bool IsInWhiteSpace() const override;
  std::vector<int32_t> GetWordStartOffsets() const override;
  std::vector<int32_t> GetWordEndOffsets() const override;
  int32_t GetNextOnLineID(int32_t node_id) const override;
  int32_t GetPreviousOnLineID(int32_t node_id) const override;

 private:
  static AXTree* tree_;

  // Returns the parent node of the provided child. Returns the parent
  // node's tree id and node id through the provided output parameters,
  // parent_tree_id and parent_id.
  static AXNode* GetParent(AXNode* child,
                           AXTreeID child_tree_id,
                           AXTreeID* parent_tree_id,
                           int32_t* parent_id);
};

}  // namespace ui

#endif  // UI_ACCESSIBILITY_AX_NODE_POSITION_H_
