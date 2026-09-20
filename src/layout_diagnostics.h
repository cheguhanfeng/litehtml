#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <cstdint>

namespace litehtml::layout_diagnostics
{
    enum phase { styles, matching, compute, topology, tree_create, tree_init, table_fix,
                 flow, positioned, extent, font_lookup, parallel_snapshot, parallel_wait, parallel_commit, phase_count };
    struct sample
    {
        uint64_t ns[phase_count] = {};
        uint64_t calls = 0, content_calls = 0, second_pass_calls = 0;
        unsigned full_calls = 0;
        uint64_t parallel_rows = 0, parallel_workers = 0, parallel_fallbacks = 0;
        uint64_t topology_snapshots = 0;
        const char* fallback = "none";
    };
    inline thread_local sample* active = nullptr;
    using clock = std::chrono::steady_clock;
    inline uint64_t elapsed(clock::time_point start)
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count());
    }
    class scope
    {
        sample* target;
        phase slot;
        clock::time_point start;
    public:
        explicit scope(phase value) : target(active), slot(value), start(target ? clock::now() : clock::time_point{}) {}
        ~scope() { if(target) target->ns[slot] += elapsed(start); }
    };
    template<class F> decltype(auto) run(phase value, F&& fn)
    {
        scope timer(value);
        return fn();
    }
    // One bounded record per document call; no per-node strings, clocks or allocations.
    // Nested phases are inclusive. This is opt-in diagnosis, never acceptance timing.
    class invocation
    {
        sample data;
        bool owner = false;
        const void* document;
        const char* entry;
        clock::time_point start;
        static const char* path()
        {
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
            static const char* value = std::getenv("LITEHTML_LAYOUT_PROFILE_PATH");
            return value && *value ? value : nullptr;
#else
            return nullptr;
#endif
        }
    public:
        invocation(const void* doc, const char* name) : document(doc), entry(name)
        {
            if(!active && path()) { owner = true; active = &data; start = clock::now(); }
        }
        ~invocation()
        {
            if(!owner) return;
            const auto total = elapsed(start);
            active = nullptr;
            static std::mutex output_mutex;
            std::lock_guard<std::mutex> lock(output_mutex);
            if(auto* file = std::fopen(path(), "a"))
            {
                std::fprintf(file, "{\"document\":\"%p\",\"entry\":\"%s\",\"fallback\":\"%s\",\"totalNs\":%llu,\"fullCalls\":%u,\"renderCalls\":%llu,\"contentCalls\":%llu,\"secondPassCalls\":%llu,\"phasesNs\":{",
                             document, entry, data.fallback, static_cast<unsigned long long>(total), data.full_calls,
                             static_cast<unsigned long long>(data.calls), static_cast<unsigned long long>(data.content_calls),
                             static_cast<unsigned long long>(data.second_pass_calls));
                const char* names[] = {"styles", "matching", "compute", "topology", "treeCreate", "treeInit", "tableFix", "flow", "positioned", "extent", "fontLookup", "parallelSnapshot", "parallelWait", "parallelCommit"};
                for(unsigned i = 0; i < phase_count; ++i)
                    std::fprintf(file, "%s\"%s\":%llu", i ? "," : "", names[i], static_cast<unsigned long long>(data.ns[i]));
                std::fprintf(file, "},\"parallelRows\":%llu,\"parallelWorkersMax\":%llu,\"parallelFallbacks\":%llu,\"topologySnapshots\":%llu}\n",
                    static_cast<unsigned long long>(data.parallel_rows), static_cast<unsigned long long>(data.parallel_workers),
                    static_cast<unsigned long long>(data.parallel_fallbacks), static_cast<unsigned long long>(data.topology_snapshots));
                std::fclose(file);
            }
        }
    };
}

#define LH_LAYOUT_PHASE(name, expression) \
    ::litehtml::layout_diagnostics::run(::litehtml::layout_diagnostics::name, [&]() -> decltype(auto) { return expression; })
