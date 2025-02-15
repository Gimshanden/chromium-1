// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/lock_to_single_user_manager.h"

#include <memory>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/chromeos/settings/scoped_testing_cros_settings.h"
#include "chrome/browser/ui/app_list/arc/arc_app_test.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chromeos/dbus/cryptohome/fake_cryptohome_client.h"
#include "chromeos/login/login_state/login_state.h"
#include "chromeos/login/session/session_termination_manager.h"
#include "chromeos/settings/cros_settings_names.h"
#include "components/account_id/account_id.h"
#include "components/arc/arc_service_manager.h"
#include "components/arc/arc_util.h"
#include "components/arc/test/fake_arc_session.h"
#include "components/policy/proto/chrome_device_policy.pb.h"
#include "components/user_manager/scoped_user_manager.h"

namespace policy {

class LockToSingleUserManagerTest : public BrowserWithTestWindowTest {
 public:
  LockToSingleUserManagerTest() = default;
  ~LockToSingleUserManagerTest() override = default;

  void SetUp() override {
    arc::SetArcAvailableCommandLineForTesting(
        base::CommandLine::ForCurrentProcess());
    chromeos::LoginState::Initialize();
    chromeos::CryptohomeClient::InitializeFake();
    lock_to_single_user_manager_ = std::make_unique<LockToSingleUserManager>();

    BrowserWithTestWindowTest::SetUp();

    settings_helper_.ReplaceDeviceSettingsProviderWithStub();
    arc::ArcSessionManager::SetUiEnabledForTesting(false);
    arc_service_manager_ = std::make_unique<arc::ArcServiceManager>();
    arc_session_manager_ = std::make_unique<arc::ArcSessionManager>(
        std::make_unique<arc::ArcSessionRunner>(
            base::BindRepeating(arc::FakeArcSession::Create)));

    arc_service_manager_->set_browser_context(profile());
  }

  void TearDown() override {
    arc_session_manager_->Shutdown();
    arc_service_manager_->set_browser_context(nullptr);

    BrowserWithTestWindowTest::TearDown();
    arc_session_manager_.reset();
    arc_service_manager_.reset();
    lock_to_single_user_manager_.reset();

    chromeos::CryptohomeClient::Shutdown();
    chromeos::LoginState::Shutdown();
  }

  void LogInUser() {
    const AccountId account_id(AccountId::FromUserEmailGaiaId(
        profile()->GetProfileUserName(), "1234567890"));
    fake_user_manager_->AddUser(account_id);
    fake_user_manager_->LoginUser(account_id);
    chromeos::LoginState::Get()->SetLoggedInState(
        chromeos::LoginState::LOGGED_IN_ACTIVE,
        chromeos::LoginState::LOGGED_IN_USER_REGULAR);

    arc_session_manager_->SetProfile(profile());
    arc_session_manager_->Initialize();
  }

  void SetPolicyValue(int value) {
    settings_helper_.SetInteger(chromeos::kDeviceRebootOnUserSignout, value);
  }

  void StartArc() { arc_session_manager_->StartArcForTesting(); }

  bool is_device_locked() const {
    return chromeos::FakeCryptohomeClient::Get()
        ->is_device_locked_to_single_user();
  }

 private:
  chromeos::ScopedCrosSettingsTestHelper settings_helper_{
      /* create_settings_service= */ false};
  chromeos::FakeChromeUserManager* fake_user_manager_{
      new chromeos::FakeChromeUserManager()};
  user_manager::ScopedUserManager scoped_user_manager_{
      base::WrapUnique(fake_user_manager_)};
  std::unique_ptr<arc::ArcServiceManager> arc_service_manager_;
  std::unique_ptr<arc::ArcSessionManager> arc_session_manager_;
  // Required for initialization.
  chromeos::SessionTerminationManager termination_manager_;
  std::unique_ptr<LockToSingleUserManager> lock_to_single_user_manager_;

  DISALLOW_COPY_AND_ASSIGN(LockToSingleUserManagerTest);
};

TEST_F(LockToSingleUserManagerTest, ArcSessionLockTest) {
  SetPolicyValue(
      enterprise_management::DeviceRebootOnUserSignoutProto::ARC_SESSION);
  LogInUser();
  EXPECT_FALSE(is_device_locked());
  StartArc();
  EXPECT_TRUE(is_device_locked());
}

TEST_F(LockToSingleUserManagerTest, AlwaysLockTest) {
  SetPolicyValue(enterprise_management::DeviceRebootOnUserSignoutProto::ALWAYS);
  LogInUser();
  EXPECT_TRUE(is_device_locked());
}

TEST_F(LockToSingleUserManagerTest, NeverLockTest) {
  SetPolicyValue(enterprise_management::DeviceRebootOnUserSignoutProto::NEVER);
  LogInUser();
  EXPECT_FALSE(is_device_locked());
}

TEST_F(LockToSingleUserManagerTest, DbusCallErrorTest) {
  chromeos::FakeCryptohomeClient::Get()->set_cryptohome_error(
      cryptohome::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  SetPolicyValue(enterprise_management::DeviceRebootOnUserSignoutProto::ALWAYS);
  LogInUser();
  EXPECT_FALSE(is_device_locked());
}

}  // namespace policy
