#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <intrin.h>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
// n=20 の840辺について、全ての辺部分集合を上界にできる896ビット整数。
// DP本体は実際に必要になった64ビットlimbだけを状態ごとに確保する。
class BigUInt {
public:
    static constexpr std::size_t LIMB_COUNT = 14;
    static constexpr int BIT_COUNT = static_cast<int>(64 * LIMB_COUNT);

    BigUInt(std::uint64_t value = 0) noexcept {
        limbs_[0] = value;
    }

    BigUInt& operator+=(const BigUInt& other) {
        unsigned char carry = 0;
        for (std::size_t limb = 0; limb < LIMB_COUNT; ++limb) {
            carry = _addcarry_u64(
                carry, limbs_[limb], other.limbs_[limb], &limbs_[limb]);
        }
        if (carry != 0) {
            throw std::overflow_error("BigUInt exceeded 896 bits");
        }
        return *this;
    }

    void set_limb(std::size_t index, std::uint64_t value) noexcept {
        limbs_[index] = value;
    }

    std::uint64_t limb(std::size_t index) const noexcept {
        return limbs_[index];
    }

    std::string to_string() const {
        std::string decimal = "0";
        for (int bit = BIT_COUNT - 1; bit >= 0; --bit) {
            unsigned carry = static_cast<unsigned>(
                (limbs_[bit / 64] >> (bit % 64)) & 1);
            for (std::size_t i = decimal.size(); i-- > 0;) {
                const unsigned value =
                    static_cast<unsigned>(decimal[i] - '0') * 2 + carry;
                decimal[i] = static_cast<char>('0' + value % 10);
                carry = value / 10;
            }
            if (carry != 0) {
                decimal.insert(decimal.begin(), static_cast<char>('0' + carry));
            }
        }
        return decimal;
    }

    friend std::ostream& operator<<(std::ostream& output, const BigUInt& value) {
        return output << value.to_string();
    }

private:
    std::array<std::uint64_t, LIMB_COUNT> limbs_{};
};

using Count = BigUInt;
using StateKey = std::uint64_t;

enum Plug : std::uint8_t {
    EMPTY = 0,
    OPEN = 1,
    CLOSE = 2,
    START = 3,
};

// STARTを除いたMotzkin語を組合せ順位へ変換する。
// 奇数長を第1表側へ寄せ、第2表は不要な終端高さを省いた32ビット値にする。
class StateRanker {
public:
    explicit StateRanker(int slot_count)
        : slot_count_(slot_count), motzkin_length_(slot_count - 1) {
        if (slot_count_ < 2 || motzkin_length_ >= 24) {
            throw std::invalid_argument("unsupported ranker width");
        }

        completions_[0][0] = 1;
        for (int remaining = 1; remaining <= motzkin_length_; ++remaining) {
            for (int height = 0; height <= motzkin_length_; ++height) {
                std::uint64_t count = completions_[remaining - 1][height];
                if (height + 1 <= motzkin_length_) {
                    count += completions_[remaining - 1][height + 1];
                }
                if (height > 0) {
                    count += completions_[remaining - 1][height - 1];
                }
                if (count > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::overflow_error("Motzkin table exceeded 32 bits");
                }
                completions_[remaining][height] =
                    static_cast<std::uint32_t>(count);
            }
        }

        motzkin_count_ = completions_[motzkin_length_][0];
        const std::uint64_t universe = 1 +
            static_cast<std::uint64_t>(slot_count_) * motzkin_count_;
        if (universe > std::numeric_limits<std::uint32_t>::max()) {
            throw std::overflow_error("rank universe exceeded 32 bits");
        }
        universe_size_ = static_cast<std::uint32_t>(universe);

        // 第2表は開始高さごとに複製されるため、奇数長では第1表を長くする。
        first_length_ = (motzkin_length_ + 1) / 2;
        second_length_ = motzkin_length_ - first_length_;
        first_code_count_ = std::size_t{1} << (2 * first_length_);
        second_code_count_ = std::size_t{1} << (2 * second_length_);
        first_mask_ = first_code_count_ - 1;

        if (motzkin_count_ > FIRST_RANK_MASK || first_length_ + 1 >= 16) {
            throw std::overflow_error("first rank table cannot be packed");
        }
        first_table_ = build_chunk_table<std::uint32_t>(
            0, first_length_, 0,
            [](int height, std::uint32_t word_rank) {
                return (static_cast<std::uint32_t>(height + 1)
                        << FIRST_RANK_BITS) |
                       word_rank;
            });
        second_table_ = build_chunk_table<std::uint32_t>(
            first_length_, second_length_,
            std::min(first_length_, second_length_),
            [](int, std::uint32_t word_rank) { return word_rank; });
    }

    std::uint32_t rank(StateKey state) const {
        if (state == 0) {
            return 0;
        }

        constexpr StateKey EVEN_BITS = 0x5555555555555555ULL;
        const StateKey start_bits = state & (state >> 1) & EVEN_BITS;
#ifndef NDEBUG
        if (start_bits == 0 || (start_bits & (start_bits - 1)) != 0) {
            throw std::logic_error("rank state must contain exactly one START");
        }
#endif
        const int start_position =
            static_cast<int>(std::countr_zero(start_bits) / 2);
        const int start_shift = 2 * start_position;
        const StateKey lower_mask =
            start_shift == 0 ? 0 : (StateKey{1} << start_shift) - 1;
        const StateKey compressed =
            (state & lower_mask) |
            ((state >> (start_shift + 2)) << start_shift);

        const std::size_t first_code = compressed & first_mask_;
        const std::size_t second_code = compressed >> (2 * first_length_);
        const std::uint32_t first = first_table_[first_code];
        const std::uint32_t middle_height =
            (first >> FIRST_RANK_BITS) - 1;
        const std::uint32_t second = second_table_[
            static_cast<std::size_t>(middle_height) * second_code_count_ +
            second_code];

        return 1 + static_cast<std::uint32_t>(start_position) * motzkin_count_ +
               (first & FIRST_RANK_MASK) + second;
    }

    StateKey unrank(std::uint32_t state_rank) const {
        if (state_rank >= universe_size_) {
            throw std::out_of_range("state rank is outside the universe");
        }
        if (state_rank == 0) {
            return 0;
        }

        --state_rank;
        const int start_position = state_rank / motzkin_count_;
        std::uint32_t word_rank = state_rank % motzkin_count_;
        int height = 0;
        StateKey state = StateKey{START} << (2 * start_position);

        for (int word_position = 0; word_position < motzkin_length_;
             ++word_position) {
            const int remaining = motzkin_length_ - word_position - 1;
            Plug symbol = EMPTY;
            const std::uint32_t empty_count = completions_.at(
                static_cast<std::size_t>(remaining)).at(
                static_cast<std::size_t>(height));
            if (word_rank >= empty_count) {
                word_rank -= empty_count;
                const std::uint32_t open_count = completions_.at(
                    static_cast<std::size_t>(remaining)).at(
                    static_cast<std::size_t>(height + 1));
                if (word_rank < open_count) {
                    symbol = OPEN;
                    ++height;
                } else {
                    word_rank -= open_count;
                    symbol = CLOSE;
                    --height;
                }
            }

            const int state_position = word_position < start_position
                ? word_position
                : word_position + 1;
            state |= static_cast<StateKey>(symbol) << (2 * state_position);
        }
        return state;
    }

    std::uint32_t universe_size() const {
        return universe_size_;
    }

private:
    static constexpr int FIRST_RANK_BITS = 28;
    static constexpr std::uint32_t FIRST_RANK_MASK =
        (std::uint32_t{1} << FIRST_RANK_BITS) - 1;

    template <typename Entry, typename Pack>
    std::vector<Entry> build_chunk_table(
        int first_position,
        int length,
        int max_start_height,
        Pack pack) const {
        const std::size_t code_count = std::size_t{1} << (2 * length);
        std::vector<Entry> table(
            static_cast<std::size_t>(max_start_height + 1) * code_count, 0);

        for (int start_height = 0; start_height <= max_start_height;
             ++start_height) {
            for (std::size_t code = 0; code < code_count; ++code) {
                int height = start_height;
                std::uint32_t word_rank = 0;
                bool valid = true;

                for (int offset = 0; offset < length; ++offset) {
                    const int symbol = static_cast<int>(
                        (code >> (2 * offset)) & 3);
                    const int remaining =
                        motzkin_length_ - (first_position + offset) - 1;
                    if (symbol == EMPTY) {
                        continue;
                    }
                    if (symbol == OPEN) {
                        word_rank += completions_[remaining][height];
                        ++height;
                        continue;
                    }
                    if (symbol == CLOSE && height > 0) {
                        word_rank += completions_[remaining][height];
                        word_rank += completions_[remaining][height + 1];
                        --height;
                        continue;
                    }
                    valid = false;
                    break;
                }

                if (valid && height <= motzkin_length_) {
                    table[static_cast<std::size_t>(start_height) * code_count +
                          code] = pack(height, word_rank);
                }
            }
        }
        return table;
    }

    int slot_count_ = 0;
    int motzkin_length_ = 0;
    int first_length_ = 0;
    int second_length_ = 0;
    std::size_t first_code_count_ = 0;
    std::size_t second_code_count_ = 0;
    StateKey first_mask_ = 0;
    std::uint32_t motzkin_count_ = 0;
    std::uint32_t universe_size_ = 0;
    std::array<std::array<std::uint32_t, 24>, 24> completions_{};
    std::vector<std::uint32_t> first_table_;
    std::vector<std::uint32_t> second_table_;
};

// 状態数に応じて再利用する可変幅カウンタ付きエントリプール。
// カウンタはAoS配置を保ち、必要になった時だけ64ビットlimbを追加する。
class AdaptiveEntryPool {
public:
    static constexpr std::size_t ENTRY_CHUNK_BITS = 8;
    static constexpr std::size_t ENTRY_CHUNK_SIZE =
        std::size_t{1} << ENTRY_CHUNK_BITS;
    static constexpr std::size_t CHUNKS_PER_SLAB = 256;
    static constexpr std::size_t ENTRIES_PER_SLAB =
        CHUNKS_PER_SLAB * ENTRY_CHUNK_SIZE;

    class CountRef {
    public:
        std::uint8_t limb_count() const noexcept {
            return limb_count_;
        }

        std::uint64_t limb(std::size_t index) const noexcept {
            return limbs_[index];
        }

        const std::uint64_t* data() const noexcept {
            return limbs_;
        }

    private:
        friend class AdaptiveEntryPool;

        CountRef(const std::uint64_t* limbs,
                 std::uint8_t limb_count) noexcept
            : limbs_(limbs), limb_count_(limb_count) {}

        const std::uint64_t* limbs_ = nullptr;
        std::uint8_t limb_count_ = 0;
    };

    AdaptiveEntryPool() = default;

    void clear(std::uint8_t required_limb_count) {
        if (required_limb_count == 0 ||
            required_limb_count > BigUInt::LIMB_COUNT) {
            throw std::invalid_argument("invalid adaptive limb count");
        }
        used_chunks_ = 0;
        active_limb_count_ = required_limb_count;
        peak_limb_count_ = std::max(peak_limb_count_, active_limb_count_);
    }

    std::uint32_t acquire_chunk() {
        const std::size_t slab_index = used_chunks_ / CHUNKS_PER_SLAB;
        const std::size_t chunk_in_slab = used_chunks_ % CHUNKS_PER_SLAB;
        if (slab_index == slabs_.size()) {
            slabs_.push_back(std::make_unique<Slab>());
        }

        Slab& slab = *slabs_[slab_index];
        if (chunk_in_slab == 0 && slab.stride < active_limb_count_) {
            slab.counts = std::make_unique_for_overwrite<std::uint64_t[]>(
                ENTRIES_PER_SLAB * active_limb_count_);
            slab.stride = active_limb_count_;
        }
        if (slab.stride < active_limb_count_) {
            throw std::logic_error("entry slab has an invalid count stride");
        }

        if (used_chunks_ == chunk_sizes_.size()) {
            chunk_sizes_.push_back(0);
        } else {
            chunk_sizes_[used_chunks_] = 0;
        }

        const std::uint64_t base =
            static_cast<std::uint64_t>(used_chunks_) * ENTRY_CHUNK_SIZE;
        if (base > std::numeric_limits<std::uint32_t>::max()) {
            throw std::overflow_error("entry pool exceeded 32-bit indices");
        }
        ++used_chunks_;
        return static_cast<std::uint32_t>(base);
    }

    void mark_chunk_size(std::uint32_t chunk_base,
                         std::uint16_t size) noexcept {
        chunk_sizes_[chunk_base >> ENTRY_CHUNK_BITS] = size;
    }

    void set_key(std::uint32_t entry_index, StateKey key) noexcept {
        slab(entry_index).keys[offset(entry_index)] = key;
    }

    StateKey key(std::uint32_t entry_index) const noexcept {
        return slab(entry_index).keys[offset(entry_index)];
    }

    void set_one(std::uint32_t entry_index) noexcept {
        std::uint64_t* destination = count_ptr(entry_index);
        destination[0] = 1;
        std::fill(destination + 1,
                  destination + active_limb_count_,
                  std::uint64_t{0});
    }

    void set_count(std::uint32_t entry_index, const CountRef& source) {
        if (source.limb_count() > active_limb_count_) {
            throw std::logic_error("source count is wider than destination");
        }
        std::uint64_t* destination = count_ptr(entry_index);
        if (active_limb_count_ == 1) {
            destination[0] = source.limb(0);
            return;
        }
        std::size_t limb_index = 0;
        for (; limb_index < source.limb_count(); ++limb_index) {
            destination[limb_index] = source.limb(limb_index);
        }
        std::fill(destination + limb_index,
                  destination + active_limb_count_,
                  std::uint64_t{0});
    }

    void add_one(std::uint32_t entry_index) {
        std::uint64_t* destination = count_ptr(entry_index);
        unsigned char carry = _addcarry_u64(
            0, destination[0], 1, &destination[0]);
        for (std::size_t limb_index = 1;
             carry != 0 && limb_index < active_limb_count_; ++limb_index) {
            carry = _addcarry_u64(
                carry, destination[limb_index], 0,
                &destination[limb_index]);
        }
        if (carry != 0) {
            const std::uint8_t old_width = active_limb_count_;
            promote();
            count_ptr(entry_index)[old_width] = 1;
        }
    }

    void add_count(std::uint32_t entry_index, const CountRef& source) {
        if (source.limb_count() > active_limb_count_) {
            throw std::logic_error("source count is wider than destination");
        }

        std::uint64_t* destination = count_ptr(entry_index);
        if (active_limb_count_ == 1 && source.limb_count() == 1) {
            const unsigned char carry = _addcarry_u64(
                0, destination[0], source.limb(0), &destination[0]);
            if (carry != 0) {
                promote();
                count_ptr(entry_index)[1] = 1;
            }
            return;
        }

        unsigned char carry = 0;
        std::size_t limb_index = 0;
        for (; limb_index < source.limb_count(); ++limb_index) {
            carry = _addcarry_u64(
                carry,
                destination[limb_index],
                source.limb(limb_index),
                &destination[limb_index]);
        }
        for (; carry != 0 && limb_index < active_limb_count_; ++limb_index) {
            carry = _addcarry_u64(
                carry, destination[limb_index], 0,
                &destination[limb_index]);
        }
        if (carry != 0) {
            const std::uint8_t old_width = active_limb_count_;
            promote();
            count_ptr(entry_index)[old_width] = 1;
        }
    }

    CountRef view(std::uint32_t entry_index) const noexcept {
        return CountRef(count_ptr(entry_index), active_limb_count_);
    }

    void copy_to(std::uint32_t entry_index, BigUInt& destination) const noexcept {
        const std::uint64_t* source = count_ptr(entry_index);
        for (std::size_t limb_index = 0;
             limb_index < BigUInt::LIMB_COUNT; ++limb_index) {
            destination.set_limb(
                limb_index,
                limb_index < active_limb_count_
                    ? source[limb_index]
                    : 0);
        }
    }

    std::uint8_t limb_count() const noexcept {
        return active_limb_count_;
    }

    std::uint8_t peak_limb_count() const noexcept {
        return peak_limb_count_;
    }

    std::size_t capacity() const noexcept {
        return slabs_.size() * ENTRIES_PER_SLAB;
    }

    std::size_t allocated_bytes() const noexcept {
        std::size_t bytes = 0;
        for (const auto& slab_ptr : slabs_) {
            bytes += ENTRIES_PER_SLAB * sizeof(StateKey);
            bytes += ENTRIES_PER_SLAB * slab_ptr->stride *
                     sizeof(std::uint64_t);
        }
        return bytes;
    }

    void swap(AdaptiveEntryPool& other) noexcept {
        slabs_.swap(other.slabs_);
        chunk_sizes_.swap(other.chunk_sizes_);
        std::swap(used_chunks_, other.used_chunks_);
        std::swap(active_limb_count_, other.active_limb_count_);
        std::swap(peak_limb_count_, other.peak_limb_count_);
    }

private:
    struct Slab {
        Slab()
            : keys(std::make_unique_for_overwrite<StateKey[]>(
                  ENTRIES_PER_SLAB)) {}

        std::unique_ptr<StateKey[]> keys;
        std::unique_ptr<std::uint64_t[]> counts;
        std::uint8_t stride = 0;
    };

    static std::size_t slab_index(std::uint32_t entry_index) noexcept {
        return entry_index >> 16;
    }

    static std::size_t offset(std::uint32_t entry_index) noexcept {
        return entry_index & (ENTRIES_PER_SLAB - 1);
    }

    Slab& slab(std::uint32_t entry_index) noexcept {
        return *slabs_[slab_index(entry_index)];
    }

    const Slab& slab(std::uint32_t entry_index) const noexcept {
        return *slabs_[slab_index(entry_index)];
    }

    std::uint64_t* count_ptr(std::uint32_t entry_index) noexcept {
        Slab& entry_slab = slab(entry_index);
        return entry_slab.counts.get() +
               offset(entry_index) * entry_slab.stride;
    }

    const std::uint64_t* count_ptr(
        std::uint32_t entry_index) const noexcept {
        const Slab& entry_slab = slab(entry_index);
        return entry_slab.counts.get() +
               offset(entry_index) * entry_slab.stride;
    }

    void promote() {
        if (active_limb_count_ >= BigUInt::LIMB_COUNT) {
            throw std::overflow_error("adaptive count exceeded 896 bits");
        }

        const std::uint8_t old_width = active_limb_count_;
        const std::uint8_t new_width = static_cast<std::uint8_t>(old_width + 1);
        const std::size_t used_slab_count =
            (used_chunks_ + CHUNKS_PER_SLAB - 1) / CHUNKS_PER_SLAB;

        for (std::size_t current_slab = 0;
             current_slab < used_slab_count; ++current_slab) {
            Slab& entry_slab = *slabs_[current_slab];

            if (entry_slab.stride >= new_width) {
                const std::size_t first_chunk =
                    current_slab * CHUNKS_PER_SLAB;
                const std::size_t chunk_end = std::min(
                    first_chunk + CHUNKS_PER_SLAB, used_chunks_);
                for (std::size_t chunk = first_chunk;
                     chunk < chunk_end; ++chunk) {
                    const std::size_t chunk_offset =
                        (chunk - first_chunk) * ENTRY_CHUNK_SIZE;
                    for (std::size_t item = 0;
                         item < chunk_sizes_[chunk]; ++item) {
                        entry_slab.counts[
                            (chunk_offset + item) * entry_slab.stride +
                            old_width] = 0;
                    }
                }
                continue;
            }

            auto expanded = std::make_unique_for_overwrite<std::uint64_t[]>(
                ENTRIES_PER_SLAB * new_width);
            const std::size_t first_chunk =
                current_slab * CHUNKS_PER_SLAB;
            const std::size_t chunk_end = std::min(
                first_chunk + CHUNKS_PER_SLAB, used_chunks_);
            for (std::size_t chunk = first_chunk;
                 chunk < chunk_end; ++chunk) {
                const std::size_t chunk_offset =
                    (chunk - first_chunk) * ENTRY_CHUNK_SIZE;
                for (std::size_t item = 0;
                     item < chunk_sizes_[chunk]; ++item) {
                    const std::size_t old_base =
                        (chunk_offset + item) * entry_slab.stride;
                    const std::size_t new_base =
                        (chunk_offset + item) * new_width;
                    std::copy_n(entry_slab.counts.get() + old_base,
                                old_width,
                                expanded.get() + new_base);
                    expanded[new_base + old_width] = 0;
                }
            }
            entry_slab.counts = std::move(expanded);
            entry_slab.stride = new_width;
        }

        active_limb_count_ = new_width;
        peak_limb_count_ = std::max(peak_limb_count_, new_width);
    }

    std::vector<std::unique_ptr<Slab>> slabs_;
    std::vector<std::uint16_t> chunk_sizes_;
    std::size_t used_chunks_ = 0;
    std::uint8_t active_limb_count_ = 1;
    std::uint8_t peak_limb_count_ = 1;
};

// 4K rankページでは、12ビット局所添字と4ビットページ内世代を
// 1つの16ビットslotへ詰める。ページ本体は再利用スラブから確保する。
class RankedStateMap {
public:
    using CountRef = AdaptiveEntryPool::CountRef;

    explicit RankedStateMap(const StateRanker& ranker)
        : ranker_(&ranker),
          pages_((static_cast<std::size_t>(ranker.universe_size()) +
                  PAGE_SIZE - 1) /
                 PAGE_SIZE,
                 nullptr) {
        active_pages_.reserve(pages_.size());
    }

    void clear(std::uint8_t required_limb_count) {
        active_pages_.clear();
        active_size_ = 0;
        entry_pool_.clear(required_limb_count);
        ++generation_;
        if (generation_ == 0) {
            for (Page* page : pages_) {
                if (page != nullptr) {
                    page->active_generation = 0;
                }
            }
            generation_ = 1;
        }
    }

    void add_one(StateKey key) {
        const auto [entry_index, inserted] = ensure_entry(key);
        if (inserted) {
            entry_pool_.set_one(entry_index);
        } else {
            entry_pool_.add_one(entry_index);
        }
    }

    void add(StateKey key, const CountRef& ways) {
        const auto [entry_index, inserted] = ensure_entry(key);
        if (inserted) {
            entry_pool_.set_count(entry_index, ways);
        } else {
            entry_pool_.add_count(entry_index, ways);
        }
    }

    bool copy_value(StateKey key, Count& destination) const {
        const std::uint32_t entry_index = find_entry(key);
        if (entry_index == INVALID_ENTRY) {
            return false;
        }
        entry_pool_.copy_to(entry_index, destination);
        return true;
    }

    std::size_t size() const noexcept {
        return active_size_;
    }

    std::size_t active_page_count() const noexcept {
        return active_pages_.size();
    }

    std::size_t page_size(std::size_t active_page_index) const noexcept {
        return active_page(active_page_index).size;
    }

    StateKey key_at(std::size_t active_page_index,
                    std::size_t local_index) const noexcept {
        const Page& page = active_page(active_page_index);
        return entry_pool_.key(page.entry_index(local_index));
    }

    CountRef ways_at(std::size_t active_page_index,
                     std::size_t local_index) const noexcept {
        const Page& page = active_page(active_page_index);
        return entry_pool_.view(page.entry_index(local_index));
    }

    std::size_t allocated_pages() const noexcept {
        return page_pool_.allocated_pages();
    }

    std::size_t page_storage_bytes() const noexcept {
        return page_pool_.allocated_bytes();
    }

    std::size_t entry_capacity() const noexcept {
        return entry_pool_.capacity();
    }

    std::size_t entry_storage_bytes() const noexcept {
        return entry_pool_.allocated_bytes();
    }

    std::uint8_t limb_count() const noexcept {
        return entry_pool_.limb_count();
    }

    std::uint8_t peak_limb_count() const noexcept {
        return entry_pool_.peak_limb_count();
    }

    void swap(RankedStateMap& other) noexcept {
        pages_.swap(other.pages_);
        active_pages_.swap(other.active_pages_);
        page_pool_.swap(other.page_pool_);
        entry_pool_.swap(other.entry_pool_);
        std::swap(active_size_, other.active_size_);
        std::swap(generation_, other.generation_);
    }

private:
    static constexpr std::size_t PAGE_BITS = 12;
    static constexpr std::size_t PAGE_SIZE = std::size_t{1} << PAGE_BITS;
    static constexpr std::size_t PAGE_MASK = PAGE_SIZE - 1;
    static constexpr std::size_t ENTRY_CHUNK_BITS =
        AdaptiveEntryPool::ENTRY_CHUNK_BITS;
    static constexpr std::size_t ENTRY_CHUNK_SIZE =
        AdaptiveEntryPool::ENTRY_CHUNK_SIZE;
    static constexpr std::size_t ENTRY_CHUNK_MASK = ENTRY_CHUNK_SIZE - 1;
    static constexpr std::size_t CHUNKS_PER_PAGE =
        PAGE_SIZE / ENTRY_CHUNK_SIZE;
    static constexpr std::uint16_t LOCAL_INDEX_MASK =
        static_cast<std::uint16_t>(PAGE_SIZE - 1);
    static constexpr int LOCAL_EPOCH_SHIFT = PAGE_BITS;
    static constexpr std::uint8_t MAX_LOCAL_EPOCH = 15;
    static constexpr std::uint32_t INVALID_ENTRY =
        std::numeric_limits<std::uint32_t>::max();

    struct Page {
        void begin_generation(std::uint8_t generation) {
            active_generation = generation;
            size = 0;
            if (local_epoch == MAX_LOCAL_EPOCH) {
                std::fill(slots.begin(), slots.end(), std::uint16_t{0});
                local_epoch = 1;
            } else {
                ++local_epoch;
            }
        }

        std::uint32_t entry_index(std::size_t local_index) const noexcept {
            return chunks[local_index >> ENTRY_CHUNK_BITS] +
                   static_cast<std::uint32_t>(
                       local_index & ENTRY_CHUNK_MASK);
        }

        std::array<std::uint16_t, PAGE_SIZE> slots{};
        std::array<std::uint32_t, CHUNKS_PER_PAGE> chunks;
        std::uint16_t size = 0;
        std::uint8_t active_generation = 0;
        std::uint8_t local_epoch = 0;
    };

    class PagePool {
    public:
        Page* acquire() {
            const std::size_t slab_index = used_ / PAGES_PER_SLAB;
            const std::size_t page_index = used_ % PAGES_PER_SLAB;
            if (slab_index == slabs_.size()) {
                slabs_.push_back(
                    std::make_unique_for_overwrite<Page[]>(PAGES_PER_SLAB));
            }
            ++used_;
            return &slabs_[slab_index][page_index];
        }

        std::size_t allocated_pages() const noexcept {
            return used_;
        }

        std::size_t allocated_bytes() const noexcept {
            return slabs_.size() * PAGES_PER_SLAB * sizeof(Page);
        }

        void swap(PagePool& other) noexcept {
            slabs_.swap(other.slabs_);
            std::swap(used_, other.used_);
        }

    private:
        static constexpr std::size_t PAGES_PER_SLAB = 256;
        std::vector<std::unique_ptr<Page[]>> slabs_;
        std::size_t used_ = 0;
    };

    std::pair<std::uint32_t, bool> ensure_entry(StateKey key) {
        const std::uint32_t state_rank = ranker_->rank(key);
        const std::size_t page_number = state_rank >> PAGE_BITS;
        const std::size_t offset_in_page = state_rank & PAGE_MASK;
        Page*& page_ptr = pages_[page_number];
        if (page_ptr == nullptr) {
            page_ptr = page_pool_.acquire();
        }
        Page& page = *page_ptr;

        if (page.active_generation != generation_) {
            page.begin_generation(generation_);
            active_pages_.push_back(static_cast<std::uint32_t>(page_number));
        }

        const std::uint16_t packed = page.slots[offset_in_page];
        if (static_cast<std::uint8_t>(packed >> LOCAL_EPOCH_SHIFT) ==
            page.local_epoch) {
            const std::size_t local_index = packed & LOCAL_INDEX_MASK;
            return {page.entry_index(local_index), false};
        }

        if (page.size >= PAGE_SIZE) {
            throw std::length_error("rank page contains too many states");
        }
        const std::uint16_t local_index = page.size++;
        if ((local_index & ENTRY_CHUNK_MASK) == 0) {
            page.chunks[local_index >> ENTRY_CHUNK_BITS] =
                entry_pool_.acquire_chunk();
        }
        const std::uint32_t entry_index = page.entry_index(local_index);
        entry_pool_.mark_chunk_size(
            page.chunks[local_index >> ENTRY_CHUNK_BITS],
            static_cast<std::uint16_t>(
                (local_index & ENTRY_CHUNK_MASK) + 1));
        entry_pool_.set_key(entry_index, key);
        page.slots[offset_in_page] = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(page.local_epoch)
             << LOCAL_EPOCH_SHIFT) |
            local_index);
        ++active_size_;
        return {entry_index, true};
    }

    std::uint32_t find_entry(StateKey key) const {
        const std::uint32_t state_rank = ranker_->rank(key);
        const std::size_t page_number = state_rank >> PAGE_BITS;
        const std::size_t offset_in_page = state_rank & PAGE_MASK;
        const Page* page = pages_[page_number];
        if (page == nullptr || page->active_generation != generation_) {
            return INVALID_ENTRY;
        }
        const std::uint16_t packed = page->slots[offset_in_page];
        if (static_cast<std::uint8_t>(packed >> LOCAL_EPOCH_SHIFT) !=
            page->local_epoch) {
            return INVALID_ENTRY;
        }
        return page->entry_index(packed & LOCAL_INDEX_MASK);
    }

    const Page& active_page(std::size_t index) const noexcept {
        return *pages_[active_pages_[index]];
    }

    const StateRanker* ranker_ = nullptr;
    std::vector<Page*> pages_;
    std::vector<std::uint32_t> active_pages_;
    PagePool page_pool_;
    AdaptiveEntryPool entry_pool_;
    std::size_t active_size_ = 0;
    std::uint8_t generation_ = 1;
};

constexpr int MAX_N = 20;
constexpr StateKey PLUG_MASK = 3;
constexpr std::size_t DEADLINE_CHECK_MASK = 0x0FFF;

int width;
int slot_count;
std::uint32_t rank_universe;
std::chrono::steady_clock::time_point probe_deadline;
bool probe_timed_out = false;
int probe_row = 0;
int probe_col = 0;
std::size_t probe_processed = 0;
std::size_t probe_current_size = 0;
std::size_t probe_allocated_pages = 0;
std::size_t probe_page_storage_bytes = 0;
std::size_t probe_entry_capacity = 0;
std::size_t probe_entry_storage_bytes = 0;
std::uint8_t probe_current_limb_count = 1;
std::uint8_t probe_peak_limb_count = 1;

Plug get_plug(StateKey state, int position) {
    return static_cast<Plug>((state >> (2 * position)) & PLUG_MASK);
}

StateKey set_plug(StateKey state, int position, Plug plug) {
    const int shift = 2 * position;
    state &= ~(PLUG_MASK << shift);
    state |= static_cast<StateKey>(plug) << shift;
    return state;
}

StateKey clear_inputs(StateKey state, int position) {
    return state & ~(StateKey{0xF} << (2 * position));
}

// OPEN/CLOSE の対応相手を、非交差な括弧列から探す。
int find_mate(StateKey state, int position) {
    const Plug plug = get_plug(state, position);
    int depth = 1;

    if (plug == OPEN) {
        for (int i = position + 1; i < slot_count; ++i) {
            const Plug current = get_plug(state, i);
            if (current == OPEN) {
                ++depth;
            } else if (current == CLOSE && --depth == 0) {
                return i;
            }
        }
    } else if (plug == CLOSE) {
        for (int i = position - 1; i >= 0; --i) {
            const Plug current = get_plug(state, i);
            if (current == CLOSE) {
                ++depth;
            } else if (current == OPEN && --depth == 0) {
                return i;
            }
        }
    }

    throw std::logic_error("invalid bracket state");
}

void save_storage_stats(const RankedStateMap& current,
                        const RankedStateMap& next) noexcept {
    probe_allocated_pages =
        current.allocated_pages() + next.allocated_pages();
    probe_page_storage_bytes =
        current.page_storage_bytes() + next.page_storage_bytes();
    probe_entry_capacity =
        current.entry_capacity() + next.entry_capacity();
    probe_entry_storage_bytes =
        current.entry_storage_bytes() + next.entry_storage_bytes();
    probe_current_limb_count = std::max(
        current.limb_count(), next.limb_count());
    probe_peak_limb_count = std::max(
        current.peak_limb_count(), next.peak_limb_count());
}

Count count_paths(int n,
                  std::size_t& peak_states,
                  std::uint64_t& transition_count) {
    if (n == 0) {
        peak_states = 1;
        transition_count = 0;
        rank_universe = 1;
        probe_row = 0;
        probe_col = 0;
        probe_processed = 1;
        probe_current_size = 1;
        return Count{1};
    }

    width = n + 1;
    slot_count = width + 1;

    StateRanker ranker(slot_count);
    rank_universe = ranker.universe_size();

    const std::array<std::uint32_t, 4> rank_checks = {
        0U, 1U, rank_universe / 2, rank_universe - 1};
    for (const std::uint32_t state_rank : rank_checks) {
        if (ranker.rank(ranker.unrank(state_rank)) != state_rank) {
            throw std::logic_error("rank/unrank verification failed");
        }
    }

    RankedStateMap current(ranker);
    RankedStateMap next(ranker);
    current.add_one(0);

    peak_states = 1;
    transition_count = 0;

    for (int row = 0; row < width; ++row) {
        // 次の行では左端に EMPTY を挿入し、下向きプラグを一つずらす。
        if (row != 0) {
            next.clear(current.limb_count());
            const std::size_t shift_size = current.size();
            std::size_t processed = 0;
            for (std::size_t page_index = 0;
                 page_index < current.active_page_count(); ++page_index) {
                const std::size_t page_size = current.page_size(page_index);
                for (std::size_t local_index = 0;
                     local_index < page_size; ++local_index) {
                    if ((processed & DEADLINE_CHECK_MASK) == 0 &&
                        std::chrono::steady_clock::now() >= probe_deadline) {
                        probe_timed_out = true;
                        probe_row = row;
                        probe_col = -1;
                        probe_processed = processed;
                        probe_current_size = shift_size;
                        save_storage_stats(current, next);
                        return Count{0};
                    }
                    next.add(current.key_at(page_index, local_index) << 2,
                             current.ways_at(page_index, local_index));
                    ++transition_count;
                    ++processed;
                }
            }
            current.swap(next);
        }

        for (int col = 0; col < width; ++col) {
            next.clear(current.limb_count());
            const bool can_right = col + 1 < width;
            const bool can_down = row + 1 < width;
            const bool is_start = row == 0 && col == 0;
            const bool is_goal = row + 1 == width && col + 1 == width;

            const std::size_t current_size = current.size();
            std::size_t state_index = 0;
            for (std::size_t page_index = 0;
                 page_index < current.active_page_count(); ++page_index) {
                const std::size_t page_size = current.page_size(page_index);
                for (std::size_t local_index = 0;
                     local_index < page_size; ++local_index) {
                if ((state_index & DEADLINE_CHECK_MASK) == 0 &&
                    std::chrono::steady_clock::now() >= probe_deadline) {
                    probe_timed_out = true;
                    probe_row = row;
                    probe_col = col;
                    probe_processed = state_index;
                    probe_current_size = current_size;
                    save_storage_stats(current, next);
                    return Count{0};
                }
                const StateKey state = current.key_at(page_index, local_index);
                const auto ways = current.ways_at(page_index, local_index);
                ++state_index;
                const Plug left = get_plug(state, col);
                const Plug up = get_plug(state, col + 1);
                const int incoming = (left != EMPTY) + (up != EMPTY);
                const StateKey base = clear_inputs(state, col);

                if (is_start) {
                    // 対角線対称性により、最初に右へ進む経路だけを数える。
                    if (can_right) {
                        next.add(set_plug(base, col + 1, START), ways);
                        ++transition_count;
                    }
                    continue;
                }

                if (is_goal) {
                    if (incoming == 1 &&
                        (left == START || up == START) &&
                        base == 0) {
                        next.add(0, ways);
                        ++transition_count;
                    }
                    continue;
                }

                if (incoming == 0) {
                    // この頂点を使わない。
                    next.add(base, ways);
                    ++transition_count;

                    // 新しい通常成分を開始する。
                    if (can_down && can_right) {
                        StateKey output = set_plug(base, col, OPEN);
                        output = set_plug(output, col + 1, CLOSE);
                        next.add(output, ways);
                        ++transition_count;
                    }
                    continue;
                }

                if (incoming == 1) {
                    const Plug plug = left != EMPTY ? left : up;
                    if (can_down) {
                        next.add(set_plug(base, col, plug), ways);
                        ++transition_count;
                    }
                    if (can_right) {
                        next.add(set_plug(base, col + 1, plug), ways);
                        ++transition_count;
                    }
                    continue;
                }

                // 2本の入力をこの頂点で接続する。
                if (left == START || up == START) {
                    if (left == START && up == START) {
                        continue;
                    }
                    const int ordinary_position = left == START ? col + 1 : col;
                    const int mate = find_mate(state, ordinary_position);
                    next.add(set_plug(base, mate, START), ways);
                    ++transition_count;
                } else if (left == OPEN && up == CLOSE) {
                    // 隣接する対応端同士なので、接続すると閉路になる。
                    continue;
                } else if (left == OPEN && up == OPEN) {
                    const int mate = find_mate(state, col + 1);
                    next.add(set_plug(base, mate, OPEN), ways);
                    ++transition_count;
                } else if (left == CLOSE && up == CLOSE) {
                    const int mate = find_mate(state, col);
                    next.add(set_plug(base, mate, CLOSE), ways);
                    ++transition_count;
                } else if (left == CLOSE && up == OPEN) {
                    next.add(base, ways);
                    ++transition_count;
                }
                }
            }

            current.swap(next);
            peak_states = std::max(peak_states, current.size());
        }
    }

    probe_row = width;
    probe_col = width;
    probe_processed = current.size();
    probe_current_size = current.size();
    save_storage_stats(current, next);
    Count answer;
    return current.copy_value(0, answer) ? answer : Count{};
}

bool parse_int_argument(const char* text, int& value) noexcept {
    const std::string_view input(text == nullptr ? "" : text);
    if (input.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(
        input.data(), input.data() + input.size(), value);
    return error == std::errc{} && end == input.data() + input.size();
}
}  // namespace

int main(int argc, char* argv[]) {
    int n = 18;
    int time_limit_seconds = 60;
    if (argc > 3 ||
        (argc > 1 && !parse_int_argument(argv[1], n)) ||
        (argc > 2 && !parse_int_argument(argv[2], time_limit_seconds))) {
        std::cerr << "usage: " << argv[0] << " [n:0.." << MAX_N
                  << "] [time-limit-seconds:1..]\n";
        return 1;
    }
    if (n < 0 || n > MAX_N || time_limit_seconds < 1) {
        std::cerr << "n must be between 0 and " << MAX_N
                  << ", and the time limit must be positive.\n";
        return 1;
    }

    probe_timed_out = false;
    probe_row = 0;
    probe_col = 0;
    probe_processed = 0;
    probe_current_size = 0;
    probe_allocated_pages = 0;
    probe_page_storage_bytes = 0;
    probe_entry_capacity = 0;
    probe_entry_storage_bytes = 0;
    probe_current_limb_count = 1;
    probe_peak_limb_count = 1;
    std::size_t peak_states = 0;
    std::uint64_t transition_count = 0;

    try {
        const auto start = std::chrono::steady_clock::now();
        probe_deadline = start + std::chrono::seconds(time_limit_seconds);
        Count paths = count_paths(n, peak_states, transition_count);
        if (n > 0 && !probe_timed_out) {
            paths += paths;
        }
        const auto finish = std::chrono::steady_clock::now();
        const auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finish - start).count();

        std::cout
            << "algorithm = adaptive-limb packed-page ranked DP (exact 896-bit)\n"
            << "n = " << n << '\n'
            << "limit = " << time_limit_seconds << " seconds\n"
            << "status = "
            << (probe_timed_out ? "TIME_LIMIT" : "COMPLETED") << '\n'
            << "rank universe = " << rank_universe << '\n'
            << "paths = "
            << (probe_timed_out ? std::string("INCOMPLETE")
                                : paths.to_string())
            << '\n'
            << "peak states = " << peak_states << '\n'
            << "transitions = " << transition_count << '\n'
            << "progress row = " << probe_row
            << ", col = " << probe_col
            << ", states = " << probe_processed << '/'
            << probe_current_size << '\n'
            << "allocated rank pages = " << probe_allocated_pages << '\n'
            << "allocated rank page bytes = "
            << probe_page_storage_bytes << '\n'
            << "allocated entry capacity = " << probe_entry_capacity << '\n'
            << "allocated entry bytes = "
            << probe_entry_storage_bytes << '\n'
            << "current count limbs = "
            << static_cast<unsigned>(probe_current_limb_count) << '\n'
            << "peak count limbs = "
            << static_cast<unsigned>(probe_peak_limb_count) << '\n'
            << "elapsed = " << std::fixed << std::setprecision(9)
            << static_cast<double>(elapsed_ns) / 1'000'000'000.0
            << " seconds\n"
            << "elapsed_ns = " << elapsed_ns << '\n';
        return 0;
    } catch (const std::bad_alloc&) {
        std::cerr << "error: out of memory\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
    }
    return 2;
}
