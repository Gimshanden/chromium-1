# Consumer User Types

This document gives a brief description of consumer user types that can log in
to a Chrome OS devices.

This does not include enterprise user types. For more information about
enterprise user types, see
*   [Active Directory integration](../enterprise/active_directory_integration.md),
*   [kiosk and public sessions](../enterprise/kiosk_public_session.md),
*   [SAML authentication](../enterprise/saml_authentication.md)

## Regular users

Regular users that were registered using their GAIA account.


## Child users

Users that logged in using
*   a child account - an account designated for children under the age of 13.
*   a Geller account - an account with parental supervision that has no age
    restrictions.

In order to add a child user to the device, the user has to go through an
adapted GAIA flow, which also requires their parent to authenticate.

Child users require a user policy that may, for example, control their device
usage time limits. If a policy cannot be fetched, either from policy server or
local cache, user login attempts will fail.

A child account can be created at
https://families.google.com/signupkid/famlink-kc?e=UnicornFamily.

For internal Google instructions for creating child accounts (including test
accounts), see go/unicorn-test-account-creation.

During child login, user session start will be delayed up to 10 seconds until
the user's policies have been refreshed (using the user's OAuth token). This
might cause issues for tests that include child user login, as it may add
unnecessary time to the test runtime. To avoid this, tests should:
*   Set up `LocalTestServerMixin` to serve a policy for the test user.
    *   This should be done even if a cached policy is set up using
        `UserPolicyMixin`.
    *   Note that `UserPolicyMixin` propagates policy changes to
        `LocalPolicyTestServerMixin` optionally provided in its constructor.
    *   For testing adding child users, directly setting up
        `LocalPolicyTestsServer`, to avoid setting user's cached policy before
        they ever logged in, might be more appropriate.
*   Set up `FakeGaiaMixin` to serve the child user's auth tokens.
    *   This should be done even if the test does not use fake gaia for login,
        as policy fetch will be blocked on OAuth token fetch.
    *   If the test user is logged in using `LoginManagerMixin`, the injected
        `UserContext` has to have the refresh token matching the token passed to
        `FakeGaiaMixin`.

## Guest

Ephemeral, anonymous users that are logged into an incognito session. The user
cryptohome is mounted on tmpfs, and none of the data from the user session is
persisted after the guest session ends.

To test guest session state, use `GaiaSessionMixin` - this will set up
appropriate guest session flags.

Testing guest user login is more complicated, as guest login required Chrome
restart. The test will require two parts:
*   `PRE_BrowserTest` test that requests login
*   `BrowserTest` that can test guest session state

To properly set up and preserve Chrome flags between sessions runs, use
`LoginManagerMixin`, and set it up using
`LoginManagerMixin::set_session_restore_enabled()`

