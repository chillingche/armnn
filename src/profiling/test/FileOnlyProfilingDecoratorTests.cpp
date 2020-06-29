//
// Copyright © 2019 Arm Ltd and Contributors. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include <Filesystem.hpp>
#include <LabelsAndEventClasses.hpp>
#include <ProfilingService.hpp>
#include "ProfilingTestUtils.hpp"
#include "PrintPacketHeaderHandler.hpp"
#include <Runtime.hpp>
#include "TestTimelinePacketHandler.hpp"

#include <boost/numeric/conversion/cast.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <sstream>
#include <sys/stat.h>

using namespace armnn::profiling;
using namespace armnn;

using namespace std::chrono_literals;

class FileOnlyHelperService : public ProfilingService
{
    public:
    // Wait for a notification from the send thread
    bool WaitForPacketsSent(uint32_t timeout = 1000)
    {
        return ProfilingService::WaitForPacketSent(m_ProfilingService, timeout);
    }
    armnn::profiling::ProfilingService m_ProfilingService;
};

BOOST_AUTO_TEST_SUITE(FileOnlyProfilingDecoratorTests)

BOOST_AUTO_TEST_CASE(TestFileOnlyProfiling)
{
    // This test requires at least one backend registry to be enabled
    // which can execute a NormalizationLayer
    if (!HasSuitableBackendRegistered())
    {
        return;
    }

    // Enable m_FileOnly but also provide ILocalPacketHandler which should consume the packets.
    // This won't dump anything to file.
    armnn::Runtime::CreationOptions creationOptions;
    creationOptions.m_ProfilingOptions.m_EnableProfiling     = true;
    creationOptions.m_ProfilingOptions.m_FileOnly            = true;
    creationOptions.m_ProfilingOptions.m_CapturePeriod       = 100;
    creationOptions.m_ProfilingOptions.m_TimelineEnabled     = true;
    ILocalPacketHandlerSharedPtr localPacketHandlerPtr = std::make_shared<TestTimelinePacketHandler>();
    creationOptions.m_ProfilingOptions.m_LocalPacketHandlers.push_back(localPacketHandlerPtr);

    armnn::Runtime runtime(creationOptions);
    // ensure the GUID generator is reset to zero
    GetProfilingService(&runtime).ResetGuidGenerator();

    // Load a simple network
    // build up the structure of the network
    INetworkPtr net(INetwork::Create());

    IConnectableLayer* input = net->AddInputLayer(0, "input");

    ElementwiseUnaryDescriptor descriptor(UnaryOperation::Sqrt);
    IConnectableLayer* normalize = net->AddElementwiseUnaryLayer(descriptor, "normalization");

    IConnectableLayer* output = net->AddOutputLayer(0, "output");

    input->GetOutputSlot(0).Connect(normalize->GetInputSlot(0));
    normalize->GetOutputSlot(0).Connect(output->GetInputSlot(0));

    input->GetOutputSlot(0).SetTensorInfo(TensorInfo({ 1, 1, 4, 4 }, DataType::Float32));
    normalize->GetOutputSlot(0).SetTensorInfo(TensorInfo({ 1, 1, 4, 4 }, DataType::Float32));

    // optimize the network
    std::vector<armnn::BackendId> backends =
            { armnn::Compute::CpuRef, armnn::Compute::CpuAcc, armnn::Compute::GpuAcc };
    IOptimizedNetworkPtr optNet = Optimize(*net, backends, runtime.GetDeviceSpec());

    // Load it into the runtime. It should succeed.
    armnn::NetworkId netId;
    BOOST_TEST(runtime.LoadNetwork(netId, std::move(optNet)) == Status::Success);

    // Creates structures for input & output.
    std::vector<float> inputData(16);
    std::vector<float> outputData(16);
    for (unsigned int i = 0; i < 16; ++i)
    {
        inputData[i] = 9.0;
        outputData[i] = 3.0;
    }

    InputTensors  inputTensors
    {
        {0, ConstTensor(runtime.GetInputTensorInfo(netId, 0), inputData.data())}
    };
    OutputTensors outputTensors
    {
        {0, Tensor(runtime.GetOutputTensorInfo(netId, 0), outputData.data())}
    };

    // Does the inference.
    runtime.EnqueueWorkload(netId, inputTensors, outputTensors);

    static_cast<TestTimelinePacketHandler*>(localPacketHandlerPtr.get())->WaitOnInferenceCompletion(3000);

    const TimelineModel& model =
        static_cast<TestTimelinePacketHandler*>(localPacketHandlerPtr.get())->GetTimelineModel();

    for (auto& error : model.GetErrors())
    {
        std::cout << error.what() << std::endl;
    }
    BOOST_TEST(model.GetErrors().empty());
    std::vector<std::string> desc = GetModelDescription(model);
    std::vector<std::string> expectedOutput;
    expectedOutput.push_back("Entity [0] name = input type = layer");
    expectedOutput.push_back("   connection [14] from entity [0] to entity [1]");
    expectedOutput.push_back("   child: Entity [23] backendId = CpuRef type = workload");
    expectedOutput.push_back("Entity [1] name = normalization type = layer");
    expectedOutput.push_back("   connection [22] from entity [1] to entity [2]");
    expectedOutput.push_back("   child: Entity [15] backendId = CpuRef type = workload");
    expectedOutput.push_back("Entity [2] name = output type = layer");
    expectedOutput.push_back("   child: Entity [27] backendId = CpuRef type = workload");
    expectedOutput.push_back("Entity [6] type = network");
    expectedOutput.push_back("   child: Entity [0] name = input type = layer");
    expectedOutput.push_back("   child: Entity [1] name = normalization type = layer");
    expectedOutput.push_back("   child: Entity [2] name = output type = layer");
    expectedOutput.push_back("   execution: Entity [31] type = inference");
    expectedOutput.push_back("Entity [15] backendId = CpuRef type = workload");
    expectedOutput.push_back("   execution: Entity [44] type = workload_execution");
    expectedOutput.push_back("Entity [23] backendId = CpuRef type = workload");
    expectedOutput.push_back("   execution: Entity [36] type = workload_execution");
    expectedOutput.push_back("Entity [27] backendId = CpuRef type = workload");
    expectedOutput.push_back("   execution: Entity [52] type = workload_execution");
    expectedOutput.push_back("Entity [31] type = inference");
    expectedOutput.push_back("   child: Entity [36] type = workload_execution");
    expectedOutput.push_back("   child: Entity [44] type = workload_execution");
    expectedOutput.push_back("   child: Entity [52] type = workload_execution");
    expectedOutput.push_back("   event: [34] class [start_of_life]");
    expectedOutput.push_back("   event: [60] class [end_of_life]");
    expectedOutput.push_back("Entity [36] type = workload_execution");
    expectedOutput.push_back("   event: [40] class [start_of_life]");
    expectedOutput.push_back("   event: [42] class [end_of_life]");
    expectedOutput.push_back("Entity [44] type = workload_execution");
    expectedOutput.push_back("   event: [48] class [start_of_life]");
    expectedOutput.push_back("   event: [50] class [end_of_life]");
    expectedOutput.push_back("Entity [52] type = workload_execution");
    expectedOutput.push_back("   event: [56] class [start_of_life]");
    expectedOutput.push_back("   event: [58] class [end_of_life]");
    BOOST_TEST(CompareOutput(desc, expectedOutput));
}

BOOST_AUTO_TEST_CASE(DumpOutgoingValidFileEndToEnd)
{
    // This test requires at least one backend registry to be enabled
    // which can execute a NormalizationLayer
    if (!HasSuitableBackendRegistered())
    {
        return;
    }

    // Create a temporary file name.
    fs::path tempPath = armnnUtils::Filesystem::NamedTempFile("DumpOutgoingValidFileEndToEnd_CaptureFile.txt");
    armnn::Runtime::CreationOptions options;
    options.m_ProfilingOptions.m_EnableProfiling     = true;
    options.m_ProfilingOptions.m_FileOnly            = true;
    options.m_ProfilingOptions.m_IncomingCaptureFile = "";
    options.m_ProfilingOptions.m_OutgoingCaptureFile = tempPath.string();
    options.m_ProfilingOptions.m_CapturePeriod       = 100;
    options.m_ProfilingOptions.m_TimelineEnabled     = true;

    ILocalPacketHandlerSharedPtr localPacketHandlerPtr = std::make_shared<TestTimelinePacketHandler>();
    options.m_ProfilingOptions.m_LocalPacketHandlers.push_back(localPacketHandlerPtr);

    // Make sure the file does not exist at this point
    BOOST_CHECK(!fs::exists(tempPath));

    armnn::Runtime runtime(options);
    // ensure the GUID generator is reset to zero
    GetProfilingService(&runtime).ResetGuidGenerator();

    // Load a simple network
    // build up the structure of the network
    INetworkPtr net(INetwork::Create());

    IConnectableLayer* input = net->AddInputLayer(0, "input");

    ElementwiseUnaryDescriptor descriptor(UnaryOperation::Sqrt);
    IConnectableLayer* normalize = net->AddElementwiseUnaryLayer(descriptor, "normalization");

    IConnectableLayer* output = net->AddOutputLayer(0, "output");

    input->GetOutputSlot(0).Connect(normalize->GetInputSlot(0));
    normalize->GetOutputSlot(0).Connect(output->GetInputSlot(0));

    input->GetOutputSlot(0).SetTensorInfo(TensorInfo({ 1, 1, 4, 4 }, DataType::Float32));
    normalize->GetOutputSlot(0).SetTensorInfo(TensorInfo({ 1, 1, 4, 4 }, DataType::Float32));

    // optimize the network
    std::vector<armnn::BackendId> backends =
            { armnn::Compute::CpuRef, armnn::Compute::CpuAcc, armnn::Compute::GpuAcc };
    IOptimizedNetworkPtr optNet = Optimize(*net, backends, runtime.GetDeviceSpec());

    // Load it into the runtime. It should succeed.
    armnn::NetworkId netId;
    BOOST_TEST(runtime.LoadNetwork(netId, std::move(optNet)) == Status::Success);

    // Creates structures for input & output.
    std::vector<float> inputData(16);
    std::vector<float> outputData(16);
    for (unsigned int i = 0; i < 16; ++i)
    {
        inputData[i] = 9.0;
        outputData[i] = 3.0;
    }

    InputTensors  inputTensors
    {
        {0, ConstTensor(runtime.GetInputTensorInfo(netId, 0), inputData.data())}
    };
    OutputTensors outputTensors
    {
        {0, Tensor(runtime.GetOutputTensorInfo(netId, 0), outputData.data())}
    };

    // Does the inference.
    runtime.EnqueueWorkload(netId, inputTensors, outputTensors);

    static_cast<TestTimelinePacketHandler*>(localPacketHandlerPtr.get())->WaitOnInferenceCompletion(3000);

    // In order to flush the files we need to gracefully close the profiling service.
    options.m_ProfilingOptions.m_EnableProfiling = false;
    GetProfilingService(&runtime).ResetExternalProfilingOptions(options.m_ProfilingOptions, true);

    // The output file size should be greater than 0.
    BOOST_CHECK(fs::file_size(tempPath) > 0);

    // NOTE: would be an interesting exercise to take this file and decode it

    // Delete the tmp file.
    BOOST_CHECK(fs::remove(tempPath));
}

BOOST_AUTO_TEST_SUITE_END()
