#include "Input/BindingTable.hpp"

#include <algorithm>

namespace kc {

    Chord::Chord(std::initializer_list<int> values) {
        for (int key : values) {
            if (count == MAX_KEYS) break;
            keys[count++] = key;
        }
    }

    bool Chord::contains(int key) const {
        for (uint8_t i = 0; i < count; ++i) {
            if (keys[i] == key) return true;
        }
        return false;
    }

    bool Chord::isSubsetOf(const Chord& other) const {
        if (count > other.count) return false;
        for (uint8_t i = 0; i < count; ++i) {
            if (!other.contains(keys[i])) return false;
        }
        return true;
    }

    bool Chord::operator==(const Chord& other) const {
        if (count != other.count) return false;
        for (uint8_t i = 0; i < count; ++i) {
            if (keys[i] != other.keys[i]) return false;
        }
        return true;
    }

} // namespace kc
