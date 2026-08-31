#include "litehtml/litehtml_layout.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct callback_state
    {
        std::vector<std::string> base_urls;
    };

    void set_base_url(const char* url, void* user)
    {
        static_cast<callback_state*>(user)->base_urls.emplace_back(url ? url : "");
    }

    bool expect(bool condition, const char* message)
    {
        if(condition)
        {
            return true;
        }
        std::cerr << message << '\n';
        return false;
    }

    bool test_initial_base_url()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user         = &state;
        callbacks.set_base_url = set_base_url;

        auto*      service = litehtml_layout_create(&callbacks);
        const bool loaded  = litehtml_layout_load_html(service, "<!doctype html><html><body></body></html>",
                                                       "https://example.test/assets/page.html", 800, 600) != 0;
        const bool passed =
            expect(loaded, "document failed to load") &&
            expect(state.base_urls.size() == 1, "initial base URL callback was not emitted exactly once") &&
            expect(state.base_urls.front() == "https://example.test/assets/page.html",
                   "initial base URL callback received the wrong value");
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_html_base_overrides_initial_url()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user         = &state;
        callbacks.set_base_url = set_base_url;

        auto*      service = litehtml_layout_create(&callbacks);
        const bool loaded =
            litehtml_layout_load_html(
                service,
                "<!doctype html><html><head><base href='https://cdn.example.test/'></head><body></body></html>",
                "https://example.test/page.html", 800, 600) != 0;
        const bool passed =
            expect(loaded, "document with <base> failed to load") &&
            expect(state.base_urls.size() == 2, "expected initial and HTML base URL callbacks") &&
            expect(state.base_urls[0] == "https://example.test/page.html", "initial base URL was not first") &&
            expect(state.base_urls[1] == "https://cdn.example.test/", "HTML <base> did not override the initial URL");
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_viewport_fallback_drives_vh_layout()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(service, "<!doctype html><html><body></body></html>", nullptr,
                                                         800, 600) != 0,
                               "viewport fallback document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "viewport fallback document failed to render");
        litehtml_size size{};
        litehtml_layout_get_content_size(service, &size);
        passed = expect(size.width > 0, "fallback viewport produced zero document width") &&
                 expect(size.height >= 600, "100vh canvas did not use the fallback viewport height") && passed;
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_render_clamps_scroll_after_content_shrinks()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed =
            expect(litehtml_layout_load_html(
                       service, "<!doctype html><html><body id='body'><div style='height:2000px'></div></body></html>",
                       nullptr, 800, 600) != 0,
                   "scroll clamp document failed to load") &&
            expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                   "scroll clamp document failed to render");
        litehtml_layout_set_scroll(service, 0, 1000);
        passed = expect(litehtml_layout_get_scroll_y(service) > 0, "test document was not scrollable") && passed;

        auto* body = litehtml_layout_get_element_by_id(service, "body");
        passed     = expect(body != nullptr, "body handle was not found") &&
                 expect(litehtml_layout_element_set_inner_html(body, "<div style='height:100px'></div>") != 0,
                        "body content replacement failed") &&
                 expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                        "shortened document failed to render") &&
                 passed;
        litehtml_size size{};
        litehtml_layout_get_content_size(service, &size);
        const float maximum_scroll = std::max(0.f, size.height - 600.f);
        passed                     = expect(std::fabs(litehtml_layout_get_scroll_y(service) - maximum_scroll) < 0.01f,
                                            "render did not clamp scroll after content shrank") &&
                 passed;
        litehtml_layout_element_destroy(body);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_stale_handles_are_rejected_after_reload()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service, "<!doctype html><html><body><div id='old'><span></span></div></body></html>",
                                 nullptr, 800, 600) != 0,
                               "first handle-generation document failed to load");
        auto* stale   = litehtml_layout_get_element_by_id(service, "old");
        passed        = expect(stale != nullptr, "old element handle was not found") && passed;
        passed =
            expect(litehtml_layout_load_html(service, "<!doctype html><html><body><div id='new'></div></body></html>",
                                             nullptr, 800, 600) != 0,
                   "second handle-generation document failed to load") &&
            passed;

        litehtml_rect placement{};
        passed =
            expect(litehtml_layout_element_get_attribute(stale, "id") == nullptr,
                   "stale handle still exposed attributes") &&
            expect(litehtml_layout_element_get_text(stale) == nullptr, "stale handle still exposed text") &&
            expect(litehtml_layout_element_get_node_id(stale) == 0, "stale handle still received a node id") &&
            expect(litehtml_layout_element_get_tag_name(stale) == nullptr, "stale handle still exposed its tag") &&
            expect(litehtml_layout_element_get_parent(stale) == nullptr, "stale handle still exposed its parent") &&
            expect(litehtml_layout_element_get_placement(stale, &placement) == 0,
                   "stale handle still exposed placement") &&
            expect(litehtml_layout_element_get_child_count(stale) == 0, "stale handle still exposed children") &&
            expect(litehtml_layout_element_get_child(stale, 0) == nullptr, "stale handle still returned a child") &&
            passed;
        litehtml_layout_element_destroy(stale);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_get_element_by_id_uses_exact_html_id()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed =
            expect(litehtml_layout_load_html(
                       service, "<!doctype html><html><body><div id='panel:item.one two'></div></body></html>", nullptr,
                       800, 600) != 0,
                   "special-id document failed to load");
        auto* exact      = litehtml_layout_get_element_by_id(service, "panel:item.one two");
        auto* wrong_case = litehtml_layout_get_element_by_id(service, "Panel:item.one two");
        passed           = expect(exact != nullptr, "legal HTML id containing CSS punctuation was not found") &&
                 expect(wrong_case == nullptr, "HTML id lookup was not case-sensitive") && passed;
        litehtml_layout_element_destroy(exact);
        litehtml_layout_element_destroy(wrong_case);
        litehtml_layout_destroy(service);
        return passed;
    }
} // namespace

int main()
{
    return test_initial_base_url() && test_html_base_overrides_initial_url() &&
                   test_viewport_fallback_drives_vh_layout() && test_render_clamps_scroll_after_content_shrinks() &&
                   test_stale_handles_are_rejected_after_reload() && test_get_element_by_id_uses_exact_html_id()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
