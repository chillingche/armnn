//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "../CommandHandlerKey.hpp"
#include "../CommandHandlerFunctor.hpp"
#include "../CommandHandlerRegistry.hpp"
#include "../EncodeVersion.hpp"
#include "../Holder.hpp"
#include "../Packet.hpp"
#include "../PacketVersionResolver.hpp"
#include "../ProfilingStateMachine.hpp"

#include "../ProfilingService.hpp"

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <thread>

BOOST_AUTO_TEST_SUITE(ExternalProfiling)

using namespace armnn::profiling;

BOOST_AUTO_TEST_CASE(CheckCommandHandlerKeyComparisons)
{
    CommandHandlerKey testKey0(1, 1);
    CommandHandlerKey testKey1(1, 1);
    CommandHandlerKey testKey2(1, 1);
    CommandHandlerKey testKey3(0, 0);
    CommandHandlerKey testKey4(2, 2);
    CommandHandlerKey testKey5(0, 2);

    BOOST_CHECK(testKey1<testKey4);
    BOOST_CHECK(testKey1>testKey3);
    BOOST_CHECK(testKey1<=testKey4);
    BOOST_CHECK(testKey1>=testKey3);
    BOOST_CHECK(testKey1<=testKey2);
    BOOST_CHECK(testKey1>=testKey2);
    BOOST_CHECK(testKey1==testKey2);
    BOOST_CHECK(testKey1==testKey1);

    BOOST_CHECK(!(testKey1==testKey5));
    BOOST_CHECK(!(testKey1!=testKey1));
    BOOST_CHECK(testKey1!=testKey5);

    BOOST_CHECK(testKey1==testKey2 && testKey2==testKey1);
    BOOST_CHECK(testKey0==testKey1 && testKey1==testKey2 && testKey0==testKey2);

    BOOST_CHECK(testKey1.GetPacketId()==1);
    BOOST_CHECK(testKey1.GetVersion()==1);

    std::vector<CommandHandlerKey> vect =
    {
        CommandHandlerKey(0,1), CommandHandlerKey(2,0), CommandHandlerKey(1,0),
        CommandHandlerKey(2,1), CommandHandlerKey(1,1), CommandHandlerKey(0,1),
        CommandHandlerKey(2,0), CommandHandlerKey(0,0)
    };

    std::sort(vect.begin(), vect.end());

    std::vector<CommandHandlerKey> expectedVect =
    {
        CommandHandlerKey(0,0), CommandHandlerKey(0,1), CommandHandlerKey(0,1),
        CommandHandlerKey(1,0), CommandHandlerKey(1,1), CommandHandlerKey(2,0),
        CommandHandlerKey(2,0), CommandHandlerKey(2,1)
    };

    BOOST_CHECK(vect == expectedVect);
}

BOOST_AUTO_TEST_CASE(CheckEncodeVersion)
{
    Version version1(12);

    BOOST_CHECK(version1.GetMajor() == 0);
    BOOST_CHECK(version1.GetMinor() == 0);
    BOOST_CHECK(version1.GetPatch() == 12);

    Version version2(4108);

    BOOST_CHECK(version2.GetMajor() == 0);
    BOOST_CHECK(version2.GetMinor() == 1);
    BOOST_CHECK(version2.GetPatch() == 12);

    Version version3(4198412);

    BOOST_CHECK(version3.GetMajor() == 1);
    BOOST_CHECK(version3.GetMinor() == 1);
    BOOST_CHECK(version3.GetPatch() == 12);

    Version version4(0);

    BOOST_CHECK(version4.GetMajor() == 0);
    BOOST_CHECK(version4.GetMinor() == 0);
    BOOST_CHECK(version4.GetPatch() == 0);

    Version version5(1, 0, 0);
    BOOST_CHECK(version5.GetEncodedValue() == 4194304);
}

BOOST_AUTO_TEST_CASE(CheckPacketClass)
{
    const char* data = "test";
    unsigned int length = static_cast<unsigned int>(std::strlen(data));

    Packet packetTest1(472580096,length,data);
    BOOST_CHECK_THROW(Packet packetTest2(472580096,0,""), armnn::Exception);

    Packet packetTest3(472580096,0, nullptr);

    BOOST_CHECK(packetTest1.GetLength() == length);
    BOOST_CHECK(packetTest1.GetData() == data);

    BOOST_CHECK(packetTest1.GetPacketFamily() == 7);
    BOOST_CHECK(packetTest1.GetPacketId() == 43);
    BOOST_CHECK(packetTest1.GetPacketType() == 3);
    BOOST_CHECK(packetTest1.GetPacketClass() == 5);
}

// Create Derived Classes
class TestFunctorA : public CommandHandlerFunctor
{
public:
    using CommandHandlerFunctor::CommandHandlerFunctor;

    int GetCount() { return m_Count; }

    void operator()(const Packet& packet) override
    {
        m_Count++;
    }

private:
    int m_Count = 0;
};

class TestFunctorB : public TestFunctorA
{
    using TestFunctorA::TestFunctorA;
};

class TestFunctorC : public TestFunctorA
{
    using TestFunctorA::TestFunctorA;
};

BOOST_AUTO_TEST_CASE(CheckCommandHandlerFunctor)
{
    // Hard code the version as it will be the same during a single profiling session
    uint32_t version = 1;

    TestFunctorA testFunctorA(461, version);
    TestFunctorB testFunctorB(963, version);
    TestFunctorC testFunctorC(983, version);

    CommandHandlerKey keyA(testFunctorA.GetPacketId(), testFunctorA.GetVersion());
    CommandHandlerKey keyB(testFunctorB.GetPacketId(), testFunctorB.GetVersion());
    CommandHandlerKey keyC(testFunctorC.GetPacketId(), testFunctorC.GetVersion());

    // Create the unwrapped map to simulate the Command Handler Registry
    std::map<CommandHandlerKey, CommandHandlerFunctor*> registry;

    registry.insert(std::make_pair(keyB, &testFunctorB));
    registry.insert(std::make_pair(keyA, &testFunctorA));
    registry.insert(std::make_pair(keyC, &testFunctorC));

    // Check the order of the map is correct
    auto it = registry.begin();
    BOOST_CHECK(it->first==keyA);
    it++;
    BOOST_CHECK(it->first==keyB);
    it++;
    BOOST_CHECK(it->first==keyC);

    Packet packetA(500000000, 0, nullptr);
    Packet packetB(600000000, 0, nullptr);
    Packet packetC(400000000, 0, nullptr);

    // Check the correct operator of derived class is called
    registry.at(CommandHandlerKey(packetA.GetPacketId(), version))->operator()(packetA);
    BOOST_CHECK(testFunctorA.GetCount() == 1);
    BOOST_CHECK(testFunctorB.GetCount() == 0);
    BOOST_CHECK(testFunctorC.GetCount() == 0);

    registry.at(CommandHandlerKey(packetB.GetPacketId(), version))->operator()(packetB);
    BOOST_CHECK(testFunctorA.GetCount() == 1);
    BOOST_CHECK(testFunctorB.GetCount() == 1);
    BOOST_CHECK(testFunctorC.GetCount() == 0);

    registry.at(CommandHandlerKey(packetC.GetPacketId(), version))->operator()(packetC);
    BOOST_CHECK(testFunctorA.GetCount() == 1);
    BOOST_CHECK(testFunctorB.GetCount() == 1);
    BOOST_CHECK(testFunctorC.GetCount() == 1);
}

BOOST_AUTO_TEST_CASE(CheckCommandHandlerRegistry)
{
    // Hard code the version as it will be the same during a single profiling session
    uint32_t version = 1;

    TestFunctorA testFunctorA(461, version);
    TestFunctorB testFunctorB(963, version);
    TestFunctorC testFunctorC(983, version);

    // Create the Command Handler Registry
    CommandHandlerRegistry registry;

    // Register multiple different derived classes
    registry.RegisterFunctor(&testFunctorA, testFunctorA.GetPacketId(), testFunctorA.GetVersion());
    registry.RegisterFunctor(&testFunctorB, testFunctorB.GetPacketId(), testFunctorB.GetVersion());
    registry.RegisterFunctor(&testFunctorC, testFunctorC.GetPacketId(), testFunctorC.GetVersion());

    Packet packetA(500000000, 0, nullptr);
    Packet packetB(600000000, 0, nullptr);
    Packet packetC(400000000, 0, nullptr);

    // Check the correct operator of derived class is called
    registry.GetFunctor(packetA.GetPacketId(), version)->operator()(packetA);
    BOOST_CHECK(testFunctorA.GetCount() == 1);
    BOOST_CHECK(testFunctorB.GetCount() == 0);
    BOOST_CHECK(testFunctorC.GetCount() == 0);

    registry.GetFunctor(packetB.GetPacketId(), version)->operator()(packetB);
    BOOST_CHECK(testFunctorA.GetCount() == 1);
    BOOST_CHECK(testFunctorB.GetCount() == 1);
    BOOST_CHECK(testFunctorC.GetCount() == 0);

    registry.GetFunctor(packetC.GetPacketId(), version)->operator()(packetC);
    BOOST_CHECK(testFunctorA.GetCount() == 1);
    BOOST_CHECK(testFunctorB.GetCount() == 1);
    BOOST_CHECK(testFunctorC.GetCount() == 1);

    // Re-register an existing key with a new function
    registry.RegisterFunctor(&testFunctorC, testFunctorA.GetPacketId(), version);
    registry.GetFunctor(packetA.GetPacketId(), version)->operator()(packetC);
    BOOST_CHECK(testFunctorA.GetCount() == 1);
    BOOST_CHECK(testFunctorB.GetCount() == 1);
    BOOST_CHECK(testFunctorC.GetCount() == 2);

    // Check that non-existent key returns nullptr for its functor
    BOOST_CHECK_THROW(registry.GetFunctor(0, 0), armnn::Exception);
}

BOOST_AUTO_TEST_CASE(CheckPacketVersionResolver)
{
    // Set up random number generator for generating packetId values
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<uint32_t> distribution(std::numeric_limits<uint32_t>::min(),
                                                         std::numeric_limits<uint32_t>::max());

    // NOTE: Expected version is always 1.0.0, regardless of packetId
    const Version expectedVersion(1, 0, 0);

    PacketVersionResolver packetVersionResolver;

    constexpr unsigned int numTests = 10u;

    for (unsigned int i = 0u; i < numTests; ++i)
    {
        const uint32_t packetId = distribution(generator);
        Version resolvedVersion = packetVersionResolver.ResolvePacketVersion(packetId);

        BOOST_TEST(resolvedVersion == expectedVersion);
    }
}
void ProfilingCurrentStateThreadImpl(ProfilingStateMachine& states)
{
    ProfilingState newState = ProfilingState::NotConnected;
    states.GetCurrentState();
    states.TransitionToState(newState);
}

BOOST_AUTO_TEST_CASE(CheckProfilingStateMachine)
{
    ProfilingStateMachine profilingState1(ProfilingState::Uninitialised);
    profilingState1.TransitionToState(ProfilingState::Uninitialised);
    BOOST_CHECK(profilingState1.GetCurrentState() ==  ProfilingState::Uninitialised);

    ProfilingStateMachine profilingState2(ProfilingState::Uninitialised);
    profilingState2.TransitionToState(ProfilingState::NotConnected);
    BOOST_CHECK(profilingState2.GetCurrentState() == ProfilingState::NotConnected);

    ProfilingStateMachine profilingState3(ProfilingState::NotConnected);
    profilingState3.TransitionToState(ProfilingState::NotConnected);
    BOOST_CHECK(profilingState3.GetCurrentState() == ProfilingState::NotConnected);

    ProfilingStateMachine profilingState4(ProfilingState::NotConnected);
    profilingState4.TransitionToState(ProfilingState::WaitingForAck);
    BOOST_CHECK(profilingState4.GetCurrentState() == ProfilingState::WaitingForAck);

    ProfilingStateMachine profilingState5(ProfilingState::WaitingForAck);
    profilingState5.TransitionToState(ProfilingState::WaitingForAck);
    BOOST_CHECK(profilingState5.GetCurrentState() == ProfilingState::WaitingForAck);

    ProfilingStateMachine profilingState6(ProfilingState::WaitingForAck);
    profilingState6.TransitionToState(ProfilingState::Active);
    BOOST_CHECK(profilingState6.GetCurrentState() == ProfilingState::Active);

    ProfilingStateMachine profilingState7(ProfilingState::Active);
    profilingState7.TransitionToState(ProfilingState::NotConnected);
    BOOST_CHECK(profilingState7.GetCurrentState() == ProfilingState::NotConnected);

    ProfilingStateMachine profilingState8(ProfilingState::Active);
    profilingState8.TransitionToState(ProfilingState::Active);
    BOOST_CHECK(profilingState8.GetCurrentState() == ProfilingState::Active);

    ProfilingStateMachine profilingState9(ProfilingState::Uninitialised);
    BOOST_CHECK_THROW(profilingState9.TransitionToState(ProfilingState::WaitingForAck),
                      armnn::Exception);

    ProfilingStateMachine profilingState10(ProfilingState::Uninitialised);
    BOOST_CHECK_THROW(profilingState10.TransitionToState(ProfilingState::Active),
                      armnn::Exception);

    ProfilingStateMachine profilingState11(ProfilingState::NotConnected);
    BOOST_CHECK_THROW(profilingState11.TransitionToState(ProfilingState::Uninitialised),
                      armnn::Exception);

    ProfilingStateMachine profilingState12(ProfilingState::NotConnected);
    BOOST_CHECK_THROW(profilingState12.TransitionToState(ProfilingState::Active),
                      armnn::Exception);

    ProfilingStateMachine profilingState13(ProfilingState::WaitingForAck);
    BOOST_CHECK_THROW(profilingState13.TransitionToState(ProfilingState::Uninitialised),
                      armnn::Exception);

    ProfilingStateMachine profilingState14(ProfilingState::WaitingForAck);
    BOOST_CHECK_THROW(profilingState14.TransitionToState(ProfilingState::NotConnected),
                      armnn::Exception);

    ProfilingStateMachine profilingState15(ProfilingState::Active);
    BOOST_CHECK_THROW(profilingState15.TransitionToState(ProfilingState::Uninitialised),
                      armnn::Exception);

    ProfilingStateMachine profilingState16(armnn::profiling::ProfilingState::Active);
    BOOST_CHECK_THROW(profilingState16.TransitionToState(ProfilingState::WaitingForAck),
                      armnn::Exception);

    ProfilingStateMachine profilingState17(ProfilingState::Uninitialised);

    std::thread thread1 (ProfilingCurrentStateThreadImpl,std::ref(profilingState17));
    std::thread thread2 (ProfilingCurrentStateThreadImpl,std::ref(profilingState17));
    std::thread thread3 (ProfilingCurrentStateThreadImpl,std::ref(profilingState17));
    std::thread thread4 (ProfilingCurrentStateThreadImpl,std::ref(profilingState17));
    std::thread thread5 (ProfilingCurrentStateThreadImpl,std::ref(profilingState17));

    thread1.join();
    thread2.join();
    thread3.join();
    thread4.join();
    thread5.join();

    BOOST_TEST((profilingState17.GetCurrentState() == ProfilingState::NotConnected));
}

void CaptureDataWriteThreadImpl(Holder &holder, uint32_t capturePeriod, std::vector<uint16_t>& counterIds)
{
    holder.SetCaptureData(capturePeriod, counterIds);
}

void CaptureDataReadThreadImpl(const Holder& holder, CaptureData& captureData)
{
    captureData = holder.GetCaptureData();
}

BOOST_AUTO_TEST_CASE(CheckCaptureDataHolder)
{
    std::map<uint32_t, std::vector<uint16_t>> periodIdMap;
    std::vector<uint16_t> counterIds;
    uint16_t numThreads = 50;
    for (uint16_t i = 0; i < numThreads; ++i)
    {
        counterIds.emplace_back(i);
        periodIdMap.insert(std::make_pair(i, counterIds));
    }

    // Check CaptureData functions
    CaptureData capture;
    BOOST_CHECK(capture.GetCapturePeriod() == 0);
    BOOST_CHECK((capture.GetCounterIds()).empty());
    capture.SetCapturePeriod(0);
    capture.SetCounterIds(periodIdMap[0]);
    BOOST_CHECK(capture.GetCapturePeriod() == 0);
    BOOST_CHECK(capture.GetCounterIds() == periodIdMap[0]);

    Holder holder;
    BOOST_CHECK((holder.GetCaptureData()).GetCapturePeriod() == 0);
    BOOST_CHECK(((holder.GetCaptureData()).GetCounterIds()).empty());

    // Check Holder functions
    std::thread thread1(CaptureDataWriteThreadImpl, std::ref(holder), 2, std::ref(periodIdMap[2]));
    thread1.join();

    BOOST_CHECK((holder.GetCaptureData()).GetCapturePeriod() == 2);
    BOOST_CHECK((holder.GetCaptureData()).GetCounterIds() == periodIdMap[2]);

    CaptureData captureData;
    std::thread thread2(CaptureDataReadThreadImpl, std::ref(holder), std::ref(captureData));
    thread2.join();
    BOOST_CHECK(captureData.GetCounterIds() == periodIdMap[2]);

    std::vector<std::thread> threadsVect;
    for (int i = 0; i < numThreads; i+=2)
    {
        threadsVect.emplace_back(std::thread(CaptureDataWriteThreadImpl,
                                 std::ref(holder),
                                 i,
                                 std::ref(periodIdMap[static_cast<uint16_t >(i)])));

        threadsVect.emplace_back(std::thread(CaptureDataReadThreadImpl,
                                 std::ref(holder),
                                 std::ref(captureData)));
    }

    for (uint16_t i = 0; i < numThreads; ++i)
    {
        threadsVect[i].join();
    }

    std::vector<std::thread> readThreadsVect;
    for (uint16_t i = 0; i < numThreads; ++i)
    {
        readThreadsVect.emplace_back(
                std::thread(CaptureDataReadThreadImpl, std::ref(holder), std::ref(captureData)));
    }

    for (uint16_t i = 0; i < numThreads; ++i)
    {
        readThreadsVect[i].join();
    }

    // Check CaptureData was written/read correctly from multiple threads
    std::vector<uint16_t> captureIds = captureData.GetCounterIds();
    uint32_t capturePeriod = captureData.GetCapturePeriod();

    BOOST_CHECK(captureIds == periodIdMap[capturePeriod]);

    std::vector<uint16_t> readIds = holder.GetCaptureData().GetCounterIds();
    BOOST_CHECK(captureIds == readIds);

    // Check assignment operator
    CaptureData assignableCaptureData;
    assignableCaptureData.SetCapturePeriod(3);
    assignableCaptureData.SetCounterIds(periodIdMap[3]);

    CaptureData secondCaptureData;
    secondCaptureData.SetCapturePeriod(2);
    secondCaptureData.SetCounterIds(periodIdMap[2]);

    BOOST_CHECK(secondCaptureData.GetCapturePeriod() == 2);
    BOOST_CHECK(secondCaptureData.GetCounterIds() == periodIdMap[2]);

    secondCaptureData = assignableCaptureData;
    BOOST_CHECK(secondCaptureData.GetCapturePeriod() == 3);
    BOOST_CHECK(secondCaptureData.GetCounterIds() == periodIdMap[3]);

    // Check copy constructor
    CaptureData copyConstructedCaptureData(assignableCaptureData);

    BOOST_CHECK(copyConstructedCaptureData.GetCapturePeriod() == 3);
    BOOST_CHECK(copyConstructedCaptureData.GetCounterIds() == periodIdMap[3]);
}

BOOST_AUTO_TEST_CASE(CheckProfilingServiceDisabled)
{
    armnn::Runtime::CreationOptions::ExternalProfilingOptions options;
    ProfilingService service(options);
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::Uninitialised);
    service.Run();
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::Uninitialised);
}

BOOST_AUTO_TEST_CASE(CheckProfilingServiceEnabled)
{
    armnn::Runtime::CreationOptions::ExternalProfilingOptions options;
    options.m_EnableProfiling = true;
    ProfilingService service(options);
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::NotConnected);
    service.Run();
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::WaitingForAck);
}


BOOST_AUTO_TEST_CASE(CheckProfilingServiceEnabledRuntime)
{
    armnn::Runtime::CreationOptions::ExternalProfilingOptions options;
    ProfilingService service(options);
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::Uninitialised);
    service.Run();
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::Uninitialised);
    service.m_Options.m_EnableProfiling = true;
    service.Run();
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::NotConnected);
    service.Run();
    BOOST_CHECK(service.GetCurrentState() ==  ProfilingState::WaitingForAck);
}

BOOST_AUTO_TEST_SUITE_END()
