#define main moto_probe_optimized_v2_fast_program_main
#include "../src/moto_probe_optimized_v2_fast.cpp"
#undef main

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
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

void test_ranked_map() {
    StateRanker ranker(13);
    RankedStateMap map(ranker);
    for (std::uint32_t rank = 0; rank < 65'536U; ++rank) {
        map.add(ranker.unrank(rank), Count{1});
    }
    require(map.size() == 65'536U && map.active_page_count() == 1U,
            "complete 64K rank page");

    const StateKey key = ranker.unrank(65'535U);
    map.add(key, Count{2});
    const Count* value = map.find(key);
    require(value != nullptr && value->to_string() == "3",
            "last local index and duplicate addition");

    for (int generation = 0; generation < 600; ++generation) {
        map.clear();
        const StateKey current = ranker.unrank(
            1U + static_cast<std::uint32_t>((generation * 7919U) % 150'000U));
        map.add(current, Count{1});
        map.add(current, Count{2});
        const Count* found = map.find(current);
        require(map.size() == 1 && found != nullptr &&
                    found->to_string() == "3",
                "fast-map generation " + std::to_string(generation));
    }
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
                "fast known path count at n=" + std::to_string(n));
    }
}
} // namespace

int main() {
    test_ranked_map();
    test_known_path_counts();
    std::cout << "All fast v2 tests passed.\n";
    return 0;
}
