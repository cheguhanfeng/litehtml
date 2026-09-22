// Opt-in, buffered wall-clock spans for one-pass attribution. No optimizer changes.
// Keep the host copy byte-identical except for module_name.
#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace lh_single_profile
{
    inline constexpr const char* module_name = "litehtml";
    inline const char* directory()
    {
        static const char* value = std::getenv("LH_SINGLE_PROFILE_DIR");
        return value && *value ? value : nullptr;
    }
    inline uint64_t now_ns()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    struct event { uint64_t start, end; const char* category; const char* name; uint64_t key; };
    struct buffer
    {
        std::vector<event> events;
        uint64_t dropped = 0;
        unsigned chunk = 0;
        static constexpr size_t capacity = 2000000;
        buffer() { events.reserve(capacity); }
        void flush()
        {
            if(events.empty() && !dropped) return;
            char path[2048];
            std::snprintf(path, sizeof(path), "%s/%s-%llu-%u.jsonl", directory(), module_name,
                static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id())), chunk++);
            if(auto* file = std::fopen(path, "wb"))
            {
                std::setvbuf(file, nullptr, _IOFBF, 1024 * 1024);
                std::fprintf(file, "{\"schema\":1,\"module\":\"%s\",\"clock\":\"steady_ns\",\"dropped\":%llu,\"count\":%llu}\n",
                    module_name, static_cast<unsigned long long>(dropped), static_cast<unsigned long long>(events.size()));
                for(const auto& e : events)
                    std::fprintf(file, "[%llu,%llu,\"%s\",\"%s\",%llu]\n",
                        static_cast<unsigned long long>(e.start), static_cast<unsigned long long>(e.end),
                        e.category, e.name, static_cast<unsigned long long>(e.key));
                std::fclose(file);
                events.clear(); dropped = 0;
            }
        }
        ~buffer() { flush(); }
    };
    inline buffer& current() { static thread_local buffer value; return value; }
    inline void flush() { if(directory()) current().flush(); }
    class scope
    {
        buffer* target;
        event value;
    public:
        scope(const char* category, const char* name, uint64_t key = 0)
            : target(directory() ? &current() : nullptr), value{target ? now_ns() : 0, 0, category, name, key} {}
        ~scope()
        {
            if(!target) return;
            value.end = now_ns();
            if(target->events.size() < buffer::capacity) target->events.push_back(value);
            else ++target->dropped;
        }
        scope(const scope&) = delete;
        scope& operator=(const scope&) = delete;
    };
    template<class F> decltype(auto) run(const char* category, const char* name, F&& fn)
    {
        scope timer(category, name);
        return fn();
    }
}
