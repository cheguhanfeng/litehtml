// Architecture diagnostic only. No production DOM is shared or committed.
// Every worker owns a complete private document; inputs and results are values.
#include "litehtml/litehtml_layout.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;
double milliseconds(clock_type::time_point a, clock_type::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
struct handle {
    litehtml_layout_element* p;
    explicit handle(litehtml_layout_element* value) : p(value) { require(p != nullptr, "missing element"); }
    ~handle() { litehtml_layout_element_destroy(p); }
    handle(const handle&) = delete;
};
struct row_input { int id; std::string html; };
struct row_result { int id; std::vector<litehtml_rect> boxes; std::vector<std::string> text; };
struct batch_result {
    std::vector<row_result> rows;
    float height = 0;
    double parse = 0, query = 0, render = 0, extract = 0;
};
struct document {
    litehtml_layout_callbacks callbacks{};
    litehtml_layout_service* service = nullptr;
    int viewport_width = 1920;
    explicit document(const std::string& source) {
        service = litehtml_layout_create(&callbacks);
        require(service != nullptr, "create failed");
        if(!litehtml_layout_load_html(service, source.c_str(), "", 1920, 1080)) {
            litehtml_layout_destroy(service); service = nullptr;
            throw std::runtime_error("load failed");
        }
    }
    ~document() { if(service) litehtml_layout_destroy(service); }
    document(const document&) = delete;
    batch_result run(const std::vector<row_input>& input, int width) {
        batch_result result;
        std::string html;
        auto start = clock_type::now();
        if(width != viewport_width) {
            require(litehtml_layout_set_viewport(service, static_cast<float>(width), 1080) != 0, "viewport failed");
            viewport_width = width;
        }
        for(const auto& row : input) html += row.html;
        handle rows(litehtml_layout_get_element_by_id(service, "rows"));
        require(litehtml_layout_element_set_inner_html(rows.p, html.c_str()) != 0, "replace failed");
        auto parsed = clock_type::now();
        for(const auto& row : input) {
            handle quantity(litehtml_layout_get_element_by_id(service, ("qty-" + std::to_string(row.id)).c_str()));
        }
        auto queried = clock_type::now();
        require(litehtml_layout_render(service, static_cast<float>(width), 0) != 0, "render failed");
        auto rendered = clock_type::now();
        litehtml_rect root{};
        require(litehtml_layout_element_get_placement(rows.p, &root) != 0, "rows placement failed");
        result.height = root.height;
        for(const auto& input_row : input) {
            handle row(litehtml_layout_get_element_by_id(service, ("item-" + std::to_string(input_row.id)).c_str()));
            row_result value; value.id = input_row.id;
            litehtml_rect rect{};
            require(litehtml_layout_element_get_placement(row.p, &rect) != 0, "row placement failed");
            require(rect.width > 0 && rect.height > 0, "row placement must be nonempty");
            rect.y -= root.y;
            value.boxes.push_back(rect);
            const int children = litehtml_layout_element_get_child_count(row.p);
            for(int i = 0; i < children; ++i) {
                handle child(litehtml_layout_element_get_child(row.p, i));
                require(litehtml_layout_element_get_placement(child.p, &rect) != 0, "child placement failed");
                rect.y -= root.y;
                value.boxes.push_back(rect);
                const auto* text = litehtml_layout_element_get_text(child.p);
                value.text.emplace_back(text ? text : "");
            }
            result.rows.push_back(std::move(value));
        }
        result.parse = milliseconds(start, parsed);
        result.query = milliseconds(parsed, queried);
        result.render = milliseconds(queried, rendered);
        result.extract = milliseconds(rendered, clock_type::now());
        return result;
    }
};

// A fixed set of reusable threads. A private service is created and destroyed on
// its owner thread. Never return native handles across this boundary.
class worker {
    std::mutex mutex;
    std::condition_variable wake;
    std::queue<std::function<void()>> jobs;
    bool stopping = false;
    std::unique_ptr<document> doc;
    std::thread thread;
public:
    worker() : thread([this] {
        for(;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stopping || !jobs.empty(); });
                if(stopping && jobs.empty()) break;
                job = std::move(jobs.front()); jobs.pop();
            }
            job();
        }
        doc.reset();
    }) {}
    ~worker() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        wake.notify_one(); thread.join();
    }
    template<class F> auto submit(F fn) {
        using result_type = decltype(fn());
        auto task = std::make_shared<std::packaged_task<result_type()>>(std::move(fn));
        auto future = task->get_future();
        { std::lock_guard<std::mutex> lock(mutex); jobs.emplace([task] { (*task)(); }); }
        wake.notify_one(); return future;
    }
    void initialize(const std::string& source) {
        submit([this, &source] { doc = std::make_unique<document>(source); }).get();
    }
    auto run(std::vector<row_input> input, int width) {
        return submit([this, input = std::move(input), width] { return doc->run(input, width); });
    }
};

std::vector<row_input> make_rows(int count, int iteration) {
    std::vector<row_input> result;
    for(int i = 0; i < count; ++i) {
        const auto id = std::to_string(i);
        std::ostringstream name; name << std::setfill('0') << std::setw(4) << i;
        const std::string label = i % 3 == 0 ? "精密组件 Precision component " : "仓储配件 Item ";
        result.push_back({i, "<div class=\"row" + std::string(i == 0 ? " selected" : "") +
            "\" id=\"item-" + id + "\" data-id=\"" + id + "\"><img src=\"../fixtures/image_fixture.png\">"
            "<span class=\"name\">" + label + name.str() + "</span><span class=\"state\">" +
            (i % 3 == 0 ? "待处理 Pending" : "已同步 Ready") + "</span><span class=\"qty\" id=\"qty-" + id +
            "\">" + std::to_string(20 + (i + iteration) % 73) + "</span></div>"});
    }
    if(iteration % 2) std::reverse(result.begin(), result.end());
    return result;
}
void compare(const batch_result& expected, const batch_result& actual) {
    require(std::abs(expected.height - actual.height) < .01f, "merged list height mismatch");
    require(expected.rows.size() == actual.rows.size(), "row count mismatch");
    for(size_t i = 0; i < expected.rows.size(); ++i) {
        const auto& a = expected.rows[i]; const auto& b = actual.rows[i];
        require(a.id == b.id && a.text == b.text && a.boxes.size() == b.boxes.size(), "row identity/text mismatch");
        for(size_t j = 0; j < a.boxes.size(); ++j) {
            const auto& x = a.boxes[j]; const auto& y = b.boxes[j];
            if(!(std::abs(x.x-y.x) < .01f && std::abs(x.y-y.y) < .01f &&
                    std::abs(x.width-y.width) < .01f && std::abs(x.height-y.height) < .01f)) {
                std::cerr << "row=" << a.id << " box=" << j << " expected=" << x.x << ',' << x.y << ',' << x.width << ',' << x.height
                          << " actual=" << y.x << ',' << y.y << ',' << y.width << ',' << y.height << '\n';
                throw std::runtime_error("merged box mismatch");
            }
        }
    }
}
int main(int argc, char** argv) try {
    require(argc == 3, "usage: probe fixture-with-inline-css.html samples.csv");
    std::ifstream input(argv[1], std::ios::binary);
    require(input.good(), "fixture open failed");
    std::string source((std::istreambuf_iterator<char>(input)), {});
    auto startup = clock_type::now();
    document owner(source);
    // _s returns references into the process atom vector. Seed every ID/class and
    // both media states before concurrency. This fixed-vocabulary experiment is
    // not evidence that concurrent arbitrary document parsing is safe.
    owner.run(make_rows(800, 0), 1920);
    owner.run(make_rows(800, 1), 800);
    std::vector<std::unique_ptr<worker>> workers;
    for(int i = 0; i < 4; ++i) {
        workers.push_back(std::make_unique<worker>());
        workers.back()->initialize(source);
        workers.back()->run(make_rows(800, 0), 1920).get();
    }
    std::cerr << "startupAndPrewarmMs=" << milliseconds(startup, clock_type::now()) << '\n';
    std::ofstream output(argv[2]);
    require(output.good(), "output open failed");
    output << "rows,width,iteration,workers,snapshotMs,dispatchWaitMs,mergeMs,totalMs,workerParseSumMs,workerQuerySumMs,workerRenderSumMs,workerExtractSumMs,maxWorkerMs,geometryMatch\n";
    for(int count : {200, 400, 800}) for(int width : {1920, 800, 1920}) {
        for(int iteration = 0; iteration < 12; ++iteration) {
            std::vector<int> modes{0, 1, 2, 4};
            std::rotate(modes.begin(), modes.begin() + iteration % 4, modes.end());
            batch_result reference;
            std::vector<batch_result> candidates;
            for(int concurrency : modes) {
                auto begin = clock_type::now();
                // Serialize the immutable boundary inside the measured envelope.
                auto rows = make_rows(count, iteration);
                std::vector<std::vector<row_input>> batches;
                if(concurrency) for(int i = 0; i < concurrency; ++i) {
                    batches.emplace_back(rows.begin() + count*i/concurrency, rows.begin() + count*(i+1)/concurrency);
                }
                auto snapshot = clock_type::now();
                std::vector<batch_result> results;
                if(concurrency == 0) results.push_back(owner.run(rows, width));
                else {
                    std::vector<std::future<batch_result>> futures;
                    for(int i = 0; i < concurrency; ++i) futures.push_back(workers[i]->run(std::move(batches[i]), width));
                    for(auto& future : futures) results.push_back(future.get());
                }
                auto waited = clock_type::now();
                batch_result merged;
                double maximum = 0;
                for(auto& result : results) {
                    maximum = std::max(maximum, result.parse + result.query + result.render + result.extract);
                    merged.parse += result.parse; merged.query += result.query;
                    merged.render += result.render; merged.extract += result.extract;
                    for(auto& row : result.rows) {
                        for(auto& box : row.boxes) box.y += merged.height;
                        merged.rows.push_back(std::move(row));
                    }
                    merged.height += result.height;
                }
                auto end = clock_type::now();
                if(concurrency == 0) reference = merged;
                else candidates.push_back(merged);
                if(iteration >= 2) output << count << ',' << width << ',' << iteration << ',' << concurrency << ','
                    << milliseconds(begin, snapshot) << ',' << milliseconds(snapshot, waited) << ','
                    << milliseconds(waited, end) << ',' << milliseconds(begin, end) << ','
                    << merged.parse << ',' << merged.query << ',' << merged.render << ',' << merged.extract << ',' << maximum << ",1\n";
            }
            // Validation is outside timing and failures make the run non-deliverable.
            for(const auto& candidate : candidates) compare(reference, candidate);
        }
        std::cerr << "validated rows=" << count << " width=" << width << '\n';
    }
    std::cerr << "PASS: ordered row/image/span boxes, text, IDs, list height; 108 paired states.\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n'; return 1;
}
