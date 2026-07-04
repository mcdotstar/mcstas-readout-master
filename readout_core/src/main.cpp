#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <thread>
#include <doctest/doctest.h>
#include "efu_time.h"
#include "Readout.h"
#include "ReadoutClass.h"

TEST_CASE("efu_time objects can be made") {
    efu_time a(0, 0);
    auto b = efu_time(0, 0);
    CHECK(a == b);
}

TEST_CASE("efu_time objects can keep subsecond values") {
    efu_time a(0, efu_time::ticks / 2), b(0.99);
    CHECK(a < b);
    auto c = a + a + a;
    CHECK(c.high() == 1);
    auto d = a * 5;
    CHECK(d.high() == 2);
    CHECK(d.low() < efu_time::ticks);
}

TEST_CASE("efu_time checks for over-range input") {
    auto a = std::numeric_limits<uint32_t>::max();
    REQUIRE(a > efu_time::ticks);
    auto t = efu_time(0, a);
    CHECK(t.high() == 48u);
    CHECK(t.low() == a % efu_time::ticks);
}

TEST_CASE("efu_time is now-aware") {
    auto a = efu_time();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    auto b = efu_time();
    CHECK(b > a);
    if (a.low() > b.low()) CHECK(b.high() - a.high() >= 1);
    else CHECK(b.high() - a.high() <= 3);

    auto c = b - a;
    CHECK(c.high() == 2);
}

TEST_CASE("efu_time subtraction is complicated") {
    auto a = efu_time(100, 100);
    auto b = efu_time(0, 0);
    CHECK(a - a == b);
    auto c = efu_time(0, 101);
    auto d = a - c;
    CHECK(d.high() == 99);
    CHECK(d.low() == efu_time::ticks - 1);

    auto g = a - efu_time(0, 1);
    CHECK(g.high() == 100);
    CHECK(g.low() == 99);
}

struct readout{
    void *obj;
    void *rep;
    void *time;
};

TEST_CASE("readout handles time-of-flight correctly") {
    double tof_low{0.120}, tof_high{0.250};  // times in seconds
    const std::string address{"127.0.0.1"};
    constexpr int port{9000}, command_port{8080};
    constexpr double source_frequency{14.0};  // Hz
    const auto readout = readout_create(address.c_str(), port, command_port, source_frequency, DetectorType::BIFROST);

    auto period = static_cast<efu_time*>(readout->rep);
    CHECK(period->high() == 0);
    CHECK(period->low() == static_cast<uint32_t>(efu_time::ticks / source_frequency));
    std::cout << "The period in clock ticks is " << period->low() << " or 1/" << source_frequency << " s\n";

    constexpr auto data = CAEN_readout_t{1, 512, 512, 0, 0};

    auto obj = static_cast<Readout *>(readout->obj);

    // adding a readout _might_ update the reference time. If we're quick enough it shouldn't:
    readout_add(readout, 0u, 0u, 1.0/14.0/2.0, &data);
    auto initial_last = efu_time(obj->lastPulseTime());
    auto initial_prev = efu_time(obj->prevPulseTime());
    readout_add(readout, 0u, 0u, 1.0/14.0/2.0, &data);
    {
        auto last = efu_time(obj->lastPulseTime());
        auto prev = efu_time(obj->prevPulseTime());
        CHECK(last == initial_last);
        CHECK(prev == initial_prev);
    }
    // waiting a pulse period before sending another packet updates the time
    std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<int64_t>(1.0E9/source_frequency)));
    constexpr double half_time{0.5/source_frequency};
    const auto half = efu_time(half_time);
    readout_add(readout, 0u, 0u, half_time, &data);
    {
        auto last = efu_time(obj->lastPulseTime());
        auto prev = efu_time(obj->prevPulseTime());
        CHECK(last == initial_last + period);
        CHECK(prev == initial_prev + period);
        auto event = efu_time(obj->lastEventTime());
        CHECK(event - last == half);
    }

    std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<int64_t>(1.0E9/source_frequency)));
    readout_add(readout, 0u, 0u, tof_low, &data);
    {
        auto last = efu_time(obj->lastPulseTime());
        auto prev = efu_time(obj->prevPulseTime());
        CHECK(last == initial_last + *period * 2);
        CHECK(prev == initial_prev + *period * 2);
        auto event = efu_time(obj->lastEventTime());
        CHECK(event - last == efu_time(tof_low));
        // this behaviour is bad for emulating real-data
        // in a real system all times _must_ follow the source period
        // there is no way for an event time to be more than 1 period
        // after the most recent pulse.
        CHECK(event - last > *period);
    }
    readout_add(readout, 0u, 0u, tof_high, &data);
    {
        auto last = efu_time(obj->lastPulseTime());
        auto prev = efu_time(obj->prevPulseTime());
        CHECK(last == initial_last + *period * 2);
        CHECK(prev == initial_prev + *period * 2);
        auto event = efu_time(obj->lastEventTime());
        CHECK(event - last == efu_time(tof_high));
        // this behaviour is bad for emulating real-data
        // in a real system all times _must_ follow the source period
        // there is no way for an event time to be more than 1 period
        // after the most recent pulse.
        CHECK(event - last > *period * 3);
    }


    // // adding a readout also sets the time...
    // // tof_low == ~1.68 (1/14) s
    // CHECK(tof_low * source_frequency == doctest::Approx(1.67999));
    // // so adding a readout with that time-of-flight _should_ force the reference time to be updated by (1/14)s
    // readout_add(readout, 0u, 0u, tof_low, &data);
    // {
    //     auto pulse_parts = obj->getPulseTime();
    //     CHECK(std::get<0>(pulse_parts) == 100u);
    //     CHECK(std::get<1>(pulse_parts) == 2*period->low());
    //     CHECK(std::get<2>(pulse_parts) == 100u);
    //     CHECK(std::get<3>(pulse_parts) == period->low());
    // }
    //
    // // tof_high == 3.5 (1/14) s
    // CHECK(tof_high * source_frequency == doctest::Approx(3.5));
    // readout_add(readout, 0u, 0u, tof_high, &data);
    //

    readout_destroy(readout);
}