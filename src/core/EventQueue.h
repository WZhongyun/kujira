#pragma once

#include <deque>
#include <mutex>
#include <vector>

// Thread-safe queue: the HTTP thread pushes, the main thread drains once per frame.
template <typename T>
class EventQueue
{
public:
    void Push(T item)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_items.size() >= kMaxItems)
        {
            _items.pop_front();
        }
        _items.push_back(std::move(item));
    }

    std::vector<T> Drain()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<T> out(std::make_move_iterator(_items.begin()), std::make_move_iterator(_items.end()));
        _items.clear();
        return out;
    }

private:
    static constexpr size_t kMaxItems = 256;
    std::mutex _mutex;
    std::deque<T> _items;
};
