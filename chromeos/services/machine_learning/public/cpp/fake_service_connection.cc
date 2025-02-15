// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/machine_learning/public/cpp/fake_service_connection.h"

#include <utility>

namespace chromeos {
namespace machine_learning {

FakeServiceConnectionImpl::FakeServiceConnectionImpl()
    : execute_result_(mojom::Tensor::New()) {}

FakeServiceConnectionImpl::~FakeServiceConnectionImpl() {}

void FakeServiceConnectionImpl::LoadModel(
    mojom::ModelSpecPtr spec,
    mojom::ModelRequest request,
    mojom::MachineLearningService::LoadModelCallback callback) {
  model_bindings_.AddBinding(this, std::move(request));
  std::move(callback).Run(mojom::LoadModelResult::OK);
}

void FakeServiceConnectionImpl::CreateGraphExecutor(
    mojom::GraphExecutorRequest request,
    mojom::Model::CreateGraphExecutorCallback callback) {
  graph_bindings_.AddBinding(this, std::move(request));
  std::move(callback).Run(mojom::CreateGraphExecutorResult::OK);
}

void FakeServiceConnectionImpl::Execute(
    base::flat_map<std::string, mojom::TensorPtr> inputs,
    const std::vector<std::string>& output_names,
    mojom::GraphExecutor::ExecuteCallback callback) {
  std::vector<mojom::TensorPtr> output_tensors;
  output_tensors.push_back(execute_result_.Clone());
  std::move(callback).Run(mojom::ExecuteResult::OK, std::move(output_tensors));
}

void FakeServiceConnectionImpl::SetOutputValue(
    const std::vector<int64_t>& shape,
    const std::vector<double>& value) {
  execute_result_->shape = mojom::Int64List::New();
  execute_result_->shape->value = shape;
  execute_result_->data = mojom::ValueList::New();
  execute_result_->data->set_float_list(mojom::FloatList::New());
  execute_result_->data->get_float_list()->value = value;
}

}  // namespace machine_learning
}  // namespace chromeos
