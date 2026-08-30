/*
 * Computing the Number of Paths in a Grid Graph
 * Hiroaki Iwashita <iwashita@erato.ist.hokudai.ac.jp>
 * Copyright (c) 2013 ERATO MINATO Project
 * $Id$
 *
 * Windows/MSVC向けの単純経路専用・計測対応版。
 * License: LICENSE_GGCOUNT_MIT.txt
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <intrin.h>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

typedef uint64_t Code;
typedef uint64_t MateID;

constexpr int MATE_SIZE = 4 * sizeof(MateID);

enum {
    NONE, DEFAULT, VERBOSE
} msg;

constexpr int MAX_N = 20;

int bitcount(uint32_t n) {
    n = ((n & 0xaaaaaaaa) >> 1) + (n & 0x55555555);
    n = ((n & 0xcccccccc) >> 2) + (n & 0x33333333);
    n = ((n & 0xf0f0f0f0) >> 4) + (n & 0x0f0f0f0f);
    n = ((n & 0xff00ff00) >> 8) + (n & 0x00ff00ff);
    return ((n & 0xffff0000) >> 16) + (n & 0x0000ffff);
}

// Dense in-place DP用。配列確保時の二重初期化を避けるため、
// default constructorは意図的に値を初期化しない。
//
// limb 数はコンパイル時パラメータにしてある。この DP は演算でも分岐でもなく
// メモリ転送量で律速されており、実行時間が sizeof(Number) にほぼ比例するため
// （docs/OPTIMIZATION_HEADROOM.md 参照）、要素を細くするのが最も効く。
// n ごとに溢出しない最小の limb 数を limbCountForN() で選ぶ。
template<std::size_t LIMB_COUNT>
class FixedCount {
public:
    static_assert(LIMB_COUNT >= 1, "at least one limb is required");

    static constexpr std::size_t limb_count = LIMB_COUNT;
    static constexpr int BIT_COUNT = static_cast<int>(64 * LIMB_COUNT);

    FixedCount() noexcept {}

    explicit FixedCount(std::uint32_t value) noexcept {
        *this = value;
    }

    FixedCount& operator=(std::uint32_t value) noexcept {
        limbs_[0] = value;
        for (std::size_t limb = 1; limb < LIMB_COUNT; ++limb) {
            limbs_[limb] = 0;
        }
        return *this;
    }

    bool operator==(std::uint32_t value) const noexcept {
        if (limbs_[0] != value) {
            return false;
        }
        for (std::size_t limb = 1; limb < LIMB_COUNT; ++limb) {
            if (limbs_[limb] != 0) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(std::uint32_t value) const noexcept {
        return !(*this == value);
    }

    FixedCount& operator+=(const FixedCount& other) {
        unsigned char carry = 0;
        for (std::size_t limb = 0; limb < LIMB_COUNT; ++limb) {
            carry = _addcarry_u64(
                carry, limbs_[limb], other.limbs_[limb], &limbs_[limb]);
        }
        if (carry != 0) {
            throw std::overflow_error(
                std::string("in-place count exceeded ")
                    + std::to_string(BIT_COUNT) + " bits");
        }
        return *this;
    }

    std::string to_string() const {
        std::string decimal = "0";
        for (int bit = BIT_COUNT - 1; bit >= 0; --bit) {
            unsigned carry = static_cast<unsigned>(
                (limbs_[bit / 64] >> (bit % 64)) & 1);
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
    std::array<std::uint64_t, LIMB_COUNT> limbs_;
};

// 旧名の後方互換エイリアス（tests/ と過去の計測との整合用）。
using Count = FixedCount<6>;

// 各 n について溢出しない最小 limb 数。
//
// 判定に使うのは「最終解」ではなく「DP の途中で value[] / deferred[] が
// 取りうる最大値」である。両者は近いが一致しない（n=18 は解 260 ビットに
// 対してピーク 261 ビット）。
//
// 値は archive/scratch/scratch_peakbits.cpp による実測（n=1..18）と、
// 実走行での溢出検証（n=19 は 5 limb で成功・4 limb で溢出、
// n=20 は 6 limb で成功・5 limb で溢出）による。
//
//   n        1  2  3  4  5  6  7  8 | 9 10 11 12 | 13 14 15 | 16 17 | 18 19 |  20
//   peak     2  4  8 14 21 30 40 52 |66 81 98 116|136 158 181|206 233|261 290|~323
//   limbs    1  1  1  1  1  1  1  1 | 2  2  2   2|  3   3   3|  4   4|  5   5|   6
//
// n=19 までが実測値。n=20 はピークの増分（n が 1 増えるごとに +28〜30 ビット）
// からの外挿だが、実走行で 5 limb は溢出・6 limb は成功するため選択自体は確定。
//
// DP の遷移を変えた場合は必ず測り直すこと。溢出は FixedCount::operator+= が
// std::overflow_error を投げて検出される。
constexpr int limbCountForN(int n) noexcept {
    return n <= 8 ? 1 :
           n <= 12 ? 2 :
           n <= 15 ? 3 :
           n <= 17 ? 4 :
           n <= 19 ? 5 :
           6;
}

enum MateValue {
    N = 0, R = 1, L = 2, X = 3
};

enum MateValuePair {
    NN = 0x0, NR = 0x1, NL = 0x2, NX = 0x3, //
    RN = 0x4, RR = 0x5, RL = 0x6, RX = 0x7, //
    LN = 0x8, LR = 0x9, LL = 0xa, LX = 0xb, //
    XN = 0xc, XR = 0xd, XL = 0xe, XX = 0xf, //
};

std::ostream& operator<<(std::ostream& os, MateValue v) {
    static char const* tbl = ".)(X";
    return os << tbl[v];
}

std::ostream& operator<<(std::ostream& os, MateValuePair w) {
    static char const* tbl = ".)(X";
    return os << tbl[w >> 2] << tbl[w & 3];
}

class Mate {
    MateID value;

    static MateID mask(int k, MateValue u) {
        return MateID(u) << (2 * k);
    }

    static MateID mask(int k, MateValuePair w) {
        return MateID(w) << (2 * (k - 1));
    }

public:
    Mate()
            : value(0) {
    }

    Mate(MateID id)
            : value(id) {
    }

    Mate(int k, MateValue u)
            : value(mask(k, u)) {
    }

    Mate(int k, MateValuePair w)
            : value(mask(k, w)) {
    }

    MateID id() const {
        return value;
    }

    MateValue get(int k) const {
        return MateValue((value >> (2 * k)) & 0x3);
    }

    void set(int k, MateValue v) {
        value = (value & ~mask(k, X)) | mask(k, v);
    }

    MateValuePair getPair(int k) const {
        return MateValuePair((value >> (2 * (k - 1))) & 0xf);
    }

    void setPair(int k, MateValuePair w) {
        value = (value & ~mask(k, XX)) | mask(k, w);
    }

    Mate shrink(int k) const {
        MateID m = (MateID(1) << (2 * k)) - 1;
        MateID h = value & ~m;
        MateID l = value & m;
        return (h >> 2) | l;
    }

    Mate getRight(int k) const {
        return value & ((MateID(1) << (k * 2)) - 1);
    }

    Mate shiftLeft(int k) const {
        return value << (k * 2);
    }

    Mate shiftRight(int k) const {
        return value >> (k * 2);
    }

    Mate operator|(Mate const& o) const {
        return value | o.value;
    }

    Mate const& operator++() {
        ++value;
        if ((value & 3) == 3) {
            MateID v1 = 1;
            MateID v3 = 3;
            do {
                value += v1;
                v1 <<= 2;
                v3 <<= 2;
            } while (v3 != 0 && (value & v3) == v3);
        }
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& os, Mate const& mate) {
        os << "[";
        for (int i = MATE_SIZE - 1; i >= 0; --i) {
            os << mate.get(i);
        }
        return os << "]";
    }
};

struct CodeTable {
    Code base;
    Code size;
    Mate mateL;
    Mate const* mateR;
};

class MateCodec {
    struct Block {
        Code size;
        Mate* mate;

        ~Block() {
            delete[] mate;
        }
    };

    int wL;
    int wR;
    int hL;
    int hR;
    int hC;

    MateID mateSizeL_;
    MateID mateSizeR_;
    Code codeSize_;
    Code codeSizeL_;
    Code* codeLR_;
    Code* codeL_;
    Code* codeR_;
    Block* rightBlock_;
    CodeTable* codeTable_;

public:
    MateCodec(int width, int rightWidth, int leftHeight, int rightHeight) {
        if (width < 1 || width > 31 || leftHeight < 0 || rightHeight < 0) {
            throw std::invalid_argument("unsupported MateCodec dimensions");
        }

        wR = (rightWidth < 1) ? 1 : (width < rightWidth) ? width : rightWidth;
        wL = width - wR;
        hL = leftHeight;
        hR = rightHeight;
        hC = std::min(hL + wL, hR + wR);

        mateSizeL_ = (wL >= 1) ? Mate(wL - 1, X).id() : 1;
        mateSizeR_ = Mate(wR - 1, X).id();
        codeSize_ = 0;
        codeSizeL_ = 0;
        codeLR_ = new Code[mateSizeL_]();
        codeL_ = new Code[mateSizeL_]();
        codeR_ = new Code[mateSizeR_]();

        if (msg == VERBOSE) std::cerr << "Allocated (2 × " << mateSizeL_
                << " + " << mateSizeR_ << ") × " << sizeof(Code) << " = "
                << (mateSizeL_ * 2 + mateSizeR_) * sizeof(Code)
                << " bytes for code tables.\n";

        const std::size_t rightBlockCount =
            static_cast<std::size_t>(hC) + 1;
        rightBlock_ = new Block[rightBlockCount]();

        for (std::size_t blockIndex = 0;
             blockIndex < rightBlockCount; ++blockIndex) {
            const int h = static_cast<int>(blockIndex);
            Code n = motzkin(wR, h, hR);
            rightBlock_[blockIndex].mate = new Mate[n]();

            if (msg == VERBOSE) std::cerr << "Allocated " << n << " × "
                    << sizeof(Mate) << " = " << n * sizeof(Mate)
                    << " bytes for right state list #" << h << ".\n";

            fillCodeR(h, wR, h, hR, Mate());

            assert(rightBlock_[blockIndex].size == n);
        }

        {
            Code n = 0;
            for (int h = 0; h <= hC; ++h) {
                n += motzkin(wL, hL, h);
            }
            codeTable_ = new CodeTable[n]();

            if (msg == VERBOSE) std::cerr << "Allocated " << n << " × "
                    << sizeof(CodeTable) << " = " << n * sizeof(CodeTable)
                    << " bytes for left state list.\n";

            fillCodeL(wL, hL, Mate());

            assert(codeSizeL_ == n);
        }

        //std::cerr << "\n" << *this;
    }

    ~MateCodec() {
        delete[] codeLR_;
        delete[] codeL_;
        delete[] codeR_;
        delete[] rightBlock_;
        delete[] codeTable_;
    }

    int leftWidth() const {
        return wL;
    }

    int rightWidth() const {
        return wR;
    }

    int leftHeight() const {
        return hL;
    }

    int rightHeight() const {
        return hR;
    }

    int centerHeight() const {
        return hC;
    }

    Code codeSize() const {
        return codeSize_;
    }

    Code codeSizeL() const {
        return codeSizeL_;
    }

    CodeTable const& codeTable(Code i) const {
        return codeTable_[i];
    }

    Code encode(Mate const& mate) const {
        MateID mL = mate.shiftRight(wR).id();
        MateID mR = mate.getRight(wR).id();
        assert(mL < mateSizeL_);
        assert(mR < mateSizeR_);
        assert(mL == 0 || codeL_[mL] > 0);
        return codeLR_[mL] + codeR_[mR];
    }

    Code encodeL(Mate const& mateL) const {
        MateID mL = mateL.id();
        assert(mL < mateSizeL_);
        assert(mL == 0 || codeL_[mL] > 0);
        return codeL_[mL];
    }

    friend std::ostream& operator<<(std::ostream& os, MateCodec const& o) {
        for (Code i = 0; i < o.codeSizeL_; ++i) {
            Code c = o.codeTable_[i].base;
            Code n = o.codeTable_[i].size;
            Mate mL = o.codeTable_[i].mateL;
            Mate const* mR = o.codeTable_[i].mateR;
            if (mR == 0) continue;

            os << "block " << i << ": " << c << ".." << c + n - 1 << "\n";
            for (Code j = 0; j < n; ++j) {
                os << "  " << (mL | *mR++) << "\n";
            }
        }
        return os;
    }

private:
    static size_t motzkin(int w, int h1, int h2) {
        static std::vector<std::vector<std::vector<size_t>>> cache;

        if (w == 0 && h1 == h2) return 1;
        if (w <= 0 || h1 < 0 || h2 < 0) return 0;
        if (w < std::abs(h1 - h2)) return 0;

        if (cache.size() <= size_t(w)) cache.resize(w + 1);
        if (cache[w].size() <= size_t(h1)) cache[w].resize(h1 + 1);
        if (cache[w][h1].size() <= size_t(h2)) cache[w][h1].resize(h2 + 1);

        size_t& m = cache[w][h1][h2];
        if (m == 0) {
            m = motzkin(w - 1, h1 + 1, h2) + motzkin(w - 1, h1, h2)
                    + motzkin(w - 1, h1 - 1, h2);
        }
        return m;
    }

    void fillCodeR(int h, int w, int h1, int h2, Mate mR) {
        if (w == 0 && h1 == h2) {
            Block& rt = rightBlock_[h];
            codeR_[mR.id()] = rt.size;
            rt.mate[rt.size++] = mR;
            return;
        }
        if (w <= 0 || h1 < 0 || h2 < 0) return;
        if (w < std::abs(h1 - h2)) return;

        --w;
        fillCodeR(h, w, h1, h2, mR);
        mR.set(w, R);
        fillCodeR(h, w, h1 - 1, h2, mR);
        mR.set(w, L);
        fillCodeR(h, w, h1 + 1, h2, mR);
    }

    void fillCodeL(int w, int h, Mate mL) {
        if (w == 0 && 0 <= h && h <= hC) {
            Block& rt = rightBlock_[h];
            CodeTable& ct = codeTable_[codeSizeL_];
            ct.base = codeLR_[mL.id()] = codeSize_;
            ct.size = rt.size;
            ct.mateL = mL.shiftLeft(wR);
            ct.mateR = rt.mate;
            codeSize_ += rt.size;
            codeL_[mL.id()] = codeSizeL_++;
            return;
        }
        if (w <= 0 || h < 0 || w < h - hC) return;

        --w;
        fillCodeL(w, h, mL);
        mL.set(w, R);
        fillCodeL(w, h - 1, mL);
        mL.set(w, L);
        fillCodeL(w, h + 1, mL);
    }
};

template<typename Number>
class PathCounter {
    int const rows;
    int const cols;
    MateCodec mc;
    MateCodec wc;

    Number* value;
    Number* deferred;

    int groupWidth;
    int numGroups;
    int* groups;
    int progressRow = 0;
    int progressCol = 0;
    std::uint64_t completedUpdates = 0;

public:
    PathCounter(int rows, int cols)
            : rows(rows), cols(cols),
              mc(cols, (cols + 1) / 2, 1, 0),
              wc(cols - 1, cols / 2, 1, 0) {

        value = new Number[mc.codeSize()];
        deferred = new Number[wc.codeSize()];

        if (mc.leftWidth() >= 3) {
            groupWidth = mc.leftWidth() - 2;
            numGroups = 1 << groupWidth;
            groups = new int[numGroups];

            if (msg == VERBOSE) std::cerr << "Allocated " << numGroups << " × "
                    << sizeof(int) << " = " << numGroups * sizeof(int)
                    << " bytes for group list.\n";
        }
        else {
            groupWidth = 0;
            numGroups = 0;
            groups = 0;
        }

        for (int i = 0; i < numGroups; ++i) {
            groups[i] = i;
        }

        if (groups != nullptr) {
            std::sort(groups, groups + numGroups, [](int a, int b) {
                return bitcount(a) > bitcount(b);
            });
        }

        if (msg) std::cerr << "Allocated (" << mc.codeSize() << " + "
                << wc.codeSize() << ") × " << sizeof(Number) << " = "
                << (mc.codeSize() + wc.codeSize()) * sizeof(Number)
                << " bytes for numbers.\n";
    }

    ~PathCounter() {
        delete[] value;
        delete[] deferred;
        delete[] groups;
    }

private:
    void update(int j) {
        int p = cols - j - 1; // bit position
        assert(1 <= p && p < cols);

        if (groups) {
            int ungroupPos =
                    (p - mc.rightWidth() > 1) ? p - mc.rightWidth() : 1;

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
            for (int i = 0; i < numGroups; ++i) {
                updateGroup(p, ungroupPos, groups[i], 0, mc.leftWidth(),
                        mc.leftHeight());
            }
        }
        else {
            for (Code i = 0; i < mc.codeSizeL(); ++i) {
                updateBlock(p, mc.codeTable(i));
            }
        }
    }

    void updateGroup(int p, int ungroupPos, int group, Mate mL, int w,
            int h) const {
        if (w == 0 && 0 <= h && h <= mc.centerHeight()) {
            Code iL = mc.encodeL(mL);
            updateBlock(p, mc.codeTable(iL));
            return;
        }
        if (w <= 0 || h < 0 || w < h - mc.centerHeight()) return; // out of bounds

        --w;
        if (w == ungroupPos || w == ungroupPos - 1) {
            mL.set(w, N), updateGroup(p, ungroupPos, group, mL, w, h);
            mL.set(w, R), updateGroup(p, ungroupPos, group, mL, w, h - 1);
            mL.set(w, L), updateGroup(p, ungroupPos, group, mL, w, h + 1);
        }
        else {
            int k = (w > ungroupPos) ? w - 2 : w;
            if ((group >> k) & 1) {
                mL.set(w, R), updateGroup(p, ungroupPos, group, mL, w, h - 1);
                mL.set(w, L), updateGroup(p, ungroupPos, group, mL, w, h + 1);
            }
            else {
                mL.set(w, N), updateGroup(p, ungroupPos, group, mL, w, h);
            }
        }
    }

    void updateBlock(int p, CodeTable const& block) const {
        for (Code i = 0; i < block.size; ++i) {
            Mate mate = block.mateL | block.mateR[i];
            Number& c = value[block.base + i];
            MateValuePair w = mate.getPair(p);

            switch (w) {
            case NN: // [..] → [()]
                {
                    Number& d = deferred[wc.encode(mate.shrink(p))];

                    if (c != 0) {
                        mate.setPair(p, LR);
                        value[mc.encode(mate)] += c;
                    }

                    c += d;
                    d = 0;
                }
                break;
            case NL: // [.(] ↔ [(.]
            case NR: // [.)] ↔ [).]
                {
                    Number& d = deferred[wc.encode(mate.shrink(p))];

                    mate.setPair(p, (w == NL) ? LN : RN);

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
                        d = tmp;
                    }
                }
                break;
            case LL: // [((---)] → [..---(]
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
                        value[mc.encode(mate)] += c;
                    }
                    else {
                        deferred[wc.encode(mate.shrink(p - 1))] += c;
                    }
                }
                break;
            case RR: // [(---))] → [)---..]
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
                    value[mc.encode(mate)] += c;
                }
                else {
                    deferred[wc.encode(mate.shrink(p - 1))] += c;
                }
                break;
            case RL: // [)(] → [..]
                mate.setPair(p, NN);
                if (p == 1) {
                    value[mc.encode(mate)] += c;
                }
                else {
                    deferred[wc.encode(mate.shrink(p - 1))] += c;
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
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (std::int64_t code = 0;
             code < static_cast<std::int64_t>(mc.codeSize()); ++code) {
            value[static_cast<Code>(code)] = 0;
        }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (std::int64_t code = 0;
             code < static_cast<std::int64_t>(wc.codeSize()); ++code) {
            deferred[static_cast<Code>(code)] = 0;
        }

        value[mc.encode(Mate(cols - 1, R))] = 1;
        completedUpdates = 0;
        progressRow = 0;
        progressCol = 0;

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols - 1; ++col) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                update(col);
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
        return completedUpdates * mc.codeSize();
    }

    int progress_row() const noexcept {
        return progressRow;
    }

    int progress_col() const noexcept {
        return progressCol;
    }
};

bool parse_int_argument(const char* text, int& value) noexcept {
    const std::string_view input(text == nullptr ? "" : text);
    if (input.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(
        input.data(), input.data() + input.size(), value);
    return error == std::errc{} && end == input.data() + input.size();
}

int configured_thread_count() noexcept {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

bool configure_thread_count(int requested_threads) {
#ifdef _OPENMP
    const int available = omp_get_max_threads();
    const int selected = requested_threads > 0
        ? requested_threads
        : std::min(16, available);
    if (selected < 1 || selected > available) {
        return false;
    }
    omp_set_num_threads(selected);
    return true;
#else
    return requested_threads <= 1;
#endif
}

// n を実際に数え上げて結果を表示する。LIMBS は要素の limb 数。
template<std::size_t LIMBS>
int runCount(int n, int timeLimitSeconds,
             const std::chrono::steady_clock::time_point start,
             const std::chrono::steady_clock::time_point deadline) {
    using Number = FixedCount<LIMBS>;

    const int gridSize = n + 1;
    PathCounter<Number> counter(gridSize, gridSize);
    Number paths;
    const bool completed = counter.count(deadline, paths);
    const auto finish = std::chrono::steady_clock::now();
    const auto elapsedNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            finish - start).count();

    std::cout
        << "algorithm = minimal-perfect-hash in-place DP (exact "
        << Number::BIT_COUNT << "-bit)\n"
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
        << "element bytes = " << sizeof(Number) << '\n'
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
}

// limbCountForN() の戻り値ごとに runCount を実体化して振り分ける。
int dispatchCount(int n, int timeLimitSeconds, int forcedLimbs,
                  const std::chrono::steady_clock::time_point start,
                  const std::chrono::steady_clock::time_point deadline) {
    switch (forcedLimbs > 0 ? forcedLimbs : limbCountForN(n)) {
        case 1: return runCount<1>(n, timeLimitSeconds, start, deadline);
        case 2: return runCount<2>(n, timeLimitSeconds, start, deadline);
        case 3: return runCount<3>(n, timeLimitSeconds, start, deadline);
        case 4: return runCount<4>(n, timeLimitSeconds, start, deadline);
        case 5: return runCount<5>(n, timeLimitSeconds, start, deadline);
        default: return runCount<6>(n, timeLimitSeconds, start, deadline);
    }
}

int main(int argc, char* argv[]) {
    int n = 18;
    int timeLimitSeconds = 60;
    int requestedThreads = 0;
    int forcedLimbs = 0;
    if (argc > 5 ||
        (argc > 1 && !parse_int_argument(argv[1], n)) ||
        (argc > 2 && !parse_int_argument(argv[2], timeLimitSeconds)) ||
        (argc > 3 && !parse_int_argument(argv[3], requestedThreads)) ||
        (argc > 4 && !parse_int_argument(argv[4], forcedLimbs))) {
        std::cerr << "usage: " << argv[0] << " [n:0.." << MAX_N
                  << "] [time-limit-seconds:1..] [threads:1..] [limbs:1..6]\n";
        return 1;
    }
    if (n < 0 || n > MAX_N || timeLimitSeconds < 1 ||
        (argc > 3 && requestedThreads < 1) ||
        (argc > 4 && (forcedLimbs < 1 || forcedLimbs > 6)) ||
        !configure_thread_count(requestedThreads)) {
        std::cerr << "n must be between 0 and " << MAX_N
                  << ", the time limit must be positive, the requested "
                     "thread count must be supported, and the limb count must "
                     "be between 1 and 6.\n";
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
            constexpr int zeroLimbs = limbCountForN(0);
            std::cout
                << "algorithm = minimal-perfect-hash in-place DP (exact "
                << FixedCount<zeroLimbs>::BIT_COUNT << "-bit)\n"
                << "n = 0\n"
                << "limit = " << timeLimitSeconds << " seconds\n"
                << "status = COMPLETED\n"
                << "paths = 1\n"
                << "main states = 1\n"
                << "deferred states = 0\n"
                << "number storage bytes = 0\n"
                << "element bytes = " << sizeof(FixedCount<zeroLimbs>) << '\n'
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

        return dispatchCount(n, timeLimitSeconds, forcedLimbs, start, deadline);
    } catch (const std::bad_alloc&) {
        std::cerr << "error: out of memory\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
    }
    return 2;
}
