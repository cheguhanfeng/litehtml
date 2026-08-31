#include "litehtml/litehtml_layout.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <limits>
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
        int                      mouse_events     = 0;
        int                      css_imports      = 0;
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

    void on_mouse_event(int, void* user)
    {
        ++static_cast<callback_state*>(user)->mouse_events;
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

    bool test_fixed_hit_testing_uses_unscrolled_client_coordinates()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service,
                                 "<!doctype html><html><body style='height:2000px'><div id='fixed' "
                                   "style='position:fixed;left:0;top:0;width:100px;height:100px'></div></body></html>",
                                 nullptr, 800, 600) != 0,
                               "fixed hit-test document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "fixed hit-test document failed to render");
        litehtml_layout_set_scroll(service, 0, 500);
        auto* hit = litehtml_layout_hit_test(service, 20, 20);
        passed    = expect(hit != nullptr, "fixed element was not hit after page scroll") &&
                 expect(hit && litehtml_layout_element_get_attribute(hit, "id") &&
                            std::string(litehtml_layout_element_get_attribute(hit, "id")) == "fixed",
                        "page scroll offset was incorrectly applied to fixed hit testing") &&
                 passed;
        litehtml_layout_element_destroy(hit);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_mouse_up_coordinates_control_click_activation()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user               = &state;
        callbacks.on_anchor_click_ex = on_anchor_click_ex;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed =
            expect(litehtml_layout_load_html(service,
                                             "<!doctype html><html><body><a href='next' "
                                             "style='display:block;width:100px;height:100px'></a></body></html>",
                                             nullptr, 800, 600) != 0,
                   "release-coordinate document failed to load") &&
            expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                   "release-coordinate document failed to render");
        litehtml_layout_on_mouse_move(service, 20, 20);
        litehtml_layout_on_mouse_down(service, 20, 20);
        litehtml_layout_on_mouse_up(service, 300, 300);
        passed =
            expect(state.anchor_clicks_ex == 0, "releasing outside the pressed anchor still activated navigation") &&
            passed;

        litehtml_layout_on_mouse_move(service, 20, 20);
        litehtml_layout_on_mouse_down(service, 20, 20);
        litehtml_layout_on_mouse_up(service, 20, 20);
        passed = expect(state.anchor_clicks_ex == 1, "releasing over the pressed anchor did not activate navigation") &&
                 passed;
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_mouse_leave_clears_hover_state()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user           = &state;
        callbacks.on_mouse_event = on_mouse_event;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service,
                                 "<!doctype html><html><head><style>#box:hover{color:red}</style></head>"
                                   "<body><div id='box' style='width:100px;height:100px'></div></body></html>",
                                 nullptr, 800, 600) != 0,
                               "mouse-leave document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "mouse-leave document failed to render");
        litehtml_layout_on_mouse_move(service, 20, 20);
        const int events_after_enter = state.mouse_events;
        litehtml_layout_on_mouse_leave(service);
        passed = expect(events_after_enter > 0, "mouse enter state was not established") &&
                 expect(state.mouse_events > events_after_enter, "mouse leave did not clear hover state") && passed;
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_scroll_input_prefers_nested_overflow_then_page()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(service,
                                                         "<!doctype html><html><body style='height:2000px'>"
                                                           "<div style='width:100px;height:100px;overflow:auto'>"
                                                           "<div style='height:1000px'></div></div></body></html>",
                                                         nullptr, 800, 600) != 0,
                               "nested-scroll document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "nested-scroll document failed to render");
        passed =
            expect(litehtml_layout_on_scroll(service, 0, 100, 20, 20) != 0,
                   "nested overflow did not consume scroll input") &&
            expect(litehtml_layout_get_scroll_y(service) == 0, "nested overflow input incorrectly scrolled the page") &&
            expect(litehtml_layout_on_scroll(service, 0, 1000, 20, 20) != 0,
                   "partial nested scroll was not consumed") &&
            expect(litehtml_layout_get_scroll_y(service) > 0,
                   "nested overflow did not chain its unconsumed delta to the page") &&
            passed;
        litehtml_layout_set_scroll(service, 0, 0);
        passed =
            expect(litehtml_layout_on_scroll(service, 0, 100, 300, 300) != 0,
                   "unconsumed scroll input did not fall back to the page") &&
            expect(litehtml_layout_get_scroll_y(service) > 0, "page scroll offset did not change after fallback") &&
            passed;
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_script_inner_html_replaces_source_text()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed =
            expect(litehtml_layout_load_html(service, "<!doctype html><html><body><script>one</script></body></html>",
                                             nullptr, 800, 600) != 0,
                   "script replacement document failed to load") &&
            expect(litehtml_layout_get_script_count(service) == 1, "inline script was not collected") &&
            expect(std::string(litehtml_layout_get_script(service, 0)) == "one",
                   "initial inline script source was wrong");
        auto* script = litehtml_layout_get_element_by_tag(service, "script", 0);
        passed       = expect(script != nullptr, "script element handle was not found") &&
                 expect(litehtml_layout_element_set_inner_html(script, "two") != 0,
                        "script inner HTML replacement failed") &&
                 expect(std::string(litehtml_layout_get_script(service, 0)) == "two",
                        "script replacement appended to old source text") &&
                 passed;
        litehtml_layout_element_destroy(script);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_script_src_uses_html_attribute_semantics()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service, "<!doctype html><html><body><script src='one.js'></script></body></html>",
                                 nullptr, 800, 600) != 0,
                               "script-src document failed to load");
        auto* script  = litehtml_layout_get_element_by_tag(service, "script", 0);
        passed =
            expect(script != nullptr, "script-src element was not found") &&
            expect(std::string(litehtml_layout_element_get_attribute(script, "SRC")) == "one.js",
                   "script src was not readable through the DOM attribute API") &&
            expect(litehtml_layout_element_set_attribute(script, "SRC", "two.js") != 0,
                   "uppercase script SRC mutation failed") &&
            expect(std::string(litehtml_layout_get_script_src(service, 0)) == "two.js",
                   "uppercase script SRC mutation did not update script source") &&
            expect(std::string(litehtml_layout_element_get_attribute(script, "src")) == "two.js",
                   "mutated script src was not readable") &&
            expect(litehtml_layout_element_remove_attribute(script, "SRC") != 0, "script SRC removal failed") &&
            expect(litehtml_layout_get_script_src(service, 0) == nullptr, "removed script src remained active") &&
            expect(litehtml_layout_element_get_attribute(script, "src") == nullptr,
                   "removed script src remained readable") &&
            expect(litehtml_layout_element_set_attribute(script, "src", "") != 0, "empty script src mutation failed") &&
            expect(litehtml_layout_element_get_attribute(script, "src") != nullptr,
                   "present empty script src was treated as absent") &&
            expect(std::string(litehtml_layout_element_get_attribute(script, "src")).empty(),
                   "present empty script src did not retain its value") &&
            expect(litehtml_layout_element_remove_attribute(script, "src") != 0,
                   "present empty script src could not be removed") &&
            passed;
        litehtml_layout_element_destroy(script);
        litehtml_layout_destroy(service);
        return passed;
    }

    void import_css(char* text, const char* url, char* baseurl, void* user)
    {
        auto* state = static_cast<callback_state*>(user);
        ++state->css_imports;
        const std::string css = url && std::string(url) == "two.css" ? "#box{height:120px}" : "#box{height:20px}";
        std::copy(css.begin(), css.end(), text);
        text[css.size()] = '\0';
        if(baseurl) baseurl[0] = '\0';
    }

    bool test_programmatic_html_tag_names_are_case_insensitive()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service, "<!doctype html><html><body id='body'></body></html>", nullptr, 800, 600) != 0,
                               "programmatic-tag document failed to load");
        auto* body   = litehtml_layout_get_element_by_id(service, "body");
        auto* script = litehtml_layout_create_element(service, "SCRIPT");
        passed       = expect(body != nullptr && script != nullptr, "uppercase script element could not be created") &&
                 expect(std::string(litehtml_layout_element_get_tag_name(script)) == "script",
                        "programmatic HTML tag name was not normalized") &&
                 expect(litehtml_layout_element_set_attribute(script, "src", "dynamic.js") != 0,
                        "programmatic script src could not be set") &&
                 expect(litehtml_layout_element_set_inner_html(script, "dynamic()") != 0,
                        "programmatic script text could not be set") &&
                 expect(litehtml_layout_element_append_child(body, script) != 0,
                        "programmatic script could not be attached") &&
                 expect(litehtml_layout_get_script_count(service) == 1,
                        "uppercase programmatic script lost special element semantics") &&
                 expect(std::string(litehtml_layout_get_script(service, 0)) == "dynamic()",
                        "programmatic script text was not collected") &&
                 expect(std::string(litehtml_layout_get_script_src(service, 0)) == "dynamic.js",
                        "programmatic script src was not collected") &&
                 passed;
        litehtml_layout_element_destroy(script);
        litehtml_layout_element_destroy(body);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_scroll_to_aligns_element_top_exactly()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service,
                                 "<!doctype html><html><body style='margin:0'>"
                                 "<div style='height:400px'></div><div id='target' style='height:50px'></div>"
                                 "<div style='height:1000px'></div></body></html>",
                                 nullptr, 800, 300) != 0,
                               "scroll-to document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "scroll-to document failed to render");
        auto* target = litehtml_layout_get_element_by_id(service, "target");
        litehtml_rect placement{};
        passed = expect(target != nullptr, "scroll-to target was not found") &&
                 expect(litehtml_layout_element_get_placement(target, &placement) != 0,
                        "scroll-to target placement was unavailable") &&
                 passed;
        litehtml_layout_scroll_to(service, target);
        passed = expect(std::fabs(litehtml_layout_get_scroll_y(service) - placement.y) < 0.01f,
                        "scroll_to did not align the target top with the viewport top") &&
                 passed;
        litehtml_layout_element_destroy(target);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_style_media_attribute_controls_author_rules()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service,
                                 "<!doctype html><html><head><style>#box{height:20px}</style>"
                                 "<style media='(min-width: 1000px)'>#box{height:200px}</style></head>"
                                 "<body><div id='box'></div></body></html>",
                                 nullptr, 800, 600) != 0,
                               "style-media document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "style-media document failed to render");
        auto* box = litehtml_layout_get_element_by_id(service, "box");
        litehtml_rect placement{};
        passed = expect(box != nullptr && litehtml_layout_element_get_placement(box, &placement) != 0,
                        "style-media box placement was unavailable") &&
                 expect(std::fabs(placement.height - 20.f) < 0.01f,
                        "inactive style media attribute was ignored") &&
                 passed;
        passed = expect(litehtml_layout_set_viewport(service, 1200, 600) != 0,
                        "style-media viewport update failed") &&
                 expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                        "style-media document failed to re-render") &&
                 expect(litehtml_layout_element_get_placement(box, &placement) != 0,
                        "style-media box placement was unavailable after resize") &&
                 expect(std::fabs(placement.height - 200.f) < 0.01f,
                        "active style media attribute did not enable its rule") &&
                 passed;
        litehtml_layout_element_destroy(box);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_dynamic_style_text_rebuilds_author_stylesheet()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service,
                                 "<!doctype html><html><head><style id='rules'>#box{height:20px}</style></head>"
                                 "<body><div id='box'></div></body></html>",
                                 nullptr, 800, 600) != 0,
                               "dynamic-style document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "dynamic-style document failed to render");
        auto* style = litehtml_layout_get_element_by_id(service, "rules");
        auto* box   = litehtml_layout_get_element_by_id(service, "box");
        litehtml_rect placement{};
        passed = expect(style != nullptr && box != nullptr, "dynamic-style elements were not found") &&
                 expect(litehtml_layout_element_get_placement(box, &placement) != 0,
                        "dynamic-style box placement was unavailable") &&
                 expect(std::fabs(placement.height - 20.f) < 0.01f,
                        "initial dynamic-style rule was not applied") &&
                 expect(litehtml_layout_element_set_inner_html(style, "#box{height:120px}") != 0,
                        "dynamic style text replacement failed") &&
                 expect(std::string(litehtml_layout_element_get_text(style)) == "#box{height:120px}",
                        "dynamic style text replacement retained old content") &&
                 expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                        "dynamic-style document failed to re-render") &&
                 expect(litehtml_layout_element_get_placement(box, &placement) != 0,
                        "dynamic-style box placement was unavailable after mutation") &&
                 expect(std::fabs(placement.height - 120.f) < 0.01f,
                        "dynamic style text did not rebuild the author stylesheet") &&
                 passed;
        litehtml_layout_element_destroy(box);
        litehtml_layout_element_destroy(style);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_dynamic_stylesheet_link_reloads_author_rules()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user       = &state;
        callbacks.import_css = import_css;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service,
                                 "<!doctype html><html><head><link id='rules' rel='StyleSheet' href='one.css'></head>"
                                 "<body><div id='box'></div></body></html>",
                                 nullptr, 800, 600) != 0,
                               "dynamic-link document failed to load") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "dynamic-link document failed to render");
        auto* link = litehtml_layout_get_element_by_id(service, "rules");
        auto* box  = litehtml_layout_get_element_by_id(service, "box");
        litehtml_rect placement{};
        passed = expect(link != nullptr && box != nullptr, "dynamic-link elements were not found") &&
                 expect(litehtml_layout_element_get_placement(box, &placement) != 0,
                        "dynamic-link box placement was unavailable") &&
                 expect(std::fabs(placement.height - 20.f) < 0.01f,
                        "initial linked stylesheet rule was not applied") &&
                 expect(litehtml_layout_element_set_attribute(link, "href", "two.css") != 0,
                        "stylesheet link href mutation failed") &&
                 expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                        "dynamic-link document failed to re-render") &&
                 expect(litehtml_layout_element_get_placement(box, &placement) != 0,
                        "dynamic-link box placement was unavailable after mutation") &&
                 expect(std::fabs(placement.height - 120.f) < 0.01f,
                        "mutated stylesheet link did not replace its author rules") &&
                 expect(state.css_imports == 2, "stylesheet link was not imported exactly once per href") &&
                 passed;
        litehtml_layout_element_destroy(box);
        litehtml_layout_element_destroy(link);
        litehtml_layout_destroy(service);
        return passed;
    }

    bool test_viewport_dimensions_must_be_finite_positive()
    {
        callback_state            state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state;

        auto* service = litehtml_layout_create(&callbacks);
        bool  passed  = expect(litehtml_layout_load_html(
                                 service, "<!doctype html><html><body></body></html>", nullptr, 0, 600) == 0,
                               "load_html accepted a zero-width viewport") &&
                      expect(litehtml_layout_load_html(
                                 service, "<!doctype html><html><body></body></html>", nullptr, 800, 600) != 0,
                             "valid viewport document failed to load") &&
                      expect(litehtml_layout_set_viewport(service, std::numeric_limits<float>::infinity(), 600) == 0,
                             "set_viewport accepted an infinite width") &&
                      expect(litehtml_layout_render(service, 0, litehtml_render_all) != 0,
                             "invalid viewport update corrupted the previous valid document");
        litehtml_layout_destroy(service);
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
                   test_extended_anchor_callback_supersedes_legacy_callback() &&
                   test_fixed_hit_testing_uses_unscrolled_client_coordinates() &&
                   test_mouse_up_coordinates_control_click_activation() && test_mouse_leave_clears_hover_state() &&
                   test_scroll_input_prefers_nested_overflow_then_page() &&
                   test_script_inner_html_replaces_source_text() && test_script_src_uses_html_attribute_semantics() &&
                   test_programmatic_html_tag_names_are_case_insensitive() &&
                   test_scroll_to_aligns_element_top_exactly() && test_style_media_attribute_controls_author_rules() &&
                   test_dynamic_style_text_rebuilds_author_stylesheet() &&
                   test_dynamic_stylesheet_link_reloads_author_rules() &&
                   test_viewport_dimensions_must_be_finite_positive()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
