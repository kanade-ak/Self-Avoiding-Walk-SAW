/*
 * Compact growable-limb production implementation for the
 * minimal-perfect-hash in-place DP.
 *
 * The fixed-width baseline is retained under src/experiments and included for
 * the common state codec and transition definitions.  The production counter
 * starts with one 64-bit limb per state and expands the packed arrays in place.
 * Windows/MSVC is intentional: the repository already targets that platform,
 * and reserved virtual memory lets expansion avoid a transient second array.
 */

#ifdef _MSC_VER
#pragma warning(push)
// FixedCount<1> in the included fixed-width baseline intentionally has two
// compile-time-empty upper-limb loops; MSVC /analyze reports C6294 for them.
#pragma warning(disable: 6294)
#endif
#define main moto_probe_mph_inplace_fixed_program_main
#include "experiments/moto_probe_mph_inplace_fixed.cpp"
#undef main
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cmath>
#include <type_traits>

namespace growable_mph {

constexpr std::size_t MAX_LIMBS = 6;

template<std::size_t LIMBS>
class CompactCountRef {
public:
    static_assert(LIMBS >= 1 && LIMBS <= MAX_LIMBS);

    explicit CompactCountRef(std::uint64_t* limbs) noexcept
            : limbs_(limbs) {}

    CompactCountRef& operator=(std::uint32_t value) noexcept {
        limbs_[0] = value;
        if constexpr (LIMBS > 1) {
            for (std::size_t limb = 1; limb < LIMBS; ++limb) {
                limbs_[limb] = 0;
            }
        }
        return *this;
    }

    bool operator==(std::uint32_t value) const noexcept {
        if (limbs_[0] != value) {
            return false;
        }
        if constexpr (LIMBS > 1) {
            for (std::size_t limb = 1; limb < LIMBS; ++limb) {
                if (limbs_[limb] != 0) {
                    return false;
                }
            }
        }
        return true;
    }

    bool operator!=(std::uint32_t value) const noexcept {
        return !(*this == value);
    }

    CompactCountRef& operator+=(const CompactCountRef& other) {
        add(other.limbs_);
        return *this;
    }

    CompactCountRef& operator+=(
            const std::array<std::uint64_t, LIMBS>& other) {
        add(other.data());
        return *this;
    }

    std::array<std::uint64_t, LIMBS> load() const noexcept {
        std::array<std::uint64_t, LIMBS> result{};
        for (std::size_t limb = 0; limb < LIMBS; ++limb) {
            result[limb] = limbs_[limb];
        }
        return result;
    }

    void store(const std::array<std::uint64_t, LIMBS>& value) noexcept {
        for (std::size_t limb = 0; limb < LIMBS; ++limb) {
            limbs_[limb] = value[limb];
        }
    }

private:
    void add(const std::uint64_t* other) {
        unsigned char carry = 0;
        for (std::size_t limb = 0; limb < LIMBS; ++limb) {
            carry = _addcarry_u64(
                carry, limbs_[limb], other[limb], &limbs_[limb]);
        }
        if (carry != 0) {
            throw std::overflow_error(
                "growable in-place count exceeded its active limb width");
        }
    }

    std::uint64_t* limbs_;
};

class CompactLimbArray {
public:
    CompactLimbArray(Code elementCount, std::size_t maximumLimbs)
            : elementCount_(elementCount), maximumLimbs_(maximumLimbs) {
        if (elementCount_ == 0 || maximumLimbs_ < 1 ||
            maximumLimbs_ > MAX_LIMBS) {
            throw std::invalid_argument("invalid compact limb array size");
        }

        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        pageSize_ = static_cast<std::size_t>(systemInfo.dwPageSize);

        const std::size_t reserveBytes = bytesFor(maximumLimbs_);
        data_ = static_cast<std::uint64_t*>(VirtualAlloc(
            nullptr, reserveBytes, MEM_RESERVE, PAGE_NOACCESS));
        if (data_ == nullptr) {
            throw std::bad_alloc();
        }

        committedBytes_ = pageRoundedBytesFor(1);
        if (VirtualAlloc(data_, committedBytes_, MEM_COMMIT, PAGE_READWRITE)
                == nullptr) {
            VirtualFree(data_, 0, MEM_RELEASE);
            data_ = nullptr;
            throw std::bad_alloc();
        }
    }

    CompactLimbArray(const CompactLimbArray&) = delete;
    CompactLimbArray& operator=(const CompactLimbArray&) = delete;

    ~CompactLimbArray() {
        if (data_ != nullptr) {
            VirtualFree(data_, 0, MEM_RELEASE);
        }
    }

    template<std::size_t LIMBS>
    CompactCountRef<LIMBS> at(Code index) const noexcept {
        assert(activeLimbs_ == LIMBS);
        assert(index < elementCount_);
        return CompactCountRef<LIMBS>(
            data_ + static_cast<std::size_t>(index) * LIMBS);
    }

    void grow() {
        if (activeLimbs_ >= maximumLimbs_) {
            throw std::overflow_error("compact limb array cannot grow further");
        }

        const std::size_t oldLimbs = activeLimbs_;
        const std::size_t newLimbs = oldLimbs + 1;
        const std::size_t newCommittedBytes = pageRoundedBytesFor(newLimbs);
        if (newCommittedBytes > committedBytes_) {
            std::byte* const uncommittedStart =
                reinterpret_cast<std::byte*>(data_) + committedBytes_;
            const std::size_t additionalBytes =
                newCommittedBytes - committedBytes_;
            if (VirtualAlloc(uncommittedStart, additionalBytes, MEM_COMMIT,
                             PAGE_READWRITE) == nullptr) {
                throw std::bad_alloc();
            }
            committedBytes_ = newCommittedBytes;
        }

        // This is the same direction rule as an expanding memmove.  Processing
        // high indices first ensures a wider destination never overwrites a
        // source element that has not yet been copied.
        for (Code index = elementCount_; index-- > 0;) {
            std::uint64_t* const source =
                data_ + static_cast<std::size_t>(index) * oldLimbs;
            std::uint64_t* const destination =
                data_ + static_cast<std::size_t>(index) * newLimbs;
            for (std::size_t limb = oldLimbs; limb-- > 0;) {
                destination[limb] = source[limb];
            }
            destination[oldLimbs] = 0;
        }
        activeLimbs_ = newLimbs;
    }

    const std::uint64_t* raw(Code index) const noexcept {
        assert(index < elementCount_);
        return data_ + static_cast<std::size_t>(index) * activeLimbs_;
    }

    std::size_t active_limbs() const noexcept {
        return activeLimbs_;
    }

    std::uint64_t logical_bytes() const noexcept {
        return static_cast<std::uint64_t>(elementCount_)
            * activeLimbs_ * sizeof(std::uint64_t);
    }

    std::uint64_t reserved_bytes() const noexcept {
        return static_cast<std::uint64_t>(elementCount_)
            * maximumLimbs_ * sizeof(std::uint64_t);
    }

private:
    std::size_t bytesFor(std::size_t limbs) const {
        constexpr std::size_t limbBytes = sizeof(std::uint64_t);
        const std::uint64_t bytes = static_cast<std::uint64_t>(elementCount_)
            * static_cast<std::uint64_t>(limbs) * limbBytes;
        if (bytes > (std::numeric_limits<std::size_t>::max)()) {
            throw std::overflow_error("compact limb allocation is too large");
        }
        return static_cast<std::size_t>(bytes);
    }

    std::size_t pageRoundedBytesFor(std::size_t limbs) const {
        const std::size_t bytes = bytesFor(limbs);
        if (bytes > (std::numeric_limits<std::size_t>::max)()
                - (pageSize_ - 1)) {
            throw std::overflow_error("compact limb allocation is too large");
        }
        return ((bytes + pageSize_ - 1) / pageSize_) * pageSize_;
    }

    Code elementCount_;
    std::size_t maximumLimbs_;
    std::size_t activeLimbs_ = 1;
    std::size_t pageSize_ = 0;
    std::size_t committedBytes_ = 0;
    std::uint64_t* data_ = nullptr;
};

class GrowableResult {
public:
    void assign(const std::uint64_t* limbs, std::size_t limbCount) noexcept {
        limbCount_ = limbCount;
        limbs_.fill(0);
        for (std::size_t limb = 0; limb < limbCount_; ++limb) {
            limbs_[limb] = limbs[limb];
        }
    }

    std::string to_string() const {
        std::string decimal = "0";
        const int bitCount = static_cast<int>(64 * limbCount_);
        for (int bit = bitCount - 1; bit >= 0; --bit) {
            unsigned carry = static_cast<unsigned>(
                (limbs_[static_cast<std::size_t>(bit) / 64]
                    >> (bit % 64)) & 1);
            for (std::size_t index = decimal.size(); index-- > 0;) {
                const unsigned value =
                    static_cast<unsigned>(decimal[index] - '0') * 2 + carry;
                decimal[index] = static_cast<char>('0' + value % 10);
                carry = value / 10;
            }
            if (carry != 0) {
                decimal.insert(decimal.begin(),
                               static_cast<char>('0' + carry));
            }
        }
        return decimal;
    }

private:
    std::array<std::uint64_t, MAX_LIMBS> limbs_{};
    std::size_t limbCount_ = 1;
};

class GrowablePathCounter {
    int const rows;
    int const cols;
    MateCodec mc;
    MateCodec wc;
    CompactLimbArray value;
    CompactLimbArray deferred;

    int groupWidth;
    int numGroups;
    std::unique_ptr<int[]> groups;
    std::size_t maximumLimbs;
    int progressRow = 0;
    int progressCol = 0;
    std::uint64_t completedUpdates = 0;
    std::uint64_t weightedLimbUpdates = 0;
    std::uint64_t promotions = 0;
    std::chrono::nanoseconds promotionTime{};
    bool used = false;

public:
    GrowablePathCounter(int rows, int cols, std::size_t maximumLimbs)
            : rows(rows), cols(cols),
              mc(cols, (cols + 1) / 2, 1, 0),
              wc(cols - 1, cols / 2, 1, 0),
              value(mc.codeSize(), maximumLimbs),
              deferred(wc.codeSize(), maximumLimbs),
              maximumLimbs(maximumLimbs) {
        if (mc.leftWidth() >= 3) {
            groupWidth = mc.leftWidth() - 2;
            numGroups = 1 << groupWidth;
            groups = std::make_unique<int[]>(
                static_cast<std::size_t>(numGroups));
            for (int i = 0; i < numGroups; ++i) {
                groups[i] = i;
            }
            std::sort(groups.get(), groups.get() + numGroups,
                      [](int a, int b) {
                          return bitcount(static_cast<std::uint32_t>(a))
                              > bitcount(static_cast<std::uint32_t>(b));
                      });
        }
        else {
            groupWidth = 0;
            numGroups = 0;
        }

        if (msg) {
            std::cerr << "Reserved " << reserved_number_storage_bytes()
                      << " bytes and committed " << number_storage_bytes()
                      << " bytes for compact growable numbers.\n";
        }
    }

private:
    template<std::size_t LIMBS>
    void update(int j) {
        const int p = cols - j - 1;
        assert(1 <= p && p < cols);

        if (groups) {
            const int ungroupPos =
                (p - mc.rightWidth() > 1) ? p - mc.rightWidth() : 1;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
            for (int i = 0; i < numGroups; ++i) {
                updateGroup<LIMBS>(p, ungroupPos, groups[i], Mate(),
                                   mc.leftWidth(), mc.leftHeight());
            }
        }
        else {
            for (Code i = 0; i < mc.codeSizeL(); ++i) {
                updateBlock<LIMBS>(p, mc.codeTable(i));
            }
        }
    }

    template<std::size_t LIMBS>
    void updateGroup(int p, int ungroupPos, int group, Mate mL, int w,
                     int h) const {
        if (w == 0 && 0 <= h && h <= mc.centerHeight()) {
            const Code iL = mc.encodeL(mL);
            updateBlock<LIMBS>(p, mc.codeTable(iL));
            return;
        }
        if (w <= 0 || h < 0 || w < h - mc.centerHeight()) {
            return;
        }

        --w;
        if (w == ungroupPos || w == ungroupPos - 1) {
            mL.set(w, N);
            updateGroup<LIMBS>(p, ungroupPos, group, mL, w, h);
            mL.set(w, R);
            updateGroup<LIMBS>(p, ungroupPos, group, mL, w, h - 1);
            mL.set(w, L);
            updateGroup<LIMBS>(p, ungroupPos, group, mL, w, h + 1);
        }
        else {
            const int k = (w > ungroupPos) ? w - 2 : w;
            if ((group >> k) & 1) {
                mL.set(w, R);
                updateGroup<LIMBS>(p, ungroupPos, group, mL, w, h - 1);
                mL.set(w, L);
                updateGroup<LIMBS>(p, ungroupPos, group, mL, w, h + 1);
            }
            else {
                mL.set(w, N);
                updateGroup<LIMBS>(p, ungroupPos, group, mL, w, h);
            }
        }
    }

    template<std::size_t LIMBS>
    void updateBlock(int p, const CodeTable& block) const {
        for (Code i = 0; i < block.size; ++i) {
            Mate mate = block.mateL | block.mateR[i];
            auto c = value.at<LIMBS>(block.base + i);
            const MateValuePair w = mate.getPair(p);

            switch (w) {
            case NN:
                {
                    auto d = deferred.at<LIMBS>(
                        wc.encode(mate.shrink(p)));
                    if (c != 0) {
                        mate.setPair(p, LR);
                        value.at<LIMBS>(mc.encode(mate)) += c;
                    }
                    c += d;
                    d = 0;
                }
                break;
            case NL:
            case NR:
                {
                    auto d = deferred.at<LIMBS>(
                        wc.encode(mate.shrink(p)));
                    mate.setPair(p, (w == NL) ? LN : RN);
                    auto cc = value.at<LIMBS>(mc.encode(mate));
                    if (p == 1) {
                        d += cc;
                        cc += c;
                        c += d;
                        d = 0;
                    }
                    else {
                        const auto temporary = c.load();
                        c += cc;
                        c += d;
                        d.store(temporary);
                    }
                }
                break;
            case LL:
                {
                    mate.setPair(p, NN);
                    int q = p - 1;
                    int s = 1;
                    while (s > 0) {
                        --q;
                        assert(q >= 0);
                        switch (mate.get(q)) {
                        case L: ++s; break;
                        case R: --s; break;
                        default: break;
                        }
                    }
                    mate.set(q, L);
                    if (p == 1) {
                        value.at<LIMBS>(mc.encode(mate)) += c;
                    }
                    else {
                        deferred.at<LIMBS>(
                            wc.encode(mate.shrink(p - 1))) += c;
                    }
                }
                break;
            case RR:
                mate.setPair(p, NN);
                {
                    int q = p;
                    int s = 1;
                    while (s > 0) {
                        ++q;
                        assert(q < cols);
                        switch (mate.get(q)) {
                        case L: --s; break;
                        case R: ++s; break;
                        default: break;
                        }
                    }
                    mate.set(q, R);
                }
                if (p == 1) {
                    value.at<LIMBS>(mc.encode(mate)) += c;
                }
                else {
                    deferred.at<LIMBS>(
                        wc.encode(mate.shrink(p - 1))) += c;
                }
                break;
            case RL:
                mate.setPair(p, NN);
                if (p == 1) {
                    value.at<LIMBS>(mc.encode(mate)) += c;
                }
                else {
                    deferred.at<LIMBS>(
                        wc.encode(mate.shrink(p - 1))) += c;
                }
                break;
            default:
                break;
            }
        }
    }

    void dispatchUpdate(int col) {
        switch (value.active_limbs()) {
        case 1: update<1>(col); break;
        case 2: update<2>(col); break;
        case 3: update<3>(col); break;
        case 4: update<4>(col); break;
        case 5: update<5>(col); break;
        default: update<6>(col); break;
        }
    }

    void prepareWidthForNextUpdate() {
        // Starting from one configuration, one cell update can create at most
        // two successor configurations per input configuration.  Therefore the
        // global total after t updates is at most 2^t.  Grow before update 64,
        // 128, ...; this is conservative, independent of measured bit profiles,
        // and leaves operator+= as a final overflow guard.
        const std::size_t requiredLimbs = (std::min)(
            maximumLimbs,
            static_cast<std::size_t>((completedUpdates + 65) / 64));
        while (value.active_limbs() < requiredLimbs) {
            const auto start = std::chrono::steady_clock::now();
            value.grow();
            deferred.grow();
            promotionTime += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start);
            ++promotions;
        }
    }

public:
    bool count(const std::chrono::steady_clock::time_point deadline,
               GrowableResult& result) {
        if (used) {
            throw std::logic_error(
                "GrowablePathCounter instances support one count only");
        }
        used = true;
        value.at<1>(mc.encode(Mate(cols - 1, R))) = 1;

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols - 1; ++col) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                prepareWidthForNextUpdate();
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                weightedLimbUpdates += value.active_limbs();
                dispatchUpdate(col);
                ++completedUpdates;
                progressRow = row;
                progressCol = col + 1;
            }
        }

        const Code resultCode = mc.encode(Mate(0, R));
        result.assign(value.raw(resultCode), value.active_limbs());
        progressRow = rows;
        progressCol = cols;
        return true;
    }

    Code main_state_count() const noexcept { return mc.codeSize(); }
    Code deferred_state_count() const noexcept { return wc.codeSize(); }
    std::uint64_t completed_updates() const noexcept {
        return completedUpdates;
    }
    std::uint64_t state_visits() const noexcept {
        return completedUpdates * mc.codeSize();
    }
    int progress_row() const noexcept { return progressRow; }
    int progress_col() const noexcept { return progressCol; }
    std::size_t active_limbs() const noexcept {
        return value.active_limbs();
    }
    std::uint64_t promotion_count() const noexcept { return promotions; }
    double promotion_seconds() const noexcept {
        return static_cast<double>(promotionTime.count()) / 1'000'000'000.0;
    }
    double average_element_bytes() const noexcept {
        return completedUpdates == 0 ? 0.0
            : static_cast<double>(weightedLimbUpdates)
                * sizeof(std::uint64_t)
                / static_cast<double>(completedUpdates);
    }
    std::uint64_t number_storage_bytes() const noexcept {
        return value.logical_bytes() + deferred.logical_bytes();
    }
    std::uint64_t reserved_number_storage_bytes() const noexcept {
        return value.reserved_bytes() + deferred.reserved_bytes();
    }
};

int run(int n, int timeLimitSeconds,
        const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point deadline) {
    const int gridSize = n + 1;
    const std::size_t maximumLimbs =
        static_cast<std::size_t>(limbCountForN(n));
    GrowablePathCounter counter(gridSize, gridSize, maximumLimbs);
    GrowableResult paths;
    const bool completed = counter.count(deadline, paths);
    const auto finish = std::chrono::steady_clock::now();
    const auto elapsedNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
            .count();

    std::cout
        << "algorithm = minimal-perfect-hash in-place DP "
           "(compact growable limbs, exact up to "
        << maximumLimbs * 64 << "-bit)\n"
        << "n = " << n << '\n'
        << "limit = " << timeLimitSeconds << " seconds\n"
        << "status = " << (completed ? "COMPLETED" : "TIME_LIMIT") << '\n'
        << "paths = "
        << (completed ? paths.to_string() : std::string("INCOMPLETE")) << '\n'
        << "main states = " << counter.main_state_count() << '\n'
        << "deferred states = " << counter.deferred_state_count() << '\n'
        << "number storage bytes = " << counter.number_storage_bytes() << '\n'
        << "reserved number storage bytes = "
        << counter.reserved_number_storage_bytes() << '\n'
        << "element bytes = " << counter.active_limbs() * 8 << '\n'
        << "average element bytes = " << std::fixed << std::setprecision(3)
        << counter.average_element_bytes() << '\n'
        << "active count limbs = " << counter.active_limbs() << '\n'
        << "limb promotions = " << counter.promotion_count() << '\n'
        << "promotion elapsed = " << std::setprecision(9)
        << counter.promotion_seconds() << " seconds\n"
        << "updates = " << counter.completed_updates() << '\n'
        << "state visits = " << counter.state_visits() << '\n'
        << "progress row = " << counter.progress_row()
        << ", col = " << counter.progress_col() << '\n'
        << "threads = " << configured_thread_count() << '\n'
        << "elapsed = " << std::setprecision(9)
        << static_cast<double>(elapsedNs) / 1'000'000'000.0 << " seconds\n"
        << "elapsed_ns = " << elapsedNs << '\n';
    return 0;
}

} // namespace growable_mph

#ifndef MPH_INPLACE_PROGRAM_MAIN
#define MPH_INPLACE_PROGRAM_MAIN main
#endif

int MPH_INPLACE_PROGRAM_MAIN(int argc, char* argv[]) {
    int n = 18;
    int timeLimitSeconds = 60;
    int requestedThreads = 0;
    if (argc > 4 ||
        (argc > 1 && !parse_int_argument(argv[1], n)) ||
        (argc > 2 && !parse_int_argument(argv[2], timeLimitSeconds)) ||
        (argc > 3 && !parse_int_argument(argv[3], requestedThreads))) {
        std::cerr << "usage: " << argv[0] << " [n:0.." << MAX_N
                  << "] [time-limit-seconds:1..] [threads:1..]\n";
        return 1;
    }
    if (n < 0 || n > MAX_N || timeLimitSeconds < 1 ||
        (argc > 3 && requestedThreads < 1) ||
        !configure_thread_count(requestedThreads)) {
        std::cerr << "n must be between 0 and " << MAX_N
                  << ", the time limit must be positive, and the requested "
                     "thread count must be supported.\n";
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(timeLimitSeconds);
    try {
        if (n == 0) {
            const auto elapsedNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
            std::cout
                << "algorithm = minimal-perfect-hash in-place DP "
                   "(compact growable limbs, exact up to 64-bit)\n"
                << "n = 0\nlimit = " << timeLimitSeconds
                << " seconds\nstatus = COMPLETED\npaths = 1\n"
                   "main states = 1\ndeferred states = 0\n"
                   "number storage bytes = 0\n"
                   "reserved number storage bytes = 0\n"
                   "element bytes = 8\naverage element bytes = 0.000\n"
                   "active count limbs = 1\nlimb promotions = 0\n"
                   "promotion elapsed = 0.000000000 seconds\n"
                   "updates = 0\nstate visits = 0\n"
                   "progress row = 0, col = 0\nthreads = "
                << configured_thread_count() << "\nelapsed = "
                << std::fixed << std::setprecision(9)
                << static_cast<double>(elapsedNs) / 1'000'000'000.0
                << " seconds\nelapsed_ns = " << elapsedNs << '\n';
            return 0;
        }
        return growable_mph::run(
            n, timeLimitSeconds, start, deadline);
    }
    catch (const std::bad_alloc&) {
        std::cerr << "error: out of memory\n";
    }
    catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
    }
    return 2;
}
