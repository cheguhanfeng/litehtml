#include "litehtml/litehtml_layout.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    struct callback_state
    {
        std::vector<std::string> base_urls;
        int                      anchor_clicks    = 0;
        int                      anchor_clicks_ex = 0;
        std::string              anchor_target;
    };

    void set_base_url(const char* url, void* user)
    {
        static_cast<callback_state*>(user)->base_urls.emplace_back(url ? url : "");
    }

    void on_anchor_click(const char*, void* user)
    {
        ++static_cast<callback_state*>(user)->anchor_clicks;
    }

    void on_anchor_click_ex(const char*, const char* target, void* user)
    {
        auto* state = static_cast<callback_state*>(user);
        ++state->anchor_clicks_ex;
        state->anchor_target = target ? target : "";
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

    bool test_node_ids_survive_allocator_address_reuse()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(
            litehtml_layout_load_html(service, "<!doctype html><html><body></body></html>", nullptr, 800, 600) != 0,
            "node-id document failed to load");
        std::unordered_set<uint64_t> ids;
        for(int index = 0; index < 64; ++index)
        {
            auto*          element = litehtml_layout_create_element(service, "div");
            const uint64_t id      = litehtml_layout_element_get_node_id(element);
            passed                 = expect(element != nullptr, "detached element creation failed") &&
                     expect(id != 0, "detached element did not receive a node id") &&
                     expect(ids.insert(id).second, "a released element's node id was reused") && passed;
            litehtml_layout_element_destroy(element);
        }
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_render_lifecycle_and_render_type_validation()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed =
            expect(litehtml_layout_load_html(service, "<!doctype html><html><body><div id='box'></div></body></html>",
                                             nullptr, 800, 600) != 0,
                   "render lifecycle document failed to load");
        auto*         box = litehtml_layout_get_element_by_id(service, "box");
        litehtml_rect placement{};
        passed = expect(litehtml_layout_element_get_placement(box, &placement) == 0,
                        "placement was reported before the first render") &&
                 expect(litehtml_layout_render(service, 0, -1) == 0, "negative render type was accepted") &&
                 expect(litehtml_layout_render(service, 0, 3) == 0, "out-of-range render type was accepted") &&
                 expect(litehtml_layout_render(service, 0, litehtml_render_fixed_only) == 0,
                        "fixed-only render was accepted before initial layout") &&
                 expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0, "initial full render failed") &&
                 expect(litehtml_layout_element_get_placement(box, &placement) != 0,
                        "placement was unavailable after render") &&
                 expect(litehtml_layout_render(service, 0, litehtml_render_fixed_only) != 0,
                        "fixed-only render failed after initial layout") &&
                 passed;
        litehtml_layout_element_destroy(box);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_viewport_can_change_without_reloading_dom()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed =
            expect(litehtml_layout_load_html(
                       service,
                       "<!doctype html><html><body><div id='box' style='width:100vw;height:100vh'></div></body></html>",
                       nullptr, 800, 600) != 0,
                   "resizable viewport document failed to load") &&
            expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                   "initial resizable viewport render failed");
        auto*         box = litehtml_layout_get_element_by_id(service, "box");
        litehtml_rect before{};
        litehtml_rect after{};
        passed =
            expect(litehtml_layout_element_get_placement(box, &before) != 0,
                   "initial viewport placement was unavailable") &&
            expect(litehtml_layout_set_viewport(service, 400, 300) != 0, "viewport update failed") &&
            expect(litehtml_layout_element_get_placement(box, &after) == 0,
                   "old placement remained valid after viewport update") &&
            expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0, "resized viewport render failed") &&
            expect(litehtml_layout_element_get_placement(box, &after) != 0,
                   "resized viewport placement was unavailable") &&
            expect(after.width < before.width && after.height < before.height,
                   "vw/vh geometry did not follow the resized viewport") &&
            expect(litehtml_layout_set_viewport(service, 0, 300) == 0, "invalid zero-width viewport was accepted") &&
            passed;
        litehtml_layout_element_destroy(box);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool activate_test_link(litehtml_layout_callbacks callbacks, callback_state& state)
    {
        callbacks.user = &state;
        auto* service  = litehtml_layout_create(&callbacks);
        bool  passed =
            expect(litehtml_layout_load_html(service,
                                             "<!doctype html><html><body><a href='next' target='_blank' "
                                             "style='display:block;width:100px;height:100px'></a></body></html>",
                                             nullptr, 800, 600) != 0,
                   "anchor callback document failed to load") &&
            expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                   "anchor callback document failed to render");
        litehtml_layout_on_mouse_move(service, 20, 20);
        litehtml_layout_on_mouse_down(service, 20, 20);
        litehtml_layout_on_mouse_up(service, 20, 20);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_extended_anchor_callback_supersedes_legacy_callback()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.on_anchor_click    = on_anchor_click;
        callbacks.on_anchor_click_ex = on_anchor_click_ex;
        bool passed                  = activate_test_link(callbacks, state);
        passed = expect(state.anchor_clicks == 0, "legacy anchor callback duplicated the extended notification") &&
                 expect(state.anchor_clicks_ex == 1, "extended anchor callback was not emitted exactly once") &&
                 expect(state.anchor_target == "_blank", "extended anchor callback lost the target") && passed;

        callback_state legacy_state;
        callbacks.on_anchor_click_ex = nullptr;
        passed                       = activate_test_link(callbacks, legacy_state) &&
                 expect(legacy_state.anchor_clicks == 1, "legacy anchor callback fallback was not emitted") && passed;
        return passed;
    }
} // namespace

int main()
{
    return test_initial_base_url() && test_html_base_overrides_initial_url() &&
                   test_viewport_fallback_drives_vh_layout() && test_render_clamps_scroll_after_content_shrinks() &&
                   test_stale_handles_are_rejected_after_reload() && test_get_element_by_id_uses_exact_html_id() &&
                   test_node_ids_survive_allocator_address_reuse() &&
                   test_render_lifecycle_and_render_type_validation() &&
                   test_viewport_can_change_without_reloading_dom() &&
                   test_extended_anchor_callback_supersedes_legacy_callback()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
