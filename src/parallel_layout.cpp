#include "parallel_layout.h"
#include "document.h"
#include "render_flex.h"
#include "render_image.h"
#include "render_inline.h"
#include "render_inline_context.h"
#include "render_block_context.h"
#include "layout_diagnostics.h"
#include "el_image.h"
#include "el_text.h"
#include "el_space.h"
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

namespace litehtml {
class layout_worker_pool {
    std::mutex mutex;
    std::condition_variable wake;
    std::queue<std::function<void()>> queue;
    std::vector<std::thread> threads;
    bool stop = false;
public:
    ~layout_worker_pool() {
        { std::lock_guard<std::mutex> lock(mutex); stop = true; }
        wake.notify_all();
        for(auto& thread : threads) thread.join();
    }
    void reserve(size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        while(threads.size() < count) threads.emplace_back([this] {
            for(;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock, [&] { return stop || !queue.empty(); });
                    if(stop && queue.empty()) return;
                    task = std::move(queue.front()); queue.pop();
                }
                task();
            }
        });
    }
    template<class F> std::future<void> submit(F fn) {
        auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
        auto future = task->get_future();
        { std::lock_guard<std::mutex> lock(mutex); queue.emplace([task] { (*task)(); }); }
        wake.notify_one(); return future;
    }
};
namespace {
thread_local bool in_layout_worker = false;

std::shared_ptr<layout_worker_pool> acquire_pool() {
    static std::mutex mutex;
    static std::weak_ptr<layout_worker_pool> shared;
    std::lock_guard<std::mutex> lock(mutex);
    auto pool = shared.lock();
    if(!pool) { pool = std::make_shared<layout_worker_pool>(); shared = pool; }
    return pool;
}

// Frozen copies of the values consumed by supported render algorithms. Never
// points back to the live document or its host callbacks. Image/text sizes are
// captured by the owner before dispatch. Image line-height writes stay private.
class snapshot_element : public element {
    size content;
    bool replaced, text, whitespace, space, linebreak;
public:
    snapshot_element(const std::shared_ptr<element>& source, const document::ptr& doc) : element(doc),
        replaced(source->is_replaced()), text(source->is_text()), whitespace(source->is_white_space()),
        space(source->is_space()), linebreak(source->is_break()) {
        m_css = source->css();
        if(replaced || text) source->get_content_size(content, 0_px);
    }
    void get_content_size(size& value, pixel_t) override { value = content; }
    bool is_replaced() const override { return replaced; }
    bool is_text() const override { return text; }
    bool is_white_space() const override { return whitespace; }
    bool is_space() const override { return space; }
    bool is_break() const override { return linebreak; }
};

bool resolved(const css_length& value) {
    return value.is_predefined() || value.units() == css_units_px || value.units() == css_units_percentage || value.units() == css_units_none;
}
bool safe_style(const css_properties& css) {
    if(css.get_position() != element_position_static || css.get_float() != float_none || css.get_clear() != clear_none ||
       css.get_overflow() != overflow_visible || css.get_display() == display_none ||
       css.get_webkit_line_clamp() != 0 || css.get_display() == display_list_item ||
       css.get_list_style_position() == list_style_position_inside) return false;
    for(const auto* length : {&css.get_width(), &css.get_height(), &css.get_min_width(), &css.get_max_width(),
        &css.get_min_height(), &css.get_max_height(), &css.get_flex_basis(), &css.get_text_indent(),
        &css.get_row_gap(), &css.get_column_gap(), &css.get_margins().left, &css.get_margins().right,
        &css.get_margins().top, &css.get_margins().bottom, &css.get_padding().left, &css.get_padding().right,
        &css.get_padding().top, &css.get_padding().bottom, &css.get_borders().left.width,
        &css.get_borders().right.width, &css.get_borders().top.width, &css.get_borders().bottom.width}) {
        if(!resolved(*length)) return false;
    }
    return true;
}
}

class parallel_layout_snapshot {
    struct binding { std::shared_ptr<render_item> before, after; };
    std::vector<binding> bindings; // owner only: never inspected by a task
    std::unordered_map<const element*, std::shared_ptr<element>> elements;
    document::ptr doc = std::make_shared<document>(nullptr);
    std::shared_ptr<element> dom_boundary;
    std::shared_ptr<render_item> render_boundary;
    std::shared_ptr<element> copy_element(const std::shared_ptr<element>& source) {
        auto it = elements.find(source.get());
        if(it != elements.end()) return it->second;
        auto result = std::make_shared<snapshot_element>(source, doc);
        elements.emplace(source.get(), result);
        result->parent(dom_boundary);
        return result;
    }
    std::shared_ptr<render_item> copy(const std::shared_ptr<render_item>& source,
                                      const std::shared_ptr<render_item>& parent) {
        if(!safe_style(source->css())) return nullptr;
        // Only these built-in leaves have width-independent content-size inputs.
        if(source->src_el()->is_replaced() && typeid(*source->src_el()) != typeid(el_image)) return nullptr;
        if(source->src_el()->is_text() && typeid(*source->src_el()) != typeid(el_text) &&
           typeid(*source->src_el()) != typeid(el_space)) return nullptr;
        auto el = copy_element(source->src_el());
        std::shared_ptr<render_item> result;
        const auto& type = typeid(*source);
        if(type == typeid(render_item_flex) && source->css().get_display() == display_flex)
            result = std::make_shared<render_item_flex>(el);
        else if(type == typeid(render_text)) result = std::make_shared<render_text>(el);
        else if(type == typeid(render_item_inline)) result = std::make_shared<render_item_inline>(el);
        else if(type == typeid(render_item_inline_context)) result = std::make_shared<render_item_inline_context>(el);
        else if(type == typeid(render_item_block_context)) result = std::make_shared<render_item_block_context>(el);
        else if(type == typeid(render_item_image)) result = std::make_shared<render_item_image>(el);
        else return nullptr;
        result->parent(parent);
        result->m_margins = source->m_margins;
        result->m_padding = source->m_padding;
        result->m_borders = source->m_borders;
        for(const auto& child : source->children()) {
            auto cloned = copy(child, result);
            if(!cloned) return nullptr;
            result->children().push_back(cloned);
        }
        bindings.push_back({source, result});
        return result;
    }
public:
    std::shared_ptr<render_item> root;
    rendered_width width;
    explicit parallel_layout_snapshot(const std::shared_ptr<render_item>& source) {
        const auto parent = source->parent();
        if(!parent) return;
        dom_boundary = std::make_shared<element>(doc);
        dom_boundary->css_w() = parent->css();
        render_boundary = std::make_shared<render_item>(dom_boundary);
        root = copy(source, render_boundary);
        if(!root) return;
        // Preserve original DOM ancestry, which can differ from anonymous render
        // wrappers. External ancestors collapse to an inert non-root boundary.
        for(const auto& item : bindings) {
            const auto ancestor = item.before->src_el()->parent();
            auto it = elements.find(ancestor.get());
            item.after->src_el()->parent(it == elements.end() ? dom_boundary : it->second);
        }
    }
    void commit(const std::shared_ptr<render_item>& parent, pixel_t top) {
        for(auto& item : bindings) {
            auto original = item.before->src_el();
            if(original->is_replaced()) original->css_w().line_height_w() = item.after->css().line_height();
            item.after->m_element = original;
            original->replace_render(item.before, item.after);
        }
        root->parent(parent);
        root->pos().y += top;
    }
};

bool render_parallel_rows(const std::shared_ptr<render_item>& parent, const containing_block_context& size,
                          formatting_context* context, rendered_width& width, pixel_t& height) {
    // Avoid even reading the environment on ordinary small formatting contexts.
    const size_t count = parent->children().size();
    if(in_layout_worker || count < 32 || !context || context->has_floats()) return false;
    const char* setting = std::getenv("LITEHTML_LAYOUT_WORKERS");
    const int requested = setting ? std::atoi(setting) : 0;
    if(requested <= 0) return false;
    if(!safe_style(parent->css())) return false;
    const size_t workers = std::min({static_cast<size_t>(requested),
        static_cast<size_t>(std::max(1u, std::thread::hardware_concurrency())), std::max(size_t(1), count / 4)});
    for(const auto& row : parent->children()) {
        const auto& css = row->css();
        if(css.get_display() != display_flex || css.get_flex_direction() != flex_direction_row ||
           css.get_position() != element_position_static || css.get_float() != float_none ||
           css.get_clear() != clear_none || css.get_overflow() != overflow_visible) return false;
        for(const auto* margin : {&css.get_margins().left, &css.get_margins().right,
                                 &css.get_margins().top, &css.get_margins().bottom}) {
            if(margin->is_predefined() || margin->val() != 0) return false;
        }
    }
    std::vector<std::unique_ptr<parallel_layout_snapshot>> snapshots;
    {
    layout_diagnostics::scope snapshot_time(layout_diagnostics::parallel_snapshot);
    for(const auto& row : parent->children()) {
        auto snapshot = std::make_unique<parallel_layout_snapshot>(row);
        if(!snapshot->root) {
            if(auto* profile = layout_diagnostics::active) ++profile->parallel_fallbacks;
            return false;
        }
        snapshots.push_back(std::move(snapshot));
    }
    }
    {
    layout_diagnostics::scope wait_time(layout_diagnostics::parallel_wait);
    auto& pool = parent->src_el()->get_document()->parallel_pool();
    if(!pool) pool = acquire_pool();
    pool->reserve(workers);
    std::vector<std::future<void>> futures;
    struct drain { std::vector<std::future<void>>& futures; ~drain() { for(auto& future : futures) if(future.valid()) future.wait(); } } cleanup{futures};
    for(size_t worker = 0; worker < workers; ++worker) {
        // The closure contains private render objects and output slots only.
        std::vector<std::pair<std::shared_ptr<render_item>, rendered_width*>> jobs;
        for(size_t i = count * worker / workers; i < count * (worker + 1) / workers; ++i)
            jobs.push_back({snapshots[i]->root, &snapshots[i]->width});
        futures.push_back(pool->submit([jobs = std::move(jobs), size = size.new_width(size.render_width)] {
            struct guard { guard() { in_layout_worker = true; } ~guard() { in_layout_worker = false; } } active;
            for(const auto& job : jobs) *job.second = job.first->render(0_px, 0_px, size, nullptr);
        }));
    }
    // Drain every task even if one throws: snapshots must outlive all workers.
    bool failed = false;
    for(auto& future : futures) { try { future.get(); } catch(...) { failed = true; } }
    if(failed) return false;
    }
    layout_diagnostics::scope commit_time(layout_diagnostics::parallel_commit);
    height = 0_px;
    width = {};
    auto target = parent->children().begin();
    for(auto& snapshot : snapshots) {
        snapshot->commit(parent, height);
        snapshot->root->calc_auto_margins(size.width);
        height += snapshot->root->height();
        width.merge(snapshot->width);
        *target++ = snapshot->root;
    }
    if(auto* profile = layout_diagnostics::active) {
        profile->parallel_rows += count;
        profile->parallel_workers = std::max(profile->parallel_workers, static_cast<uint64_t>(workers));
    }
    return true;
}
}
