// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been auto-generated from the Jinja2 template
// third_party/blink/renderer/bindings/templates/callback_function.h.tmpl
// by the script code_generator_v8.py.
// DO NOT MODIFY!

// clang-format off
#ifndef THIRD_PARTY_BLINK_RENDERER_BINDINGS_TESTS_RESULTS_CORE_V8_ANY_CALLBACK_FUNCTION_VARIADIC_ANY_ARGS_H_
#define THIRD_PARTY_BLINK_RENDERER_BINDINGS_TESTS_RESULTS_CORE_V8_ANY_CALLBACK_FUNCTION_VARIADIC_ANY_ARGS_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/bindings/callback_function_base.h"
#include "third_party/blink/renderer/platform/bindings/v8_value_or_script_wrappable_adapter.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"

namespace blink {

class CORE_EXPORT V8AnyCallbackFunctionVariadicAnyArgs final : public CallbackFunctionBase {
 public:
  static V8AnyCallbackFunctionVariadicAnyArgs* Create(v8::Local<v8::Function> callback_function) {
    return MakeGarbageCollected<V8AnyCallbackFunctionVariadicAnyArgs>(callback_function);
  }

  explicit V8AnyCallbackFunctionVariadicAnyArgs(v8::Local<v8::Function> callback_function)
      : CallbackFunctionBase(callback_function) {}
  ~V8AnyCallbackFunctionVariadicAnyArgs() override = default;

  // NameClient overrides:
  const char* NameInHeapSnapshot() const override;

  // Performs "invoke".
  // https://heycam.github.io/webidl/#es-invoking-callback-functions
  v8::Maybe<ScriptValue> Invoke(bindings::V8ValueOrScriptWrappableAdapter callback_this_value, const Vector<ScriptValue>& arguments) WARN_UNUSED_RESULT;

  // Performs "construct".
  // https://heycam.github.io/webidl/#construct-a-callback-function
  v8::Maybe<ScriptValue> Construct(const Vector<ScriptValue>& arguments) WARN_UNUSED_RESULT;
};

template <>
class V8PersistentCallbackFunction<V8AnyCallbackFunctionVariadicAnyArgs> final : public V8PersistentCallbackFunctionBase {
  using V8CallbackFunction = V8AnyCallbackFunctionVariadicAnyArgs;

 public:
  explicit V8PersistentCallbackFunction(V8CallbackFunction* callback_function)
      : V8PersistentCallbackFunctionBase(callback_function) {}
  ~V8PersistentCallbackFunction() override = default;

  // Returns a wrapper-tracing version of this callback function.
  V8CallbackFunction* ToNonV8Persistent() { return Proxy(); }

  v8::Maybe<ScriptValue> Invoke(bindings::V8ValueOrScriptWrappableAdapter callback_this_value, const Vector<ScriptValue>& arguments) WARN_UNUSED_RESULT;

 private:
  V8CallbackFunction* Proxy() {
    return As<V8CallbackFunction>();
  }

  template <typename V8CallbackFunction>
  friend V8PersistentCallbackFunction<V8CallbackFunction>*
  ToV8PersistentCallbackFunction(V8CallbackFunction*);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_BINDINGS_TESTS_RESULTS_CORE_V8_ANY_CALLBACK_FUNCTION_VARIADIC_ANY_ARGS_H_
