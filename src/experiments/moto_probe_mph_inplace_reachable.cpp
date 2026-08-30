/*
 * Computing the Number of Paths in a Grid Graph
 *
 * Position-reachable-state variant of moto_probe_mph_inplace.cpp.
 * The original in-place transition is included unchanged for the common
 * state encoding and arithmetic types; this file replaces only the driver
 * that selects state blocks for each cell.
 *
 * STATUS: NOT ADOPTED. See docs/REACHABLE_STATE_PRUNING.md.
 * The block-level reachable set saturates almost immediately (at n=14 all
 * 1373 blocks end up reachable), so the pruning removes only ~2% of the
 * state visits while costing ~6.7x wall time. Two known defects were left
 * unfixed because the approach was abandoned:
 *   - reachableBlocks is not updated when `c += d` / `c += cc` writes a
 *     non-zero value into the block currently being processed, which makes
 *     the count too low from n>=5. Adding
 *     `if (c != 0) activateBlock(blockIndex);` right after the switch in
 *     updateBlockAtPosition makes n=4..14 match the original exactly.
 *   - SIGSEGV at n=15 (and at n=13 once the fix above is applied), most
 *     likely an out-of-range code produced in activateDeferredConsumers.
 *
 * License: LICENSE_GGCOUNT_MIT.txt
 */

#include <atomic>

// Keep the original program's main function available under another name so
// that this file can be compiled as a standalone executable.  push_macro is
// also needed when a test includes this file after redefining main.
#ifdef main
#pragma push_macro("main")
#undef main
#define main moto_probe_mph_inplace_original_main
#include "../moto_probe_mph_inplace.cpp"
#undef main
#pragma pop_macro("main")
#else
#define main moto_probe_mph_inplace_original_main
#include "../moto_probe_mph_inplace.cpp"
#undef main
#endif

template<typename Number>
class ReachablePathCounter {
    int const rows;
    int const cols;
    MateCodec mc;
    MateCodec wc;

    Number* value;
    Number* deferred;

    int groupWidth;
    int numGroups;
    int* groups;

    // A state value is never cleared by the in-place recurrence.  Therefore
    // a block that has received a non-zero value remains reachable for all
    // following cells.  The flags are monotone during one count() call.
    std::unique_ptr<std::atomic_bool[]> reachableBlocks;
    std::unique_ptr<std::atomic_bool[]> reachableGroups;
    std::atomic<std::uint64_t> stateVisits{0};

    int progressRow = 0;
    int progressCol = 0;
    std::uint64_t completedUpdates = 0;

public:
    ReachablePathCounter(int rows, int cols)
            : rows(rows), cols(cols),
              mc(cols, (cols + 1) / 2, 1, 0),
              wc(cols - 1, cols / 2, 1, 0),
              value(new Number[mc.codeSize()]),
              deferred(new Number[wc.codeSize()]),
              groupWidth(0), numGroups(0), groups(nullptr),
              reachableBlocks(new std::atomic_bool[mc.codeSizeL()]),
              reachableGroups(nullptr) {

        if (mc.leftWidth() >= 3) {
            groupWidth = mc.leftWidth() - 2;
            numGroups = 1 << groupWidth;
            groups = new int[numGroups];
            reachableGroups.reset(
                new std::atomic_bool[static_cast<std::size_t>(cols) *
                                      static_cast<std::size_t>(numGroups)]);

            if (msg == VERBOSE) std::cerr << "Allocated " << numGroups
                    << " groups for each position.\n";
        }

        for (Code i = 0; i < mc.codeSizeL(); ++i) {
            reachableBlocks[i].store(false, std::memory_order_relaxed);
        }
        if (reachableGroups != nullptr) {
            const std::size_t count = static_cast<std::size_t>(cols) *
                                      static_cast<std::size_t>(numGroups);
            for (std::size_t i = 0; i < count; ++i) {
                reachableGroups[i].store(false, std::memory_order_relaxed);
            }
        }

        if (groups != nullptr) {
            for (int i = 0; i < numGroups; ++i) {
                groups[i] = i;
            }
            std::sort(groups, groups + numGroups, [](int a, int b) {
                return bitcount(a) > bitcount(b);
            });
        }

        if (msg) std::cerr << "Allocated (" << mc.codeSize() << " + "
                << wc.codeSize() << ") * " << sizeof(Number) << " bytes"
                << " for numbers.\n";
    }

    ~ReachablePathCounter() {
        delete[] value;
        delete[] deferred;
        delete[] groups;
    }

private:
    std::size_t groupOffset(int p, int group) const noexcept {
        return static_cast<std::size_t>(p) *
                   static_cast<std::size_t>(numGroups) +
               static_cast<std::size_t>(group);
    }

    int ungroupPosition(int p) const noexcept {
        return (p - mc.rightWidth() > 1) ? p - mc.rightWidth() : 1;
    }

    int groupFor(Mate const& mateL, int p) const noexcept {
        const int ungroupPos = ungroupPosition(p);
        int group = 0;
        for (int w = 0; w < mc.leftWidth(); ++w) {
            if (w == ungroupPos || w == ungroupPos - 1) {
                continue;
            }
            const int k = (w > ungroupPos) ? w - 2 : w;
            if (mateL.get(w) != N) {
                group |= 1 << k;
            }
        }
        return group;
    }

    bool blockIsReachable(Code blockIndex) const noexcept {
        return reachableBlocks[blockIndex].load(std::memory_order_relaxed);
    }

    void activateBlock(Code blockIndex) {
        assert(blockIndex < mc.codeSizeL());
        if (reachableBlocks[blockIndex].exchange(
                    true, std::memory_order_relaxed)) {
            return;
        }

        if (reachableGroups != nullptr) {
            const Mate mateL = mc.codeTable(blockIndex).mateL.shiftRight(
                mc.rightWidth());
            for (int p = 1; p < cols; ++p) {
                const int group = groupFor(mateL, p);
                reachableGroups[groupOffset(p, group)].store(
                    true, std::memory_order_relaxed);
            }
        }
    }

    Code blockIndexFor(Mate const& mate) const {
        return mc.encodeL(mate.shiftRight(mc.rightWidth()));
    }

    Code addToValue(Mate const& mate, Number const& amount) {
        if (amount == 0) {
            return 0;
        }
        const Code blockIndex = blockIndexFor(mate);
        activateBlock(blockIndex);
        const Code code = mc.encode(mate);
        value[code] += amount;
        return code;
    }

    static Mate insertSlot(Mate const& compressed, int position,
            MateValue value) noexcept {
        const int shift = 2 * position;
        const MateID raw = compressed.id();
        const MateID lowerMask = shift == 0
            ? MateID(0)
            : (MateID(1) << shift) - 1;
        return Mate((raw & lowerMask) |
                    (MateID(value) << shift) |
                    ((raw >> shift) << (shift + 2)));
    }

    bool isMainState(Mate const& mate) const noexcept {
        int height = mc.leftHeight();
        for (int position = 0; position < mc.leftWidth(); ++position) {
            switch (mate.get(position)) {
            case R:
                --height;
                break;
            case L:
                ++height;
                break;
            case N:
                break;
            default:
                return false;
            }
            if (height < 0) {
                return false;
            }
        }
        if (height < 0 || height > mc.centerHeight()) {
            return false;
        }

        for (int position = mc.leftWidth(); position < cols; ++position) {
            switch (mate.get(position)) {
            case R:
                --height;
                break;
            case L:
                ++height;
                break;
            case N:
                break;
            default:
                return false;
            }
            if (height < 0) {
                return false;
            }
        }
        return height == mc.rightHeight();
    }

    // A deferred value written while processing p is consumed by the
    // expansion of its compressed mate at p or at the next position p - 1.
    // Mark all valid expansions.  There are at most six candidates, so this
    // is much cheaper than looking through the complete block table.
    void activateDeferredConsumers(Mate const& compressed, int p) {
        const int positions[2] = {p, p - 1};
        for (const int position : positions) {
            if (position < 0 || position >= cols) {
                continue;
            }
            for (const MateValue value : {N, R, L}) {
                const Mate candidate = insertSlot(
                    compressed, position, value);
                if (isMainState(candidate)) {
                    activateBlock(blockIndexFor(candidate));
                }
            }
        }
    }

    void addToDeferred(Mate const& compressed, Number const& amount, int p) {
        if (amount != 0) {
            activateDeferredConsumers(compressed, p);
        }
        deferred[wc.encode(compressed)] += amount;
    }

    void updateBlockAtPosition(int p, CodeTable const& block) {
        stateVisits.fetch_add(block.size, std::memory_order_relaxed);

        for (Code i = 0; i < block.size; ++i) {
            Mate mate = block.mateL | block.mateR[i];
            Number& c = value[block.base + i];
            const MateValuePair w = mate.getPair(p);
            const Mate compressedMate = mate.shrink(p);
            const Code deferredCode = wc.encode(compressedMate);

            switch (w) {
            case NN:
                {
                    Number& d = deferred[deferredCode];
                    if (c != 0) {
                        mate.setPair(p, LR);
                        addToValue(mate, c);
                    }
                    c += d;
                    d = 0;
                }
                break;
            case NL:
            case NR:
                {
                    Number& d = deferred[deferredCode];
                    mate.setPair(p, (w == NL) ? LN : RN);
                    activateBlock(blockIndexFor(mate));
                    Number& cc = value[mc.encode(mate)];

                    if (p == 1) {
                        d += cc;
                        cc += c;
                        c += d;
                        d = 0;
                    }
                    else {
                        Number tmp = c;
                        c += cc;
                        c += d;
                        if (tmp != 0) {
                            activateDeferredConsumers(compressedMate, p);
                        }
                        d = tmp;
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
                        case L:
                            ++s;
                            break;
                        case R:
                            --s;
                            break;
                        default:
                            break;
                        }
                    }
                    mate.set(q, L);
                    if (p == 1) {
                        addToValue(mate, c);
                    }
                    else {
                        addToDeferred(mate.shrink(p - 1), c, p);
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
                        case L:
                            --s;
                            break;
                        case R:
                            ++s;
                            break;
                        default:
                            break;
                        }
                    }
                    mate.set(q, R);
                }
                if (p == 1) {
                    addToValue(mate, c);
                }
                else {
                    addToDeferred(mate.shrink(p - 1), c, p);
                }
                break;
            case RL:
                mate.setPair(p, NN);
                if (p == 1) {
                    addToValue(mate, c);
                }
                else {
                    addToDeferred(mate.shrink(p - 1), c, p);
                }
                break;
            default:
                break;
            }
        }
    }

public:
    bool count(const std::chrono::steady_clock::time_point deadline,
               Number& result) {
        for (std::int64_t code = 0;
             code < static_cast<std::int64_t>(mc.codeSize()); ++code) {
            value[static_cast<Code>(code)] = 0;
        }
        for (std::int64_t code = 0;
             code < static_cast<std::int64_t>(wc.codeSize()); ++code) {
            deferred[static_cast<Code>(code)] = 0;
        }

        for (Code blockIndex = 0; blockIndex < mc.codeSizeL(); ++blockIndex) {
            reachableBlocks[blockIndex].store(false,
                                               std::memory_order_relaxed);
        }
        if (reachableGroups != nullptr) {
            const std::size_t count = static_cast<std::size_t>(cols) *
                                      static_cast<std::size_t>(numGroups);
            for (std::size_t i = 0; i < count; ++i) {
                reachableGroups[i].store(false,
                                         std::memory_order_relaxed);
            }
        }

        const Mate initialMate(cols - 1, R);
        value[mc.encode(initialMate)] = 1;
        activateBlock(blockIndexFor(initialMate));
        stateVisits.store(0, std::memory_order_relaxed);
        completedUpdates = 0;
        progressRow = 0;
        progressCol = 0;

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols - 1; ++col) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                const int p = cols - col - 1;

                if (groups != nullptr) {
                    const int ungroupPos = ungroupPosition(p);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
                    for (int i = 0; i < numGroups; ++i) {
                        const int group = groups[i];
                        if (reachableGroups[groupOffset(p, group)].load(
                                    std::memory_order_relaxed)) {
                            updateGroupAtPosition(p, ungroupPos, group,
                                Mate(), mc.leftWidth(), mc.leftHeight());
                        }
                    }
                }
                else {
                    for (Code blockIndex = 0;
                         blockIndex < mc.codeSizeL(); ++blockIndex) {
                        updateBlockAtPosition(p, mc.codeTable(blockIndex));
                    }
                }

                ++completedUpdates;
                progressRow = row;
                progressCol = col + 1;
            }
        }

        result = value[mc.encode(Mate(0, R))];
        progressRow = rows;
        progressCol = cols;
        return true;
    }

    Code main_state_count() const noexcept {
        return mc.codeSize();
    }

    Code deferred_state_count() const noexcept {
        return wc.codeSize();
    }

    std::uint64_t number_storage_bytes() const noexcept {
        return (mc.codeSize() + wc.codeSize()) * sizeof(Number);
    }

    std::uint64_t completed_updates() const noexcept {
        return completedUpdates;
    }

    std::uint64_t state_visits() const noexcept {
        return stateVisits.load(std::memory_order_relaxed);
    }

    std::uint64_t reachable_block_count() const noexcept {
        std::uint64_t count = 0;
        for (Code blockIndex = 0; blockIndex < mc.codeSizeL(); ++blockIndex) {
            if (blockIsReachable(blockIndex)) {
                ++count;
            }
        }
        return count;
    }

    int progress_row() const noexcept {
        return progressRow;
    }

    int progress_col() const noexcept {
        return progressCol;
    }

private:
    void updateGroupAtPosition(int p, int ungroupPos, int group,
            Mate mateL, int w, int h) {
        if (w == 0 && 0 <= h && h <= mc.centerHeight()) {
            const Code blockIndex = mc.encodeL(mateL);
            updateBlockAtPosition(p, mc.codeTable(blockIndex));
            return;
        }
        if (w <= 0 || h < 0 || w < h - mc.centerHeight()) {
            return;
        }

        --w;
        if (w == ungroupPos || w == ungroupPos - 1) {
            mateL.set(w, N);
            updateGroupAtPosition(p, ungroupPos, group, mateL, w, h);
            mateL.set(w, R);
            updateGroupAtPosition(p, ungroupPos, group, mateL, w, h - 1);
            mateL.set(w, L);
            updateGroupAtPosition(p, ungroupPos, group, mateL, w, h + 1);
        }
        else {
            const int k = (w > ungroupPos) ? w - 2 : w;
            if ((group >> k) & 1) {
                mateL.set(w, R);
                updateGroupAtPosition(p, ungroupPos, group, mateL,
                                      w, h - 1);
                mateL.set(w, L);
                updateGroupAtPosition(p, ungroupPos, group, mateL,
                                      w, h + 1);
            }
            else {
                mateL.set(w, N);
                updateGroupAtPosition(p, ungroupPos, group, mateL, w, h);
            }
        }
    }
};

int main(int argc, char* argv[]) {
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
            const auto finish = std::chrono::steady_clock::now();
            const auto elapsedNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    finish - start).count();
            std::cout
                << "algorithm = minimal-perfect-hash in-place DP "
                   "(position-reachable blocks, exact 384-bit)\n"
                << "n = 0\n"
                << "limit = " << timeLimitSeconds << " seconds\n"
                << "status = COMPLETED\n"
                << "paths = 1\n"
                << "main states = 1\n"
                << "deferred states = 0\n"
                << "number storage bytes = 0\n"
                << "reachable blocks = 0\n"
                << "updates = 0\n"
                << "state visits = 0\n"
                << "progress row = 0, col = 0\n"
                << "threads = " << configured_thread_count() << '\n'
                << "elapsed = " << std::fixed << std::setprecision(9)
                << static_cast<double>(elapsedNs) / 1'000'000'000.0
                << " seconds\n"
                << "elapsed_ns = " << elapsedNs << '\n';
            return 0;
        }

        const int gridSize = n + 1;
        ReachablePathCounter<Count> counter(gridSize, gridSize);
        Count paths;
        const bool completed = counter.count(deadline, paths);
        const auto finish = std::chrono::steady_clock::now();
        const auto elapsedNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finish - start).count();

        std::cout
            << "algorithm = minimal-perfect-hash in-place DP "
               "(position-reachable blocks, exact 384-bit)\n"
            << "n = " << n << '\n'
            << "limit = " << timeLimitSeconds << " seconds\n"
            << "status = " << (completed ? "COMPLETED" : "TIME_LIMIT")
            << '\n'
            << "paths = "
            << (completed ? paths.to_string() : std::string("INCOMPLETE"))
            << '\n'
            << "main states = " << counter.main_state_count() << '\n'
            << "deferred states = " << counter.deferred_state_count() << '\n'
            << "number storage bytes = "
            << counter.number_storage_bytes() << '\n'
            << "reachable blocks = " << counter.reachable_block_count()
            << '\n'
            << "updates = " << counter.completed_updates() << '\n'
            << "state visits = " << counter.state_visits() << '\n'
            << "progress row = " << counter.progress_row()
            << ", col = " << counter.progress_col() << '\n'
            << "threads = " << configured_thread_count() << '\n'
            << "elapsed = " << std::fixed << std::setprecision(9)
            << static_cast<double>(elapsedNs) / 1'000'000'000.0
            << " seconds\n"
            << "elapsed_ns = " << elapsedNs << '\n';
        return 0;
    } catch (const std::bad_alloc&) {
        std::cerr << "error: out of memory\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
    }
    return 2;
}
