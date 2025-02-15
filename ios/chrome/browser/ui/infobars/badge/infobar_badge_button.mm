// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/infobars/badge/infobar_badge_button.h"

#import "ios/chrome/browser/ui/util/uikit_ui_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// Duration of button animations, in seconds.
const CGFloat kButtonAnimationDuration = 0.2;
// Edge insets of button.
const CGFloat kButtonEdgeInset = 6;
// White value of the button background in a selected state.
const CGFloat kSelectedWhiteValue = 0.80;
// Tint color of the button in an active state.
const CGFloat kActiveTintColor = 0x1A73E8;
// To achieve a circular corner radius, divide length of a side by 2.
const CGFloat kCircularCornerRadiusDivisor = 2.0;
}  // namespace

@interface InfobarBadgeButton ()
// Indicates whether the button has already been properly configured.
@property(nonatomic, assign) BOOL didSetupSubviews;
@end

@implementation InfobarBadgeButton

- (void)willMoveToSuperview:(UIView*)newSuperview {
  if (!self.didSetupSubviews) {
    self.didSetupSubviews = YES;
    [self
        setImage:[[UIImage imageNamed:@"infobar_passwords_icon"]
                     imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate]
        forState:UIControlStateNormal];
    self.imageView.contentMode = UIViewContentModeScaleToFill;
    self.imageEdgeInsets = UIEdgeInsetsMake(kButtonEdgeInset, kButtonEdgeInset,
                                            kButtonEdgeInset, kButtonEdgeInset);
  }
  [super willMoveToSuperview:newSuperview];
}

- (void)layoutSubviews {
  [super layoutSubviews];
  self.layer.cornerRadius =
      self.bounds.size.height / kCircularCornerRadiusDivisor;
}

- (void)setSelected:(BOOL)selected animated:(BOOL)animated {
  void (^changeBackgroundColor)() = ^{
    self.backgroundColor =
        selected ? [UIColor colorWithWhite:kSelectedWhiteValue alpha:1.0]
                 : [UIColor clearColor];
  };
  if (animated) {
    [UIView animateWithDuration:kButtonAnimationDuration
                     animations:^{
                       changeBackgroundColor();
                     }];
  } else {
    changeBackgroundColor();
  }
}
- (void)setActive:(BOOL)active animated:(BOOL)animated {
  void (^changeTintColor)() = ^{
    self.tintColor =
        active ? UIColorFromRGB(kActiveTintColor) : [UIColor lightGrayColor];
  };
  if (animated) {
    [UIView animateWithDuration:kButtonAnimationDuration
                     animations:^{
                       changeTintColor();
                     }];
  } else {
    changeTintColor();
  }
}

- (void)displayBadge:(BOOL)display animated:(BOOL)animated {
  void (^changeBadgeDisplay)() = ^{
    self.hidden = !display;
  };
  if (animated) {
    // Shrink badge to nothing before animating it's expansion back to original
    // scale.
    self.transform = CGAffineTransformMakeScale(0, 0);
    changeBadgeDisplay();
    [UIView animateWithDuration:kButtonAnimationDuration
                     animations:^{
                       self.transform = CGAffineTransformIdentity;
                     }];

  } else {
    changeBadgeDisplay();
  }
}

@end
