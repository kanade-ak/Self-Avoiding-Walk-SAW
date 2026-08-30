#define MPH_INPLACE_PROGRAM_MAIN moto_probe_mph_inplace_program_main
#include "../src/moto_probe_mph_inplace.cpp"
#undef MPH_INPLACE_PROGRAM_MAIN

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

void test_in_place_growth_preserves_elements() {
    // Large enough that every promotion has to commit additional pages.  This
    // exercises both the VirtualAlloc boundary handling and the overlapping
    // backwards relocation, rather than only moving inside the first page.
    constexpr Code elementCount = 1025;
    growable_mph::CompactLimbArray values(elementCount, 3);
    require(values.logical_bytes() == elementCount * sizeof(std::uint64_t),
            "compact array starts with one physical limb per element");
    require(values.reserved_bytes()
                == elementCount * 3 * sizeof(std::uint64_t),
            "compact array reserves its maximum physical width");

    for (Code index = 0; index < elementCount; ++index) {
        values.at<1>(index) = static_cast<std::uint32_t>(index + 10);
    }
    values.grow();
    require(values.active_limbs() == 2,
            "first compact growth activates two limbs");
    for (Code index = 0; index < elementCount; ++index) {
        const auto limbs = values.at<2>(index).load();
        require(limbs[0] == index + 10 && limbs[1] == 0,
                "first compact growth preserves element "
                    + std::to_string(index));
    }
    constexpr Code changedIndex = elementCount / 2;
    values.at<2>(changedIndex) += std::array<std::uint64_t, 2>{7, 3};
    values.grow();
    require(values.active_limbs() == 3,
            "second compact growth activates three limbs");
    for (Code index = 0; index < elementCount; ++index) {
        const auto grown = values.at<3>(index).load();
        const std::uint64_t expectedLow = index + 10
            + (index == changedIndex ? 7 : 0);
        const std::uint64_t expectedMiddle = index == changedIndex ? 3 : 0;
        require(grown[0] == expectedLow && grown[1] == expectedMiddle
                    && grown[2] == 0,
                "second compact growth preserves element "
                    + std::to_string(index));
    }
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

    for (int n = 1; n < static_cast<int>(expected.size()); ++n) {
        const int gridSize = n + 1;
        growable_mph::GrowablePathCounter counter(
            gridSize, gridSize,
            static_cast<std::size_t>(limbCountForN(n)));
        growable_mph::GrowableResult paths;
        const bool completed = counter.count(
            std::chrono::steady_clock::now() + std::chrono::minutes(5),
            paths);
        require(completed && paths.to_string() == expected[n],
                "known growable path count at n=" + std::to_string(n));
    }
}
} // namespace

int main() {
    msg = NONE;
    test_in_place_growth_preserves_elements();
    test_known_path_counts();
    std::cout << "All compact growable-limb tests passed.\n";
    return 0;
}
