// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Runs the Sign-in web UI tests. */

// Polymer BrowserTest fixture.
GEN_INCLUDE(['//chrome/test/data/webui/polymer_browser_test_base.js']);
GEN('#include "base/command_line.h"');
GEN('#include "chrome/test/data/webui/signin_browsertest.h"');

/**
 * Test fixture for
 * chrome/browser/resources/signin/dice_sync_confirmation/sync_confirmation.html.
 * This has to be declared as a variable for TEST_F to find it correctly.
 */
SigninSyncConfirmationTest = class extends PolymerTest {
  /** @override */
  get typedefCppFixture() {
    return 'SigninBrowserTest';
  }

  /** @override */
  testGenPreamble() {
    GEN('  EnableUnity();');
  }

  /** @override */
  get browsePreload() {
    return 'chrome://sync-confirmation/sync_confirmation_app.html';
  }

  /** @override */
  get extraLibraries() {
    return [
      ...super.extraLibraries,
      '//chrome/test/data/webui/test_browser_proxy.js',
      '//chrome/browser/resources/signin/dice_sync_confirmation/' +
          'sync_confirmation_browser_proxy.js',
      'test_sync_confirmation_browser_proxy.js',
      'sync_confirmation_test.js',
    ];
  }
};

// TODO(https://crbug.com/862573): Re-enable when no longer failing when
// is_chrome_branded is true.
GEN('#if defined(GOOGLE_CHROME_BUILD)');
GEN('#define MAYBE_DialogWithDice DISABLED_DialogWithDice');
GEN('#else');
GEN('#define MAYBE_DialogWithDice');
GEN('#endif');
TEST_F('SigninSyncConfirmationTest', 'MAYBE_DialogWithDice', function() {
  mocha.run();
});
