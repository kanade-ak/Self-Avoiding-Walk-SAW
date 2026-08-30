#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <intrin.h>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
// n = 20 までの結果を保持できる 384 ビット符号なし整数。
class BigUInt {
public:
    BigUInt(std::uint64_t value = 0) {
        limbs_[0] = value;
    }

    BigUInt& operator+=(const BigUInt& other) {
        unsigned char carry = 0;
        carry = _addcarry_u64(carry, limbs_[0], other.limbs_[0], &limbs_[0]);
        carry = _addcarry_u64(carry, limbs_[1], other.limbs_[1], &limbs_[1]);
        carry = _addcarry_u64(carry, limbs_[2], other.limbs_[2], &limbs_[2]);
        carry = _addcarry_u64(carry, limbs_[3], other.limbs_[3], &limbs_[3]);
        carry = _addcarry_u64(carry, limbs_[4], other.limbs_[4], &limbs_[4]);
        carry = _addcarry_u64(carry, limbs_[5], other.limbs_[5], &limbs_[5]);
        if (carry != 0) {
            throw std::overflow_error("BigUInt exceeded 384 bits");
        }
        return *this;
    }

    std::string to_string() const {
        std::string decimal = "0";
        for (int bit = 383; bit >= 0; --bit) {
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
    std::array<std::uint64_t, 6> limbs_{};
};

// n=20 の321ビット結果まで保持できる384ビット整数。
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
        completions_[0][0] = 1;
        for (int remaining = 1; remaining <= motzkin_length_; ++remaining) {
            for (int height = 0; height <= motzkin_length_; ++height) {
                std::uint32_t count = completions_[remaining - 1][height];
                if (height + 1 <= motzkin_length_) {
                    count += completions_[remaining - 1][height + 1];
                }
                if (height > 0) {
                    count += completions_[remaining - 1][height - 1];
                }
                completions_[remaining][height] = count;
            }
        }

        motzkin_count_ = completions_[motzkin_length_][0];
        universe_size_ = 1 +
            static_cast<std::uint32_t>(slot_count_) * motzkin_count_;

        // 第2表は開始高さごとに複製されるため、奇数長では第1表を長くする。
        first_length_ = (motzkin_length_ + 1) / 2;
        second_length_ = motzkin_length_ - first_length_;
        first_code_count_ = std::size_t{1} << (2 * first_length_);
        second_code_count_ = std::size_t{1} << (2 * second_length_);
        first_mask_ = first_code_count_ - 1;

        first_table_ = build_chunk_table(0, first_length_, 0);
        second_table_ = build_final_chunk_table(
            first_length_, second_length_, first_length_);
    }

    std::uint32_t rank(StateKey state) const {
        if (state == 0) {
            return 0;
        }

        constexpr StateKey EVEN_BITS = 0x5555555555555555ULL;
        const StateKey start_bits = state & (state >> 1) & EVEN_BITS;
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
        const std::uint64_t first = first_table_[first_code];
        const std::uint32_t middle_height =
            static_cast<std::uint32_t>((first >> 32) - 1);
        const std::uint32_t second = second_table_[
            static_cast<std::size_t>(middle_height) * second_code_count_ +
            second_code];

        return 1 + static_cast<std::uint32_t>(start_position) * motzkin_count_ +
               static_cast<std::uint32_t>(first) + second;
    }

    StateKey unrank(std::uint32_t state_rank) const {
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
            const std::uint32_t empty_count = completions_[remaining][height];
            if (word_rank >= empty_count) {
                word_rank -= empty_count;
                const std::uint32_t open_count =
                    completions_[remaining][height + 1];
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
    std::vector<std::uint64_t> build_chunk_table(
        int first_position, int length, int max_start_height) const {
        const std::size_t code_count = std::size_t{1} << (2 * length);
        std::vector<std::uint64_t> table(
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
                          code] =
                        (static_cast<std::uint64_t>(height + 1) << 32) |
                        word_rank;
                }
            }
        }
        return table;
    }

    std::vector<std::uint32_t> build_final_chunk_table(
        int first_position, int length, int max_start_height) const {
        const std::vector<std::uint64_t> packed = build_chunk_table(
            first_position, length, max_start_height);
        std::vector<std::uint32_t> table(packed.size());
        std::transform(packed.begin(), packed.end(), table.begin(),
                       [](std::uint64_t entry) {
                           return static_cast<std::uint32_t>(entry);
                       });
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
    std::vector<std::uint64_t> first_table_;
    std::vector<std::uint32_t> second_table_;
};

// 各64K rankページ内では16ビットのローカル添字を使う。
// 状態本体は256件単位の再利用プールへ置き、巨大vectorの再確保と
// ページごとの多数のヒープ確保を避ける。
class RankedStateMap {
public:
    RankedStateMap(const StateRanker& ranker, std::size_t expected_entries)
        : ranker_(&ranker),
          pages_((static_cast<std::size_t>(ranker.universe_size()) + PAGE_SIZE - 1) /
                 PAGE_SIZE) {
        (void)expected_entries;
        active_pages_.reserve(pages_.size());
    }

    void clear() {
        active_pages_.clear();
        active_size_ = 0;
        entry_pool_.clear();
        ++generation_;
        if (generation_ == 0) {
            for (const auto& page : pages_) {
                if (page) {
                    std::fill(page->stamps.begin(), page->stamps.end(), 0);
                    page->active_generation = 0;
                }
            }
            generation_ = 1;
        }
    }

    void add(StateKey key, const Count& ways) {
        const std::uint32_t state_rank = ranker_->rank(key);
        const std::size_t page_number = state_rank >> PAGE_BITS;
        const std::size_t offset = state_rank & (PAGE_SIZE - 1);
        if (!pages_[page_number]) {
            pages_[page_number] = std::make_unique<Page>();
        }
        Page& page = *pages_[page_number];

        if (page.active_generation != generation_) {
            page.active_generation = generation_;
            page.size = 0;
            active_pages_.push_back(static_cast<std::uint32_t>(page_number));
        }

        if (page.stamps[offset] != generation_) {
            if (page.size >= PAGE_SIZE) {
                throw std::length_error("rank page contains too many states");
            }
            const std::uint16_t local_index =
                static_cast<std::uint16_t>(page.size++);
            if ((local_index & ENTRY_CHUNK_MASK) == 0) {
                page.chunks[local_index >> ENTRY_CHUNK_BITS] =
                    entry_pool_.acquire();
            }
            page.stamps[offset] = generation_;
            page.indices[offset] = local_index;
            page.set(local_index, key, ways);
            ++active_size_;
        } else {
            page.ways(page.indices[offset]) += ways;
        }
    }

    const Count* find(StateKey key) const {
        const std::uint32_t state_rank = ranker_->rank(key);
        const std::size_t page_number = state_rank >> PAGE_BITS;
        const std::size_t offset = state_rank & (PAGE_SIZE - 1);
        if (!pages_[page_number] ||
            pages_[page_number]->stamps[offset] != generation_) {
            return nullptr;
        }
        const Page& page = *pages_[page_number];
        return &page.ways(page.indices[offset]);
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
        return active_page(active_page_index).key(local_index);
    }

    const Count& ways_at(std::size_t active_page_index,
                         std::size_t local_index) const noexcept {
        return active_page(active_page_index).ways(local_index);
    }

    std::size_t allocated_pages() const {
        return static_cast<std::size_t>(std::count_if(
            pages_.begin(), pages_.end(),
            [](const auto& page) { return static_cast<bool>(page); }));
    }

    std::size_t entry_capacity() const noexcept {
        return entry_pool_.capacity();
    }

    void swap(RankedStateMap& other) noexcept {
        pages_.swap(other.pages_);
        active_pages_.swap(other.active_pages_);
        entry_pool_.swap(other.entry_pool_);
        std::swap(active_size_, other.active_size_);
        std::swap(generation_, other.generation_);
    }

private:
    static constexpr std::size_t PAGE_BITS = 16;
    static constexpr std::size_t PAGE_SIZE = std::size_t{1} << PAGE_BITS;
    static constexpr std::size_t ENTRY_CHUNK_BITS = 8;
    static constexpr std::size_t ENTRY_CHUNK_SIZE =
        std::size_t{1} << ENTRY_CHUNK_BITS;
    static constexpr std::size_t ENTRY_CHUNK_MASK = ENTRY_CHUNK_SIZE - 1;
    static constexpr std::size_t CHUNKS_PER_PAGE =
        PAGE_SIZE / ENTRY_CHUNK_SIZE;
    static constexpr std::size_t CHUNKS_PER_SLAB = 256;

    struct EntryChunk {
        void set(std::size_t index, StateKey key, const Count& ways) noexcept {
            ::new (static_cast<void*>(key_storage + index * sizeof(StateKey)))
                StateKey(key);
            ::new (static_cast<void*>(ways_storage + index * sizeof(Count)))
                Count(ways);
        }

        StateKey key(std::size_t index) const noexcept {
            return *std::launder(reinterpret_cast<const StateKey*>(
                key_storage + index * sizeof(StateKey)));
        }

        Count& ways(std::size_t index) noexcept {
            return *std::launder(reinterpret_cast<Count*>(
                ways_storage + index * sizeof(Count)));
        }

        const Count& ways(std::size_t index) const noexcept {
            return *std::launder(reinterpret_cast<const Count*>(
                ways_storage + index * sizeof(Count)));
        }

        alignas(StateKey) unsigned char
            key_storage[ENTRY_CHUNK_SIZE * sizeof(StateKey)];
        alignas(Count) unsigned char
            ways_storage[ENTRY_CHUNK_SIZE * sizeof(Count)];
    };

    class EntryPool {
    public:
        EntryChunk* acquire() {
            const std::size_t slab_index = used_ / CHUNKS_PER_SLAB;
            const std::size_t chunk_index = used_ % CHUNKS_PER_SLAB;
            if (slab_index == slabs_.size()) {
                slabs_.push_back(
                    std::make_unique_for_overwrite<EntryChunk[]>(CHUNKS_PER_SLAB));
            }
            ++used_;
            return &slabs_[slab_index][chunk_index];
        }

        void clear() noexcept {
            used_ = 0;
        }

        void swap(EntryPool& other) noexcept {
            slabs_.swap(other.slabs_);
            std::swap(used_, other.used_);
        }

        std::size_t capacity() const noexcept {
            return slabs_.size() * CHUNKS_PER_SLAB * ENTRY_CHUNK_SIZE;
        }

    private:
        std::vector<std::unique_ptr<EntryChunk[]>> slabs_;
        std::size_t used_ = 0;
    };

    struct Page {
        void set(std::size_t local_index,
                 StateKey key,
                 const Count& value) noexcept {
            chunks[local_index >> ENTRY_CHUNK_BITS]->set(
                local_index & ENTRY_CHUNK_MASK, key, value);
        }

        StateKey key(std::size_t local_index) const noexcept {
            return chunks[local_index >> ENTRY_CHUNK_BITS]->key(
                local_index & ENTRY_CHUNK_MASK);
        }

        Count& ways(std::size_t local_index) noexcept {
            return chunks[local_index >> ENTRY_CHUNK_BITS]->ways(
                local_index & ENTRY_CHUNK_MASK);
        }

        const Count& ways(std::size_t local_index) const noexcept {
            return chunks[local_index >> ENTRY_CHUNK_BITS]->ways(
                local_index & ENTRY_CHUNK_MASK);
        }

        std::array<std::uint8_t, PAGE_SIZE> stamps{};
        std::array<std::uint16_t, PAGE_SIZE> indices;
        std::array<EntryChunk*, CHUNKS_PER_PAGE> chunks{};
        std::size_t size = 0;
        std::uint8_t active_generation = 0;
    };

    const Page& active_page(std::size_t index) const noexcept {
        return *pages_[active_pages_[index]];
    }

    const StateRanker* ranker_ = nullptr;
    std::vector<std::unique_ptr<Page>> pages_;
    std::vector<std::uint32_t> active_pages_;
    EntryPool entry_pool_;
    std::size_t active_size_ = 0;
    std::uint8_t generation_ = 1;
};

constexpr int MAX_N = 20;
constexpr StateKey PLUG_MASK = 3;

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
std::size_t probe_entry_capacity = 0;

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
    probe_entry_capacity =
        current.entry_capacity() + next.entry_capacity();
}

std::size_t reserve_hint_for(int grid_width) {
    return grid_width >= 18 ? 25'000'000 :
           grid_width == 17 ? 9'000'000 :
           grid_width == 16 ? 3'200'000 :
           grid_width == 15 ? 1'200'000 :
           grid_width == 14 ? 450'000 :
           grid_width == 13 ? 160'000 :
           grid_width == 12 ? 60'000 :
           grid_width == 11 ? 22'000 :
           grid_width == 10 ? 8000 : 4096;
}

Count count_paths(int n,
                  std::size_t& peak_states,
                  std::uint64_t& transition_count) {
    if (n == 0) {
        peak_states = 1;
        transition_count = 0;
        rank_universe = 1;
        return Count{1};
    }

    width = n + 1;
    slot_count = width + 1;

    const std::size_t reserve_hint = reserve_hint_for(width);
    StateRanker ranker(slot_count);
    rank_universe = ranker.universe_size();

    const std::array<std::uint32_t, 4> rank_checks = {
        0U, 1U, rank_universe / 2, rank_universe - 1};
    for (const std::uint32_t state_rank : rank_checks) {
        if (ranker.rank(ranker.unrank(state_rank)) != state_rank) {
            throw std::logic_error("rank/unrank verification failed");
        }
    }

    RankedStateMap current(ranker, reserve_hint);
    RankedStateMap next(ranker, reserve_hint);
    current.add(0, Count{1});

    peak_states = 1;
    transition_count = 0;

    for (int row = 0; row < width; ++row) {
        // 次の行では左端に EMPTY を挿入し、下向きプラグを一つずらす。
        if (row != 0) {
            next.clear();
            const std::size_t shift_size = current.size();
            std::size_t processed = 0;
            for (std::size_t page_index = 0;
                 page_index < current.active_page_count(); ++page_index) {
                const std::size_t page_size = current.page_size(page_index);
                for (std::size_t local_index = 0;
                     local_index < page_size; ++local_index) {
                    if ((processed & 0x3FFF) == 0 &&
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
            next.clear();
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
                if ((state_index & 0x3FFF) == 0 &&
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
                const Count& ways = current.ways_at(page_index, local_index);
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

    save_storage_stats(current, next);
    const Count* answer = current.find(0);
    return answer == nullptr ? Count{} : *answer;
}
}  // namespace

int main(int argc, char* argv[]) {
    const int n = argc > 1 ? std::atoi(argv[1]) : 18;
    const int time_limit_seconds = argc > 2 ? std::max(1, std::atoi(argv[2])) : 60;
    if (n < 0 || n > MAX_N) {
        std::cerr << "n must be between 0 and " << MAX_N << ".\n";
        return 1;
    }

    probe_timed_out = false;
    probe_processed = 0;
    probe_current_size = 0;
    probe_allocated_pages = 0;
    probe_entry_capacity = 0;
    std::size_t peak_states = 0;
    std::uint64_t transition_count = 0;

    const auto start = std::chrono::steady_clock::now();
    probe_deadline = start + std::chrono::seconds(time_limit_seconds);
    Count paths = count_paths(n, peak_states, transition_count);
    if (n > 0 && !probe_timed_out) {
        paths += paths;
    }
    const auto finish = std::chrono::steady_clock::now();
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();

    std::cout << "algorithm = optimized local-page chunk-pool ranked DP (exact 384-bit)\n"
              << "n = " << n << '\n'
              << "limit = " << time_limit_seconds << " seconds\n"
              << "status = " << (probe_timed_out ? "TIME_LIMIT" : "COMPLETED") << '\n'
              << "rank universe = " << rank_universe << '\n'
              << "paths = " << (probe_timed_out ? std::string("INCOMPLETE") : paths.to_string()) << '\n'
              << "peak states = " << peak_states << '\n'
              << "transitions = " << transition_count << '\n'
              << "progress row = " << probe_row
              << ", col = " << probe_col
              << ", states = " << probe_processed << '/' << probe_current_size << '\n'
              << "allocated rank pages = " << probe_allocated_pages << '\n'
              << "allocated entry capacity = " << probe_entry_capacity << '\n'
              << "elapsed = " << std::fixed << std::setprecision(9)
              << static_cast<double>(elapsed_ns) / 1'000'000'000.0
              << " seconds\n"
              << "elapsed_ns = " << elapsed_ns << '\n';
}
