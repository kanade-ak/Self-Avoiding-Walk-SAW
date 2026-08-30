#define main moto_probe_optimized_v2_program_main
#include "../src/moto_probe_optimized_v2.cpp"
#undef main

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

namespace {
[[noreturn]] void fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void test_big_uint() {
    BigUInt value;
    value.set_limb(0, std::numeric_limits<std::uint64_t>::max());
    value += BigUInt{1};
    require(value.limb(0) == 0 && value.limb(1) == 1,
            "BigUInt carry into the second limb");

    BigUInt decimal;
    decimal.set_limb(6, 1); // 2^384: verifies output beyond the old limit.
    require(decimal.to_string() ==
                "3940200619639447921227904010014361380507973927046544666794"
                "8293404245721771497210611414266254884915640806627990306816",
            "BigUInt 896-bit decimal conversion");
}

void test_rank_round_trips() {
    std::mt19937_64 random(0x5eed1234ULL);
    for (int n = 1; n <= MAX_N; ++n) {
        StateRanker ranker(n + 2);
        const std::uint32_t universe = ranker.universe_size();
        const std::uint64_t checks =
            universe <= 600'000U ? universe : 100'000U;
        for (std::uint64_t index = 0; index < checks; ++index) {
            const std::uint32_t rank = universe <= 600'000U
                ? static_cast<std::uint32_t>(index)
                : static_cast<std::uint32_t>(random() % universe);
            const StateKey state = ranker.unrank(rank);
            require(ranker.rank(state) == rank,
                    "rank/unrank round trip at n=" + std::to_string(n));
        }
    }
}

void test_ranked_map_pages_and_generations() {
    StateRanker ranker(13); // Universe is large enough for several 4K pages.
    RankedStateMap map(ranker);

    for (std::uint32_t rank = 0; rank < 4096U; ++rank) {
        map.add_one(ranker.unrank(rank));
    }
    require(map.size() == 4096U && map.active_page_count() == 1U,
            "complete 4K rank page");

    const StateKey last_key = ranker.unrank(4095U);
    map.add_one(last_key);
    Count value;
    require(map.copy_value(last_key, value) && value.to_string() == "2",
            "duplicate addition at the last local page index");

    StateKey previous_key = last_key;
    for (int generation = 0; generation < 600; ++generation) {
        map.clear(1);
        const std::uint32_t rank =
            1U + static_cast<std::uint32_t>((generation * 7919U) % 150'000U);
        const StateKey key = ranker.unrank(rank);
        map.add_one(key);
        map.add_one(key);

        Count found;
        require(map.size() == 1 && map.copy_value(key, found) &&
                    found.to_string() == "2",
                "map generation " + std::to_string(generation));
        if (previous_key != key) {
            Count stale;
            require(!map.copy_value(previous_key, stale),
                    "stale map entry after clear");
        }
        previous_key = key;
    }
}

void test_adaptive_promotion() {
    StateRanker ranker(3);
    RankedStateMap current(ranker);
    RankedStateMap next(ranker);
    current.add_one(0);

    for (int exponent = 1; exponent <= 450; ++exponent) {
        next.clear(current.limb_count());
        const auto source = current.ways_at(0, 0);
        next.add(0, source);
        next.add(0, source);
        current.swap(next);

        Count value;
        require(current.copy_value(0, value), "promoted count lookup");
        const std::size_t high_limb = static_cast<std::size_t>(exponent / 64);
        const std::uint64_t high_value =
            std::uint64_t{1} << (exponent % 64);
        for (std::size_t limb = 0; limb < BigUInt::LIMB_COUNT; ++limb) {
            const std::uint64_t expected =
                limb == high_limb ? high_value : 0;
            require(value.limb(limb) == expected,
                    "adaptive promotion at bit " + std::to_string(exponent));
        }
    }
    require(current.limb_count() == 8,
            "adaptive counter grows beyond the former 384-bit limit");
}

void test_known_path_counts() {
    static constexpr std::array<const char*, 14> expected = {
        "1",
        "2",
        "12",
        "184",
        "8512",
        "1262816",
        "575780564",
        "789360053252",
        "3266598486981642",
        "41044208702632496804",
        "1568758030464750013214100",
        "182413291514248049241470885236",
        "64528039343270018963357185158482118",
        "69450664761521361664274701548907358996488",
    };

    for (int n = 0; n < static_cast<int>(expected.size()); ++n) {
        probe_timed_out = false;
        probe_deadline = std::chrono::steady_clock::now() +
                         std::chrono::minutes(2);
        std::size_t peak_states = 0;
        std::uint64_t transitions = 0;
        Count paths = count_paths(n, peak_states, transitions);
        if (n > 0) {
            paths += paths;
        }
        require(!probe_timed_out && paths.to_string() == expected[n],
                "known path count at n=" + std::to_string(n));
    }
}
} // namespace

int main() {
    test_big_uint();
    test_rank_round_trips();
    test_ranked_map_pages_and_generations();
    test_adaptive_promotion();
    test_known_path_counts();
    std::cout << "All adaptive v2 tests passed.\n";
    return 0;
}
