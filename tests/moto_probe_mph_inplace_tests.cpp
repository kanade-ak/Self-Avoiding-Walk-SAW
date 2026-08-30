#define main moto_probe_mph_inplace_program_main
#include "../src/moto_probe_mph_inplace.cpp"
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

void test_minimal_state_universe() {
    MateCodec mainCodec(21, 11, 1, 0);
    MateCodec deferredCodec(20, 10, 1, 0);
    require(mainCodec.codeSize() == 258'215'664ULL,
            "n=20 main minimal-perfect-hash state count");
    require(deferredCodec.codeSize() == 91'695'540ULL,
            "n=20 deferred minimal-perfect-hash state count");
}

void test_known_path_counts() {
    static constexpr std::array<const char*, 17> expected = {
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
        "227449714676812739631826459327989863387613323440",
        "2266745568862672746374567396713098934866324885408319028",
        "68745445609149931587631563132489232824587945968099457285419306",
    };

    for (int n = 0; n < static_cast<int>(expected.size()); ++n) {
        if (n == 0) {
            require(std::string(expected[n]) == "1", "n=0 path count");
            continue;
        }
        const int gridSize = n + 1;
        PathCounter<Count> counter(gridSize, gridSize);
        Count paths;
        const bool completed = counter.count(
            std::chrono::steady_clock::now() + std::chrono::minutes(5),
            paths);
        require(completed && paths.to_string() == expected[n],
                "known in-place path count at n=" + std::to_string(n));
    }
}
} // namespace

int main() {
    msg = NONE;
    test_minimal_state_universe();
    test_known_path_counts();
    std::cout << "All minimal-perfect-hash in-place tests passed.\n";
    return 0;
}
