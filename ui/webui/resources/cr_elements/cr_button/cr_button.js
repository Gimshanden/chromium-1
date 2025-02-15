// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'cr-button' is a button which displays slotted elements. It can
 * be interacted with like a normal button using click as well as space and
 * enter to effectively click the button and fire a 'click' event.
 */
Polymer({
  is: 'cr-button',

  behaviors: [
    Polymer.PaperRippleBehavior,
  ],

  properties: {
    disabled: {
      type: Boolean,
      value: false,
      reflectToAttribute: true,
      observer: 'disabledChanged_',
    },
  },

  hostAttributes: {
    'aria-disabled': 'false',
    role: 'button',
    tabindex: 0,
  },

  listeners: {
    click: 'onClick_',
    keydown: 'onKeyDown_',
    keyup: 'onKeyUp_',
    pointerdown: 'onPointerDown_',
  },

  /** @override */
  ready: function() {
    cr.ui.FocusOutlineManager.forDocument(document);
  },

  /**
   * @param {boolean} newValue
   * @param {boolean} oldValue
   * @private
   */
  disabledChanged_: function(newValue, oldValue) {
    if (!newValue && oldValue == undefined) {
      return;
    }
    if (this.disabled) {
      this.blur();
    }
    this.setAttribute('aria-disabled', Boolean(this.disabled));
    this.setAttribute('tabindex', this.disabled ? -1 : 0);
  },

  /**
   * @param {!Event} e
   * @private
   */
  onClick_: function(e) {
    if (this.disabled) {
      e.stopImmediatePropagation();
    }
  },

  /**
   * @param {!KeyboardEvent} e
   * @private
   */
  onKeyDown_: function(e) {
    if (e.key != ' ' && e.key != 'Enter') {
      return;
    }

    e.preventDefault();
    e.stopPropagation();
    if (e.repeat) {
      return;
    }

    if (e.key == 'Enter') {
      this.click();
    } else {
      this.getRipple().uiDownAction();
    }
  },

  /**
   * @param {!KeyboardEvent} e
   * @private
   */
  onKeyUp_: function(e) {
    if (e.key == ' ' || e.key == 'Enter') {
      e.preventDefault();
      e.stopPropagation();
    }

    if (e.key == ' ') {
      this.click();
      this.getRipple().uiUpAction();
    }
  },

  /** @private */
  onPointerDown_: function() {
    this.ensureRipple();
  },
});
