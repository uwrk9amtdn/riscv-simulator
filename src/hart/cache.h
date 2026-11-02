#include <unordered_map>
#include <common.h>

template <typename KeyT, typename DataT>
class cache {
public:
    void invalidate(KeyT key) {
        mem.erase(key);
    }

    void flush() {
        mem.clear();
    }

    void set(KeyT key, DataT data) {
        mem[key] = data;
    }

    bool get(KeyT key, DataT& data) {
        if (mem.contains(key)) {
            data = mem[key];
            return true;
        } else {
            return false;
        }
    }
private:
    std::unordered_map<u32, u32> mem;
};