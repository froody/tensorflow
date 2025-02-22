/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "tensorflow/core/data/service/dispatcher_client.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "tensorflow/core/data/service/common.pb.h"
#include "tensorflow/core/data/service/data_transfer.h"
#include "tensorflow/core/data/service/snapshot/path_utils.h"
#include "tensorflow/core/data/service/test_cluster.h"
#include "tensorflow/core/data/service/test_util.h"
#include "tensorflow/core/framework/dataset.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/platform/errors.h"
#include "tensorflow/core/platform/status_matchers.h"
#include "tensorflow/core/platform/statusor.h"
#include "tensorflow/core/platform/test.h"
#include "tensorflow/core/protobuf/data_service.pb.h"
#include "tensorflow/core/protobuf/error_codes.pb.h"
#include "tensorflow/core/protobuf/snapshot.pb.h"
#include "tensorflow/core/protobuf/struct.pb.h"

namespace tensorflow {
namespace data {
namespace {

using ::tensorflow::data::experimental::DistributedSnapshotMetadata;
using ::tensorflow::data::testing::CreateDummyDistributedSnapshotMetadata;
using ::tensorflow::data::testing::EqualsProto;
using ::tensorflow::data::testing::InfiniteDataset;
using ::tensorflow::data::testing::LocalTempFilename;
using ::tensorflow::data::testing::RangeDataset;
using ::tensorflow::testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;

constexpr const char kProtocol[] = "grpc";

DataServiceMetadata GetDefaultMetadata() {
  StructuredValue decoded_spec;
  TensorShapeProto::Dim* dim =
      decoded_spec.mutable_tensor_shape_value()->add_dim();
  dim->set_size(1);
  dim->set_name(absl::StrCat("dim"));

  DataServiceMetadata metadata;
  metadata.set_element_spec(decoded_spec.SerializeAsString());
  metadata.set_compression(DataServiceMetadata::COMPRESSION_SNAPPY);
  metadata.set_cardinality(kUnknownCardinality);
  return metadata;
}

class DispatcherClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_cluster_ = std::make_unique<TestCluster>(/*num_workers=*/1);
    TF_ASSERT_OK(test_cluster_->Initialize());
    dispatcher_client_ = std::make_unique<DataServiceDispatcherClient>(
        test_cluster_->DispatcherAddress(), kProtocol);
  }

  // Creates a dataset and returns the dataset ID.
  StatusOr<std::string> RegisterDataset(
      const DatasetDef& dataset, const DataServiceMetadata& metadata,
      const std::optional<std::string>& requested_dataset_id = std::nullopt) {
    std::string dataset_id;
    TF_RETURN_IF_ERROR(dispatcher_client_->RegisterDataset(
        dataset, metadata, requested_dataset_id, dataset_id));
    return dataset_id;
  }

  // Starts snapshots and returns the directories.
  StatusOr<absl::flat_hash_set<std::string>> StartDummySnapshots() {
    DistributedSnapshotMetadata metadata =
        CreateDummyDistributedSnapshotMetadata();
    // Create a set of local file paths to which snapshots will be materialized.
    absl::flat_hash_set<std::string> directories = {LocalTempFilename(),
                                                    LocalTempFilename()};
    for (const auto& directory : directories) {
      TF_RETURN_IF_ERROR(
          dispatcher_client_->Snapshot(RangeDataset(10), directory, metadata));
    }
    return directories;
  }

  std::unique_ptr<TestCluster> test_cluster_;
  std::unique_ptr<DataServiceDispatcherClient> dispatcher_client_;
};

TEST_F(DispatcherClientTest, GetDataServiceMetadata) {
  DataServiceMetadata metadata = GetDefaultMetadata();
  metadata.set_cardinality(10);
  TF_ASSERT_OK_AND_ASSIGN(const std::string dataset_id,
                          RegisterDataset(RangeDataset(10), metadata));

  DataServiceMetadata result;
  TF_ASSERT_OK(dispatcher_client_->GetDataServiceMetadata(dataset_id, result));
  EXPECT_THAT(result, EqualsProto(metadata));
}

TEST_F(DispatcherClientTest, DatasetDoesNotExist) {
  DataServiceMetadata metadata = GetDefaultMetadata();
  EXPECT_THAT(
      dispatcher_client_->GetDataServiceMetadata(
          /*dataset_id=*/"not-found", metadata),
      StatusIs(error::NOT_FOUND, HasSubstr("Dataset id not-found not found")));
}

TEST_F(DispatcherClientTest, SnapshotAlreadyStarted) {
  DistributedSnapshotMetadata metadata =
      CreateDummyDistributedSnapshotMetadata();
  std::string directory = LocalTempFilename();
  TF_ASSERT_OK(
      dispatcher_client_->Snapshot(RangeDataset(10), directory, metadata));
  EXPECT_THAT(
      dispatcher_client_->Snapshot(RangeDataset(10), directory, metadata),
      StatusIs(error::INVALID_ARGUMENT, HasSubstr("already started")));
}

TEST_F(DispatcherClientTest, GetDataServiceConfig) {
  DataServiceConfig config;
  TF_ASSERT_OK(dispatcher_client_->GetDataServiceConfig(config));
  EXPECT_EQ(config.deployment_mode(), DEPLOYMENT_MODE_COLOCATED);
}

TEST_F(DispatcherClientTest, SnapshotMetadataAndDatasetDefWritten) {
  TF_ASSERT_OK_AND_ASSIGN(absl::flat_hash_set<std::string> directories,
                          StartDummySnapshots());
  for (const auto& directory : directories) {
    TF_ASSERT_OK(Env::Default()->FileExists(
        io::JoinPath(directory, "snapshot.metadata")));
    TF_ASSERT_OK(Env::Default()->FileExists(
        io::JoinPath(directory, "dataset_def.proto")));
  }
}

TEST_F(DispatcherClientTest, CreateCommittedChunksDirectory) {
  TF_ASSERT_OK_AND_ASSIGN(absl::flat_hash_set<std::string> directories,
                          StartDummySnapshots());
  for (const auto& directory : directories) {
    TF_ASSERT_OK(
        Env::Default()->FileExists(CommittedChunksDirectory(directory)));
  }
}

TEST_F(DispatcherClientTest, SnapshotsInHeartbeat) {
  TF_ASSERT_OK_AND_ASSIGN(absl::flat_hash_set<std::string> directories,
                          StartDummySnapshots());
  WorkerHeartbeatRequest worker_heartbeat_request;
  worker_heartbeat_request.set_worker_address(test_cluster_->WorkerAddress(0));
  TF_ASSERT_OK_AND_ASSIGN(
      WorkerHeartbeatResponse worker_heartbeat_response,
      dispatcher_client_->WorkerHeartbeat(worker_heartbeat_request));
  ASSERT_EQ(worker_heartbeat_response.snapshots_size(), directories.size());
  for (const auto& snapshot : worker_heartbeat_response.snapshots()) {
    ASSERT_TRUE(directories.count(snapshot.directory()));
    ASSERT_EQ(snapshot.stream_index(), 0);
  }
}

TEST_F(DispatcherClientTest, GetSnapshotSplit) {
  TF_ASSERT_OK_AND_ASSIGN(absl::flat_hash_set<std::string> directories,
                          StartDummySnapshots());
  WorkerHeartbeatRequest worker_heartbeat_request;
  worker_heartbeat_request.set_worker_address(test_cluster_->WorkerAddress(0));
  TF_ASSERT_OK_AND_ASSIGN(
      WorkerHeartbeatResponse worker_heartbeat_response,
      dispatcher_client_->WorkerHeartbeat(worker_heartbeat_request));
  for (const auto& snapshot : worker_heartbeat_response.snapshots()) {
    GetSnapshotSplitRequest get_snapshot_split_request;
    Tensor split;
    bool end_of_splits;
    TF_ASSERT_OK(dispatcher_client_->GetSnapshotSplit(
        snapshot.directory(), snapshot.stream_index(), /*source_index=*/0,
        split, end_of_splits));
    ASSERT_FALSE(end_of_splits);
  }
}

TEST_F(DispatcherClientTest, RegisterDatasetWithExplicitId) {
  DataServiceMetadata metadata = GetDefaultMetadata();
  metadata.set_cardinality(10);
  TF_ASSERT_OK_AND_ASSIGN(
      const std::string dataset_id1,
      RegisterDataset(RangeDataset(10), metadata,
                      /*requested_dataset_id=*/"dataset_id"));
  EXPECT_EQ(dataset_id1, "dataset_id");

  // Registers a dataset with the same dataset ID.
  TF_ASSERT_OK_AND_ASSIGN(
      const std::string dataset_id2,
      RegisterDataset(RangeDataset(10), metadata,
                      /*requested_dataset_id=*/"dataset_id"));
  EXPECT_EQ(dataset_id1, dataset_id2);
}

TEST_F(DispatcherClientTest, DatasetsDoNotMatch) {
  DataServiceMetadata metadata = GetDefaultMetadata();
  metadata.set_cardinality(10);
  TF_ASSERT_OK_AND_ASSIGN(
      const std::string dataset_id1,
      RegisterDataset(RangeDataset(10), metadata,
                      /*requested_dataset_id=*/"dataset_id"));
  EXPECT_EQ(dataset_id1, "dataset_id");

  // Registers a dataset with the same dataset ID but different metadata.
  metadata.set_cardinality(kInfiniteCardinality);
  EXPECT_THAT(
      RegisterDataset(InfiniteDataset(), metadata,
                      /*requested_dataset_id=*/"dataset_id"),
      StatusIs(
          error::INVALID_ARGUMENT,
          HasSubstr(
              "Datasets with the same ID should have the same structure")));
}

TEST_F(DispatcherClientTest, EnableCrossTrainerCache) {
  DataServiceMetadata metadata = GetDefaultMetadata();
  metadata.set_cardinality(kInfiniteCardinality);
  TF_ASSERT_OK_AND_ASSIGN(const std::string dataset_id,
                          RegisterDataset(InfiniteDataset(), metadata));

  ProcessingModeDef processing_mode;
  processing_mode.set_sharding_policy(ProcessingModeDef::OFF);
  std::string job_name = "job";
  int64_t job_id;
  TF_ASSERT_OK(dispatcher_client_->GetOrCreateJob(
      dataset_id, processing_mode, job_name,
      /*num_consumers=*/std::nullopt,
      /*use_cross_trainer_cache=*/true, TARGET_WORKERS_AUTO, job_id));
  int64_t iteration_client_id;
  TF_ASSERT_OK(dispatcher_client_->GetOrCreateIteration(
      job_id, /*repetition=*/0, iteration_client_id));

  WorkerHeartbeatRequest worker_heartbeat_request;
  worker_heartbeat_request.set_worker_address(test_cluster_->WorkerAddress(0));
  TF_ASSERT_OK_AND_ASSIGN(
      WorkerHeartbeatResponse worker_heartbeat_response,
      dispatcher_client_->WorkerHeartbeat(worker_heartbeat_request));
  ASSERT_EQ(worker_heartbeat_response.new_tasks_size(), 1);
  EXPECT_TRUE(worker_heartbeat_response.new_tasks(0).use_cross_trainer_cache());
}

TEST_F(DispatcherClientTest, CreateNamedJob) {
  DataServiceMetadata metadata = GetDefaultMetadata();
  metadata.set_cardinality(10);
  TF_ASSERT_OK_AND_ASSIGN(const std::string dataset_id,
                          RegisterDataset(RangeDataset(10), metadata));

  ProcessingModeDef processing_mode;
  processing_mode.set_sharding_policy(ProcessingModeDef::OFF);
  std::string job_name = "job";
  int64_t job_id_1 = -1;
  TF_ASSERT_OK(dispatcher_client_->GetOrCreateJob(
      dataset_id, processing_mode, job_name,
      /*num_consumers=*/std::nullopt,
      /*use_cross_trainer_cache=*/true, TARGET_WORKERS_AUTO, job_id_1));

  int64_t job_id_2 = -2;
  // Creating the same job should succeed and receive the same job id.
  TF_ASSERT_OK(dispatcher_client_->GetOrCreateJob(
      dataset_id, processing_mode, job_name,
      /*num_consumers=*/std::nullopt,
      /*use_cross_trainer_cache=*/true, TARGET_WORKERS_AUTO, job_id_2));
  ASSERT_EQ(job_id_1, job_id_2);
}

TEST_F(DispatcherClientTest, NamedJobsDoNotMatch) {
  DataServiceMetadata metadata = GetDefaultMetadata();
  metadata.set_cardinality(10);
  TF_ASSERT_OK_AND_ASSIGN(const std::string dataset_id,
                          RegisterDataset(RangeDataset(10), metadata));

  int64_t job_id = 0;
  ProcessingModeDef processing_mode;
  processing_mode.set_sharding_policy(ProcessingModeDef::OFF);
  std::string job_name = "job";
  TF_ASSERT_OK(dispatcher_client_->GetOrCreateJob(
      dataset_id, processing_mode, job_name,
      /*num_consumers=*/std::nullopt,
      /*use_cross_trainer_cache=*/false, TARGET_WORKERS_AUTO, job_id));

  // Creating the same iteration with a different argument should fail.
  processing_mode.set_sharding_policy(ProcessingModeDef::DYNAMIC);
  EXPECT_THAT(
      dispatcher_client_->GetOrCreateJob(dataset_id, processing_mode, job_name,
                                         /*num_consumers=*/std::nullopt,
                                         /*use_cross_trainer_cache=*/true,
                                         TARGET_WORKERS_AUTO, job_id),
      StatusIs(error::INVALID_ARGUMENT,
               AllOf(HasSubstr("but found an existing job with different "
                               "parameters: "),
                     HasSubstr("Existing processing mode: <>"),
                     HasSubstr("Existing cross-trainer cache: <disabled>"))));
}
}  // namespace
}  // namespace data
}  // namespace tensorflow
