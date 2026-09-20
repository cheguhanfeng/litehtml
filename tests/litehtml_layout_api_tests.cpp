#include "litehtml/litehtml_layout.h"
#include "litehtml/font_description.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>
#include <thread>
#include <atomic>

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
		std::vector<litehtml_font_description> font_descriptions;
		std::vector<std::string> painted_text;
    };

	void* capture_font(const litehtml_font_description* descr, litehtml_font_metrics* metrics, void* user)
	{
		auto* state = static_cast<callback_state*>(user);
		if(descr) state->font_descriptions.push_back(*descr);
		if(metrics)
		{
			metrics->font_size = descr ? descr->size : 16.f;
			metrics->height = metrics->font_size * 1.2f;
			metrics->ascent = metrics->font_size * .8f;
			metrics->descent = metrics->font_size * .2f;
			metrics->x_height = metrics->font_size * .5f;
			metrics->ch_width = metrics->font_size * .5f;
			metrics->draw_spaces = 1;
		}
		return reinterpret_cast<void*>(1);
	}

	float capture_text_width(const char* text, void*, void*)
	{
		return text ? static_cast<float>(std::strlen(text)) * 8.f : 0.f;
	}

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
        auto* retained = litehtml_layout_create_element(service, "span");
        const uint64_t retained_id = litehtml_layout_element_get_node_id(retained);
        for(int index = 0; index < 4096; ++index)
        {
            auto*          element = litehtml_layout_create_element(service, "div");
            const uint64_t id      = litehtml_layout_element_get_node_id(element);
            passed                 = expect(element != nullptr, "detached element creation failed") &&
                     expect(id != 0, "detached element did not receive a node id") &&
                     expect(ids.insert(id).second, "a released element's node id was reused") && passed;
            litehtml_layout_element_destroy(element);
        }
        const auto entries = litehtml_layout_get_node_identity_entry_count(service);
        std::printf("node identity entries after 4096 released nodes: %llu\n", static_cast<unsigned long long>(entries));
        passed = expect(entries <= 257, "expired node identity entries retain released allocations") && passed;
        passed = expect(litehtml_layout_element_get_node_id(retained) == retained_id,
                        "cleanup changed a still-live detached node identity") && passed;
        litehtml_layout_element_destroy(retained);
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

    bool test_element_attribute_names_are_enumerable()
    {
        litehtml_layout_callbacks callbacks{};
        auto* service = litehtml_layout_create(&callbacks);
        bool passed = expect(litehtml_layout_load_html(
                                 service,
                                 "<!doctype html><html><body><div id='card' class='hot' data-score='7'></div></body></html>",
                                 nullptr, 800, 600) != 0,
                             "attribute enumeration document failed to load");
        auto* card = litehtml_layout_get_element_by_id(service, "card");
        std::unordered_set<std::string> names;
        const int count = litehtml_layout_element_get_attribute_count(card);
        for(int index = 0; index < count; ++index)
        {
            const char* name = litehtml_layout_element_get_attribute_name(card, index);
            if(name) names.emplace(name);
        }
        passed = expect(card != nullptr, "attribute enumeration element was not found") &&
                 expect(count == 3, "attribute enumeration returned the wrong count") &&
                 expect(names == std::unordered_set<std::string>({"id", "class", "data-score"}),
                        "attribute enumeration returned the wrong names") &&
                 expect(litehtml_layout_element_get_attribute_name(card, -1) == nullptr,
                        "attribute enumeration accepted a negative index") &&
                 expect(litehtml_layout_element_get_attribute_name(card, count) == nullptr,
                        "attribute enumeration accepted an out-of-range index") &&
                 passed;
        litehtml_layout_element_destroy(card);
        litehtml_layout_destroy(service);
        return passed;
    }

	bool test_font_callback_carries_text_decoration()
	{
		callback_state state;
		litehtml_layout_callbacks callbacks{};
		callbacks.user = &state;
		callbacks.create_font = capture_font;
		callbacks.text_width = capture_text_width;
		auto* service = litehtml_layout_create(&callbacks);
		bool passed = expect(litehtml_layout_load_html(
			service,
			"<!doctype html><html><body><span style='font:20px Arial;text-decoration-line:underline;text-decoration-style:dashed;text-decoration-thickness:3px;text-decoration-color:#e11d48'>decorated</span></body></html>",
			nullptr, 800, 600) != 0, "text decoration ABI document failed to load") &&
			expect(litehtml_layout_render(service, 800, litehtml_render_all) != 0,
				"text decoration ABI document failed to render");
		const auto found = std::find_if(state.font_descriptions.begin(), state.font_descriptions.end(),
			[](const litehtml_font_description& font)
			{
				return font.decoration_line == 1 && font.decoration_style == 3 &&
					std::fabs(font.decoration_thickness - 3.f) < .01f &&
					font.decoration_color.r == 225 && font.decoration_color.g == 29 &&
					font.decoration_color.b == 72 && font.decoration_color.a == 255;
			});
		passed = expect(found != state.font_descriptions.end(),
			"create_font did not receive text decoration line/style/thickness/color") && passed;
		litehtml_layout_destroy(service);
		return passed;
	}
    bool test_rebuild_does_not_accumulate_render_references()
    {
        callback_state state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user=&state;callbacks.create_font=capture_font;callbacks.text_width=capture_text_width;
        auto* service = litehtml_layout_create(&callbacks);
        litehtml_layout_load_html(service,"<html><body><div id='static'>kept</div><div id='changing'>0</div></body></html>",nullptr,800,600);
        litehtml_layout_render(service,800,litehtml_render_all);
        const uint64_t initial = litehtml_layout_get_render_reference_count(service);
        auto* changed = litehtml_layout_get_element_by_id(service,"changing");
        for(int i=0;i<200;++i)
        {
            litehtml_layout_element_set_inner_html(changed,std::to_string(i).c_str());
            litehtml_layout_render(service,800,litehtml_render_all);
        }
        const uint64_t final_count = litehtml_layout_get_render_reference_count(service);
        std::cout << "render references after 200 rebuilds: " << initial << " -> " << final_count << '\n';
        const bool passed = expect(initial > 0 && final_count <= initial + 4,"unqueried stable DOM must not retain every expired render reference");
        litehtml_layout_element_destroy(changed);litehtml_layout_destroy(service);
        return passed;
    }

    bool test_equal_size_numeric_text_replacement()
    {
        callback_state state;
        litehtml_layout_callbacks callbacks{};
        callbacks.user = &state; callbacks.create_font = capture_font; callbacks.text_width = capture_text_width;
        callbacks.draw_text = [](const char* text, void*, litehtml_color, const litehtml_rect*, void* user) {
            static_cast<callback_state*>(user)->painted_text.emplace_back(text ? text : "");
        };
        auto* service = litehtml_layout_create(&callbacks);
        litehtml_layout_load_html(service,"<html><body><div id='value'>42</div><div id='next'>after</div></body></html>",nullptr,800,600);
        litehtml_layout_render(service,800,litehtml_render_all);
        auto* value = litehtml_layout_get_element_by_id(service,"value");
        auto* next = litehtml_layout_get_element_by_id(service,"next");
        auto* old_child = litehtml_layout_element_get_child(value,0);
        litehtml_rect before{},after{};
        litehtml_layout_element_get_placement(next,&before);
        bool passed = expect(litehtml_layout_element_try_replace_numeric_text(value,"73") == 1,"equal-size numeric replacement must use retained layout");
        auto* new_child = litehtml_layout_element_get_child(value,0);
        passed = expect(std::string(litehtml_layout_element_get_text(value)) == "73","new DOM text missing") && passed;
        passed = expect(std::string(litehtml_layout_element_get_text(old_child)) == "42","replacement must not mutate detached node identity") && passed;
        passed = expect(litehtml_layout_element_get_node_id(old_child) != litehtml_layout_element_get_node_id(new_child),"text replacement must allocate fresh DOM identity") && passed;
        litehtml_layout_draw(service);
        passed = expect(std::find(state.painted_text.begin(),state.painted_text.end(),"73") != state.painted_text.end(),"retained draw must paint new text without layout") && passed;
        passed = expect(std::find(state.painted_text.begin(),state.painted_text.end(),"42") == state.painted_text.end(),"retained draw must not paint detached text") && passed;
        litehtml_layout_element_get_placement(next,&after);
        passed = expect(before.x == after.x && before.y == after.y && before.width == after.width && before.height == after.height,"paint-only update moved adjacent geometry") && passed;
        litehtml_layout_element_set_inner_html(next,"changed sibling");
        litehtml_layout_render(service,800,litehtml_render_all);
        passed = expect(litehtml_layout_element_try_replace_numeric_text(value,"73") == 1,"unrelated rebuild must not leave stale render handles") && passed;
        passed = expect(litehtml_layout_element_try_replace_numeric_text(value,"100") == 0,"changed measured width must fall back") && passed;
        passed = expect(litehtml_layout_element_try_replace_numeric_text(value,"<b>2</b>") == 0,"markup must never take numeric path") && passed;
        passed = expect(litehtml_layout_element_try_replace_numeric_text(value,"") == 0,"empty text changes topology") && passed;
        litehtml_layout_element_set_attribute(value,"style","font-size:30px");
        passed = expect(litehtml_layout_element_try_replace_numeric_text(value,"24") == 0,"pending styles must fall back") && passed;
        passed = expect(std::string(litehtml_layout_element_get_text(value)) == "73","failed fast path must not mutate DOM") && passed;
        litehtml_layout_element_destroy(old_child);litehtml_layout_element_destroy(new_child);
        litehtml_layout_element_destroy(next);litehtml_layout_element_destroy(value);litehtml_layout_destroy(service);
        return passed;
    }

} // namespace

bool test_plain_text_replacement_preserves_selector_matches()
{
    callback_state state;
    litehtml_layout_callbacks callbacks{};
    callbacks.user = &state;
    callbacks.create_font = capture_font;
    callbacks.text_width = capture_text_width;
    auto* service = litehtml_layout_create(&callbacks);
    litehtml_layout_load_html(service,
        "<html><head><style>#value{display:block;width:80px;font-size:16px}#next{display:block}b:nth-child(even){color:red}</style></head>"
        "<body><span id='value'>Old label</span><span id='next'>next</span><div id='other'>Status old</div></body></html>", nullptr,800,600);
    litehtml_layout_render(service,800,litehtml_render_all);
    auto* value=litehtml_layout_get_element_by_id(service,"value");
    auto* next=litehtml_layout_get_element_by_id(service,"next");
    auto* other=litehtml_layout_get_element_by_id(service,"other");
    auto* old=litehtml_layout_element_get_child(value,0);
    litehtml_rect before{},after{};
    litehtml_layout_element_get_placement(next,&before);
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_layout_reset_style_invalidation_stats(service);
#endif
    litehtml_layout_element_set_inner_html(value,"A much longer label now");
    litehtml_layout_element_set_inner_html(other,"Status changed");
    litehtml_layout_render(service,800,litehtml_render_all);
    litehtml_layout_element_get_placement(next,&after);
    bool passed=expect(after.y>before.y,"changed plain text must still reflow adjacent geometry");
    passed=expect(std::string(litehtml_layout_element_get_text(old))=="Old","old text node identity changed") && passed;
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_style_invalidation_stats stats{};
    litehtml_layout_get_style_invalidation_stats(service,&stats);
    std::printf("plain text replacement style-matched elements: %llu\n",static_cast<unsigned long long>(stats.full_match_elements+stats.subtree_match_elements));
    passed=expect(stats.full_match_elements+stats.subtree_match_elements==0,"plain text replacement rematches unrelated element selectors") && passed;
#endif
    // Markup remains a structural mutation, including structural selector effects.
    litehtml_layout_element_set_inner_html(value,"<b>one</b><b id='second'>two</b>");
    litehtml_layout_render(service,800,litehtml_render_all);
    auto* second=litehtml_layout_get_element_by_id(service,"second");
    passed=expect(second!=nullptr,"markup fallback lost new elements") && passed;
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_layout_get_style_invalidation_stats(service,&stats);
    passed=expect(stats.full_match_count+stats.subtree_match_count>0,"markup must retain structural invalidation") && passed;
#endif
    litehtml_layout_element_set_inner_html(value,"Word");
    litehtml_layout_render(service,800,litehtml_render_all);
    litehtml_layout_element_get_placement(value,&before);
    litehtml_layout_element_set_attribute(value,"style","font-size:32px");
    litehtml_layout_element_set_inner_html(value,"Word");
    litehtml_layout_render(service,800,litehtml_render_all);
    litehtml_layout_element_get_placement(value,&after);
    passed=expect(after.height>before.height,"pending inherited font change must refresh new text styles") && passed;
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_layout_reset_style_invalidation_stats(service);
#endif
    litehtml_layout_element_set_inner_html(other,"<b>updated</b>");
    litehtml_layout_element_set_inner_html(value,"Another label");
    litehtml_layout_render(service,800,litehtml_render_all);
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_layout_get_style_invalidation_stats(service,&stats);
    std::printf("unrelated pending subtree style-matched elements: %llu\n",static_cast<unsigned long long>(stats.full_match_elements+stats.subtree_match_elements));
    passed=expect(stats.full_match_elements+stats.subtree_match_elements<=4,
                  "unrelated pending styles must not expand text rematching to the whole document") && passed;
#endif
    litehtml_layout_element_destroy(second);litehtml_layout_element_destroy(old);
    litehtml_layout_element_destroy(other);litehtml_layout_element_destroy(next);litehtml_layout_element_destroy(value);litehtml_layout_destroy(service);
    return passed;
}

bool test_full_render_and_dirty_geometry_validation()
{
    litehtml_layout_callbacks callbacks{};
    auto* service = litehtml_layout_create(&callbacks);
    bool passed = expect(litehtml_layout_load_html(service,
        "<html><body><div id='box' style='position:absolute;width:80px;height:40px'>"
        "<span id='child' style='display:none'>hidden</span></div></body></html>", "", 800, 600) != 0,
        "geometry validation fixture failed to load");
    passed = expect(litehtml_layout_render(service, 800, 0) != 0, "initial geometry render failed") && passed;
    auto* box = litehtml_layout_get_element_by_id(service, "box");
    auto* child = litehtml_layout_get_element_by_id(service, "child");
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_layout_reset_style_invalidation_stats(service);
#endif
    litehtml_layout_element_set_attribute(box, "style", "position:absolute;width:80px;height:40px;color:red");
    litehtml_layout_render_dirty(service, box, 800, 0);
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_style_invalidation_stats stats{};
    litehtml_layout_get_style_invalidation_stats(service, &stats);
    passed = expect(stats.geometry_cache_hit_count == 1 && stats.layout_visited_elements == 0,
                    "paint-only dirty render did not retain geometry") && passed;
#endif

    litehtml_layout_element_set_attribute(box, "style", "position:absolute;width:120px;height:40px;color:red");
    litehtml_layout_render_dirty(service, box, 800, 0);
    litehtml_rect rect{};
    litehtml_layout_element_get_placement(box, &rect);
    passed = expect(std::abs(rect.width - 120.f) < .01f, "dirty render skipped changed width") && passed;
#if defined(LITEHTML_ENABLE_STYLE_DIAGNOSTICS)
    litehtml_layout_get_style_invalidation_stats(service, &stats);
    passed = expect(stats.geometry_cache_miss_count == 1 &&
                    stats.layout_visited_elements == 3,
                    "dirty render changed geometry-miss or node-count telemetry") && passed;
#endif

    // A full render must still reflow dimensions without geometry signatures,
    // and still detect topology changes that introduce previously hidden items.
    litehtml_layout_element_set_attribute(box, "style", "position:absolute;width:160px;height:40px");
    litehtml_layout_element_set_attribute(child, "style", "display:block;height:25px");
    litehtml_layout_render(service, 800, 0);
    litehtml_layout_element_get_placement(box, &rect);
    passed = expect(std::abs(rect.width - 160.f) < .01f, "full render did not reflow changed width") && passed;
    passed = expect(litehtml_layout_element_get_placement(child, &rect) != 0 &&
                    std::abs(rect.height - 25.f) < .01f, "full render lost topology rebuild") && passed;
    // Structure already requires rebuilding. A simultaneous style change still
    // has to feed the final tree, even when topology snapshots are unnecessary.
    litehtml_layout_element_set_inner_html(box, "<span id='fresh' style='display:block;height:31px'>fresh</span>");
    litehtml_layout_element_set_attribute(box, "style", "position:absolute;width:180px;height:70px");
    litehtml_layout_render(service, 800, 0);
    auto* fresh = litehtml_layout_get_element_by_id(service, "fresh");
    passed = expect(litehtml_layout_element_get_placement(box, &rect) != 0 &&
                    std::abs(rect.width - 180.f) < .01f, "pending rebuild lost changed parent geometry") && passed;
    passed = expect(litehtml_layout_element_get_placement(fresh, &rect) != 0 &&
                    std::abs(rect.height - 31.f) < .01f, "pending rebuild lost fresh child styles") && passed;
    litehtml_layout_element_destroy(fresh);
    litehtml_layout_element_destroy(child);
    litehtml_layout_element_destroy(box);
    litehtml_layout_destroy(service);
    return passed;
}

bool test_html_id_index_mutation_lifecycle()
{
    litehtml_layout_callbacks callbacks{};
    auto* service = litehtml_layout_create(&callbacks);
    litehtml_layout_load_html(service,
        "<html><body><div id='host'><p id='duplicate'>first</p><p id='duplicate'>second</p>"
        "<section id='sub'><b id='nested'>old</b></section></div></body></html>", "", 800, 600);
    auto* host = litehtml_layout_get_element_by_id(service, "host");
    auto* first = litehtml_layout_get_element_by_id(service, "duplicate");
    auto* second = litehtml_layout_element_get_child(host, 1);
    auto* sub = litehtml_layout_get_element_by_id(service, "sub");
    auto* nested = litehtml_layout_get_element_by_id(service, "nested");
    auto matches = [&](const char* id, litehtml_layout_element* expected) {
        auto* found = litehtml_layout_get_element_by_id(service, id);
        const bool same = expected ? found && litehtml_layout_element_get_node_id(found) == litehtml_layout_element_get_node_id(expected) : !found;
        litehtml_layout_element_destroy(found);
        return same;
    };
    bool passed = expect(matches("duplicate", first), "id index must return the first duplicate in tree order");
    // Moving an existing sibling must change which duplicate wins immediately.
    litehtml_layout_element_append_child(host, first);
    passed = expect(matches("duplicate", second), "id index ignored same-parent reorder") && passed;
    litehtml_layout_element_remove_child(host, second);
    passed = expect(matches("duplicate", first), "id index retained detached first match") && passed;
    litehtml_layout_element_set_attribute(first, "ID", "renamed");
    passed = expect(matches("duplicate", nullptr) && matches("renamed", first), "id rename did not invalidate index") && passed;
    litehtml_layout_element_remove_attribute(first, "id");
    passed = expect(matches("renamed", nullptr), "removed id still indexed") && passed;

    auto* replacement = litehtml_layout_create_element(service, "article");
    litehtml_layout_element_set_attribute(replacement, "id", "replacement");
    // Build the index after detached-node setup, before replace_child's direct splice.
    passed = expect(matches("nested", nested) && matches("replacement", nullptr), "detached id leaked into index") && passed;
    litehtml_layout_element_replace_child(host, replacement, sub);
    passed = expect(matches("nested", nullptr) && matches("replacement", replacement), "replacement left stale subtree ids") && passed;
    litehtml_layout_element_set_inner_html(replacement, "<b id='fresh'>new</b>");
    auto* fresh = litehtml_layout_get_element_by_id(service, "fresh");
    passed = expect(fresh != nullptr, "innerHTML new id missing before render") && passed;
    litehtml_layout_element_set_inner_html(replacement, "plain text");
    passed = expect(matches("fresh", nullptr), "text replacement retained old element id") && passed;
    litehtml_layout_element_append_child(host, sub);
    passed = expect(matches("nested", nested), "reattached subtree id not restored") && passed;
    litehtml_layout_load_html(service, "<html><body><p id='nested'>new document</p></body></html>", "", 800, 600);
    passed = expect(matches("replacement", nullptr) && !matches("nested", nested), "reload reused stale document index") && passed;
    for(auto* node : {fresh, replacement, nested, sub, second, first, host}) litehtml_layout_element_destroy(node);
    litehtml_layout_destroy(service);
    return passed;
}

bool test_text_render_children_differential()
{
    bool passed = true;
    for(const char* parent_style : {"display:inline", "display:block", "display:flex", "display:block;overflow:auto;height:45px"})
    {
        callback_state states[2];
        litehtml_layout_callbacks callbacks[2]{};
        litehtml_layout_service* services[2]{};
        litehtml_layout_element* parents[2]{};
        litehtml_layout_element* islands[2]{};
        for(int i = 0; i < 2; ++i)
        {
            auto& cb = callbacks[i];
            cb.user = &states[i]; cb.create_font = capture_font; cb.text_width = capture_text_width;
            cb.draw_text = [](const char* text, void*, litehtml_color, const litehtml_rect*, void* user) {
                static_cast<callback_state*>(user)->painted_text.emplace_back(text ? text : "");
            };
            services[i] = litehtml_layout_create(&cb);
            const std::string html = std::string("<html><body style='width:130px'><div id='container'><span id='value' style='") +
                parent_style + "'>Old label</span><span id='next'>next sibling</span></div>"
                "<div id='other'>Second label</div><div id='island' style='position:absolute;left:200px;top:0;width:90px;height:30px'>island</div>"
                "<div id='tail' style='height:600px'>tail</div></body></html>";
            passed = expect(litehtml_layout_load_html(services[i], html.c_str(), "", 400, 200) != 0, "text tree fixture load") && passed;
            litehtml_layout_render(services[i], 400, 0);
            parents[i] = litehtml_layout_get_element_by_id(services[i], "value");
            islands[i] = litehtml_layout_get_element_by_id(services[i], "island");
        }
        auto* old = litehtml_layout_element_get_child(parents[0], 0);
        const auto old_id = litehtml_layout_element_get_node_id(old);
        const char* updates[] = {"Longer words that wrap across several lines", "Short", "中文 mixed words with spaces", " ", "again", "<b>markup</b>", "text again", "last longer update"};
        for(int step = 0; step < 8; ++step)
        {
            for(int i = 0; i < 2; ++i)
            {
                // An unrelated dirty absolute island must not suppress pending normal flow.
                litehtml_layout_element_set_inner_html(parents[i], updates[step]);
                auto* other = litehtml_layout_get_element_by_id(services[i], "other");
                litehtml_layout_element_set_inner_html(other, step % 2 ? "Second" : "Second longer label with more words");
                litehtml_layout_element_destroy(other);
                litehtml_layout_element_set_attribute(islands[i], "style", step % 2 ?
                    "position:absolute;left:200px;top:0;width:90px;height:30px;color:red" :
                    "position:absolute;left:200px;top:0;width:90px;height:30px;color:blue");
                if(step == 2 || step == 3) litehtml_layout_element_set_attribute(parents[i], "style", step == 2 ? "font-size:24px" : parent_style);
                if(i == 1)
                {
                    // Force the reference through structural rebuild with identical final DOM.
                    auto* transient = litehtml_layout_create_element(services[i], "b");
                    litehtml_layout_element_append_child(parents[i], transient);
                    litehtml_layout_element_remove_child(parents[i], transient);
                    litehtml_layout_element_destroy(transient);
                }
                litehtml_layout_render_dirty(services[i], islands[i], 400, 0);
                states[i].painted_text.clear();
                litehtml_layout_draw(services[i]);
            }
            if(states[0].painted_text != states[1].painted_text)
            {
                std::cout << "text differential " << parent_style << " step=" << step << "\n";
                for(int i=0;i<2;++i) { std::cout << i << ": "; for(const auto& t:states[i].painted_text) std::cout << "[" << t << "]"; std::cout << "\n"; }
            }
            passed = expect(states[0].painted_text == states[1].painted_text, "retained text differs from forced rebuild paint") && passed;
            for(const char* id : {"value", "next", "container", "other", "tail", "island"})
            {
                litehtml_rect rects[2]{};
                for(int i = 0; i < 2; ++i)
                {
                    auto* node = litehtml_layout_get_element_by_id(services[i], id);
                    litehtml_layout_element_get_placement(node, &rects[i]);
                    litehtml_layout_element_destroy(node);
                }
                passed = expect(std::abs(rects[0].x-rects[1].x)<.01f && std::abs(rects[0].y-rects[1].y)<.01f &&
                    std::abs(rects[0].width-rects[1].width)<.01f && std::abs(rects[0].height-rects[1].height)<.01f,
                    "retained text differs from forced rebuild geometry") && passed;
            }
            for(int i = 0; i < 2; ++i) litehtml_layout_scroll_by(services[i], 0, 100000);
            passed = expect(std::abs(litehtml_layout_get_scroll_y(services[0])-litehtml_layout_get_scroll_y(services[1]))<.01f,
                            "retained text changed scroll extent") && passed;
            for(int i = 0; i < 2; ++i) litehtml_layout_scroll_by(services[i], 0, -100000);
        }
        passed = expect(litehtml_layout_element_get_node_id(old) == old_id &&
                        std::string(litehtml_layout_element_get_text(old)) == "Old", "retained rendering changed detached DOM identity") && passed;
        litehtml_layout_element_destroy(old);
        for(int i = 0; i < 2; ++i)
        {
            litehtml_layout_element_destroy(parents[i]); litehtml_layout_element_destroy(islands[i]);
            litehtml_layout_destroy(services[i]);
        }
    }
    return passed;
}

bool test_flex_grid_container_placement_registration()
{
    bool passed = true;
    for(const char* display : {"flex", "inline-flex", "grid"})
    {
        litehtml_layout_callbacks callbacks{};
        auto* service = litehtml_layout_create(&callbacks);
        const std::string html = std::string("<html><body style='margin:0'><div id='host'><div id='box' style='display:") +
            display + ";width:120px;height:40px;margin-left:17px;margin-top:13px'><span>label</span></div></div></body></html>";
        passed = expect(litehtml_layout_load_html(service, html.c_str(), "", 400, 200) != 0, "flex/grid placement fixture") && passed;
        auto* host = litehtml_layout_get_element_by_id(service, "host");
        auto* box = litehtml_layout_get_element_by_id(service, "box");
        for(int step = 0; step < 3; ++step)
        {
            if(step) litehtml_layout_element_set_inner_html(box, step == 1 ? "<b>new child</b>" : "<span>restored</span>");
            litehtml_layout_render(service, 400, 0);
            litehtml_rect rect{};
            const bool placed = litehtml_layout_element_get_placement(box, &rect) && rect.width > 100 && rect.height > 30 &&
                rect.x >= 17 && rect.y >= 13;
            if(!placed) std::cout << display << " step=" << step << " rect=" << rect.x << ',' << rect.y << ',' << rect.width << ',' << rect.height << '\n';
            passed = expect(placed, "flex/grid container must have nonzero placed geometry after rebuild") && passed;
        }
        // A detached container must not retain geometry from the removed render tree.
        litehtml_layout_element_set_inner_html(host, "<div>replacement</div>");
        litehtml_layout_render(service, 400, 0);
        litehtml_rect detached{};
        litehtml_layout_element_get_placement(box, &detached);
        passed = expect(detached.width == 0 && detached.height == 0, "detached flex/grid must release old placement") && passed;
        litehtml_layout_element_destroy(box);
        litehtml_layout_element_destroy(host);
        litehtml_layout_destroy(service);
    }
    return passed;
}

bool test_parallel_rows_commit_differential()
{
    const char* saved = std::getenv("LITEHTML_LAYOUT_WORKERS");
    const std::string previous = saved ? saved : "";
    auto set_workers = [](const char* value) {
#ifdef _WIN32
        _putenv_s("LITEHTML_LAYOUT_WORKERS", value);
#else
        setenv("LITEHTML_LAYOUT_WORKERS", value, 1);
#endif
    };
    struct state {
        std::thread::id owner = std::this_thread::get_id();
        std::atomic<bool> callbacks_on_owner{true};
        std::vector<std::string> paint;
        int clicks = 0;
        void check() { if(std::this_thread::get_id() != owner) callbacks_on_owner = false; }
    } states[2];
    litehtml_layout_callbacks callbacks[2]{};
    litehtml_layout_service* services[2]{};
    for(int i = 0; i < 2; ++i) {
        auto& cb = callbacks[i]; cb.user = &states[i];
        cb.create_font = [](const litehtml_font_description* descr, litehtml_font_metrics* fm, void* user) -> void* {
            static_cast<state*>(user)->check();
            fm->font_size = descr->size; fm->height = descr->size * 1.2f;
            fm->ascent = descr->size * .8f; fm->descent = descr->size * .2f;
            fm->x_height = descr->size * .5f; fm->ch_width = descr->size * .5f; fm->draw_spaces = 1;
            return reinterpret_cast<void*>(1);
        };
        cb.text_width = [](const char* text, void*, void* user) {
            static_cast<state*>(user)->check(); return static_cast<float>(std::strlen(text)) * 7.f;
        };
        cb.get_image_size = [](const char*, const char*, litehtml_size* size, void* user) {
            static_cast<state*>(user)->check(); size->width = 31; size->height = 17;
        };
        cb.draw_text = [](const char* text, void*, litehtml_color, const litehtml_rect* rect, void* user) {
            auto& s = *static_cast<state*>(user); s.check();
            s.paint.push_back(std::string(text) + ":" + std::to_string(rect->x) + "," + std::to_string(rect->y) +
                "," + std::to_string(rect->width) + "," + std::to_string(rect->height));
        };
        cb.draw_image = [](const litehtml_background_layer* layer, const char*, const char*, void* user) {
            auto& s = *static_cast<state*>(user); s.check(); const auto& rect = layer->border_box;
            s.paint.push_back("image:" + std::to_string(rect.x) + "," + std::to_string(rect.y) +
                "," + std::to_string(rect.width) + "," + std::to_string(rect.height));
        };
        cb.on_anchor_click = [](const char*, void* user) { auto& s = *static_cast<state*>(user); s.check(); ++s.clicks; };
        services[i] = litehtml_layout_create(&cb);
    }
    bool passed = true;
    const char* shell = "<html><head><style>body{margin:0}.row{display:flex;align-items:center;min-height:25px;padding:1px 10px;border-bottom:1px solid black}.row img{width:18px;height:18px;margin-right:12px}.name{display:block;width:40%}.qty{width:20%}.row:nth-child(2n) .name{font-size:18px}</style></head><body><div id='rows'></div><div id='tail'>tail</div></body></html>";
    for(auto* service : services) passed = expect(litehtml_layout_load_html(service, shell, "", 500, 220) != 0, "parallel fixture load") && passed;
    litehtml_layout_element* old = nullptr;
    uint64_t old_id = 0;
    for(int step = 0; step < 8; ++step) {
        std::string html;
        for(int i = 0; i < 64; ++i) {
            const int id = step % 2 ? 63-i : i;
            const std::string suffix = std::to_string(id);
            html += "<div class='row' id='row-" + suffix + "'";
            if(id == 7 && step == 3) html += " style='margin-top:3px'"; // cross-row fallback
            if(id == 7 && step == 4) html += " style='position:relative;left:3px'"; // positional fallback
            html += "><img src='image'><a class='name' href='target' id='link-" + suffix + "'>" +
                (step % 2 ? "A much longer 中文 label " : "Short ") + suffix + "</a><span class='qty'>" +
                std::to_string(step+id) + "</span></div>";
        }
        for(int i = 0; i < 2; ++i) {
            set_workers(i == 0 ? "0" : (step < 2 ? "1" : step < 5 ? "4" : "32"));
            litehtml_layout_set_viewport(services[i], step == 2 || step == 3 ? 300.f : 500.f, 220);
            auto* rows = litehtml_layout_get_element_by_id(services[i], "rows");
            litehtml_layout_element_set_inner_html(rows, html.c_str());
            litehtml_layout_element_destroy(rows);
            passed = expect(litehtml_layout_render(services[i], step == 2 || step == 3 ? 300.f : 500.f, 0) != 0, "parallel render") && passed;
            litehtml_layout_set_scroll(services[i], 0, 0);
            states[i].paint.clear(); litehtml_layout_draw(services[i]);
        }
        if(step == 0) {
            old = litehtml_layout_get_element_by_id(services[1], "row-0");
            old_id = litehtml_layout_element_get_node_id(old);
        }
        passed = expect(states[0].paint.size() > 128 && states[0].paint == states[1].paint, "parallel commit differs in ordered painted text/image geometry") && passed;
        for(int id = 0; id < 64; ++id) {
            litehtml_rect rects[2]{};
            for(int i = 0; i < 2; ++i) {
                auto* row = litehtml_layout_get_element_by_id(services[i], ("row-"+std::to_string(id)).c_str());
                litehtml_layout_element_get_placement(row, &rects[i]); litehtml_layout_element_destroy(row);
            }
            passed = expect(rects[0].width > 0 && rects[1].width > 0 && std::abs(rects[0].x-rects[1].x)<.01f &&
                std::abs(rects[0].y-rects[1].y)<.01f && std::abs(rects[0].width-rects[1].width)<.01f &&
                std::abs(rects[0].height-rects[1].height)<.01f, "parallel committed row differs from serial") && passed;
        }
        for(int i = 0; i < 2; ++i) {
            auto* link = litehtml_layout_get_element_by_id(services[i], step % 2 ? "link-63" : "link-0");
            litehtml_rect rect{}; litehtml_layout_element_get_placement(link, &rect);
            litehtml_layout_on_mouse_move(services[i], rect.x+2, rect.y+2);
            litehtml_layout_on_mouse_down(services[i], rect.x+2, rect.y+2);
            litehtml_layout_on_mouse_up(services[i], rect.x+2, rect.y+2);
            litehtml_layout_element_destroy(link);
            litehtml_layout_scroll_by(services[i], 0, 100000);
        }
        passed = expect(states[0].clicks == step+1 && states[1].clicks == step+1, "parallel commit lost original link hit/identity") && passed;
        if(states[0].clicks != step+1 || states[1].clicks != step+1)
            std::cout << "step=" << step << " clicks=" << states[0].clicks << ',' << states[1].clicks << '\n';
        passed = expect(std::abs(litehtml_layout_get_scroll_y(services[0])-litehtml_layout_get_scroll_y(services[1]))<.01f,
                        "parallel commit changed scroll extent") && passed;
    }
    litehtml_rect detached{}; litehtml_layout_element_get_placement(old, &detached);
    passed = expect(litehtml_layout_element_get_node_id(old) == old_id && detached.width == 0, "parallel retained detached geometry/changed identity") && passed;
    litehtml_layout_element_destroy(old);
    for(int i = 0; i < 2; ++i) {
        passed = expect(states[i].callbacks_on_owner, "parallel invoked host callback on worker") && passed;
        litehtml_layout_destroy(services[i]);
    }
    set_workers(previous.c_str());
    return passed;
}

bool test_mixed_block_inline_transfer_preserves_paint()
{
    callback_state state;
    litehtml_layout_callbacks cb{}; cb.user = &state;
    cb.create_font = capture_font; cb.text_width = capture_text_width;
    cb.draw_text = [](const char* text, void*, litehtml_color, const litehtml_rect*, void* user) {
        const std::string value = text ? text : "";
        if(value.find_first_not_of(" \r\n\t") != std::string::npos)
            static_cast<callback_state*>(user)->painted_text.push_back(value);
    };
    auto* service = litehtml_layout_create(&cb);
    const char* html = "<html><body><div id='root'>Leading <span>inline <div>block</div>trailing</span> ending<div>tail</div>last</div></body></html>";
    bool passed = expect(litehtml_layout_load_html(service, html, "", 400, 300) != 0, "mixed inline fixture");
    auto* root = litehtml_layout_get_element_by_id(service, "root");
    for(int step = 0; step < 3; ++step) {
        if(step) litehtml_layout_element_set_attribute(root, "style", step == 1 ? "width:80px" : "width:300px");
        state.painted_text.clear();
        litehtml_layout_render(service, 400, 0); litehtml_layout_draw(service);
        passed = expect(state.painted_text == std::vector<std::string>{"Leading", "inline", "block", "trailing", "ending", "tail", "last"},
                        "inline/block grouping lost or reordered painted content") && passed;
    }
    litehtml_layout_element_destroy(root); litehtml_layout_destroy(service);
    return passed;
}

bool test_grid_inline_initializes_once()
{
    litehtml_layout_callbacks cb{};
    auto* service = litehtml_layout_create(&cb);
    bool passed = expect(litehtml_layout_load_html(service,
        "<html><body><div id='grid' style='display:grid;width:120px;height:40px'><span id='value'>Word</span></div></body></html>",
        "", 400, 200) != 0, "single init fixture load");
    auto* value = litehtml_layout_get_element_by_id(service, "value");
    for(int step = 0; step < 3; ++step) {
        if(step) litehtml_layout_element_set_inner_html(value, step == 1 ? "<b>Word</b>" : "Word");
        litehtml_layout_render(service, 400, 0);
        // html/body/grid/span/text, plus b in the middle state. No split inline
        // fragments here: one reference per rendered DOM node is sufficient.
        const auto references = litehtml_layout_get_render_reference_count(service);
        const uint64_t expected = step == 1 ? 6 : 5;
        if(references != expected) std::cout << "single init step=" << step << " references=" << references << " expected=" << expected << '\n';
        passed = expect(references == expected, "grid anonymous wrapper initialized DOM render items more than once") && passed;
    }
    litehtml_layout_element_destroy(value);
    litehtml_layout_destroy(service);
    return passed;
}

bool test_font_cache_key_property_isolation()
{
    using namespace litehtml;
    font_description base{"Arial", pixel_t(16), font_style_normal, 400, 0, css_length{},
        text_decoration_style_solid, web_color(false), "", web_color(false), text_emphasis_position_over};
    const font_description_less less;
    const auto equal = [&](const font_description& a, const font_description& b) { return !less(a,b) && !less(b,a); };
    bool passed = expect(equal(base, base), "font key must reuse an identical descriptor");
    std::vector<font_description> variants(11, base);
    variants[0].family = "Other";
    variants[1].size = 17;
    variants[2].style = font_style_italic;
    variants[3].weight = 700;
    variants[4].decoration_line = 1;
    variants[5].decoration_thickness.set_value(2, css_units_px);
    variants[6].decoration_style = text_decoration_style_dashed;
    variants[7].decoration_color.red = 80;
    variants[8].emphasis_style = "dot";
    variants[9].emphasis_color.alpha = 80;
    variants[10].emphasis_position = text_emphasis_position_under;
    for(const auto& variant : variants)
        passed = expect(!equal(base, variant), "font cache aliased a distinct font property") && passed;
    auto a = base, b = base;
    a.decoration_color.is_current_color = true;
    passed = expect(!equal(a,b), "font cache aliased currentColor and literal black") && passed;
    b = a; b.decoration_color.red = 20;
    passed = expect(equal(a,b), "currentColor must ignore inactive RGBA") && passed;
    a = b = base;
    a.decoration_thickness.set_value(1, css_units_px);
    b.decoration_thickness.set_value(1, css_units_percentage);
    passed = expect(!equal(a,b), "font cache aliased thickness units") && passed;
    a.decoration_thickness.predef(0); b.decoration_thickness.predef(0);
    passed = expect(equal(a,b), "predefined thickness must ignore inactive units") && passed;
    b.decoration_thickness.predef(1);
    passed = expect(!equal(a,b), "font cache aliased predefined thickness values") && passed;
    a.decoration_thickness.set_value(1, css_units_px);
    b.decoration_thickness.set_value(std::nextafter(1.f, 2.f), css_units_px);
    passed = expect(!equal(a,b), "font cache rounded distinct numeric thicknesses together") && passed;
    a = b = base; a.size = std::numeric_limits<float>::quiet_NaN(); b = a;
    passed = expect(equal(a,b) && !equal(a,base), "nonfinite font size broke cache ordering") && passed;
    return passed;
}

bool test_font_cache_reuses_handles_and_metrics()
{
    struct font_state { std::vector<float> sizes; int deleted = 0; } state;
    litehtml_layout_callbacks cb{}; cb.user = &state;
    cb.create_font = [](const litehtml_font_description* descr, litehtml_font_metrics* fm, void* user) -> void* {
        auto& s = *static_cast<font_state*>(user);
        s.sizes.push_back(descr->size);
        *fm = {}; fm->font_size = descr->size; fm->height = descr->size;
        fm->ascent = descr->size*.8f; fm->descent = descr->size*.2f;
        fm->x_height = fm->ch_width = descr->size*.5f; fm->draw_spaces = 1;
        return reinterpret_cast<void*>(s.sizes.size());
    };
    cb.text_width = [](const char* text, void* font, void* user) -> float {
        const auto& sizes = static_cast<font_state*>(user)->sizes;
        return float(std::strlen(text)) * sizes.at(reinterpret_cast<uintptr_t>(font)-1) * .5f;
    };
    cb.delete_font = [](void*, void* user) { ++static_cast<font_state*>(user)->deleted; };
    auto* service = litehtml_layout_create(&cb);
    bool passed = expect(litehtml_layout_load_html(service,
        "<html><body><div id='rows'></div></body></html>", "", 500, 300) != 0, "font cache fixture load");
    auto* rows = litehtml_layout_get_element_by_id(service, "rows");
    size_t initial_count = 0;
    for(int step = 0; step < 3; ++step) {
        litehtml_layout_element_set_inner_html(rows,
            "<span id='small' style='display:inline-block;font:20px Arial'>XX</span>"
            "<span id='large' style='display:inline-block;font:30px Arial'>XX</span>"
            "<span style='font:20px Arial'>again</span>");
        passed = expect(litehtml_layout_render(service, 500, 0) != 0, "font cache render") && passed;
        if(step == 0) initial_count = state.sizes.size();
        passed = expect(state.sizes.size() == initial_count, "rebuilding identical styles recreated cached fonts") && passed;
        for(const auto& item : {std::make_pair("small",20.f),std::make_pair("large",30.f)}) {
            auto* node = litehtml_layout_get_element_by_id(service, item.first);
            litehtml_rect rect{};
            passed = expect(litehtml_layout_element_get_placement(node, &rect) && std::fabs(rect.width-item.second)<.01f,
                            "cached font handle selected wrong text metrics") && passed;
            litehtml_layout_element_destroy(node);
        }
    }
    passed = expect(std::count(state.sizes.begin(),state.sizes.end(),20.f)==1 &&
                    std::count(state.sizes.begin(),state.sizes.end(),30.f)==1, "distinct sizes must each create one font") && passed;
    litehtml_layout_element_destroy(rows); litehtml_layout_destroy(service);
    return expect(state.deleted == int(state.sizes.size()), "cached fonts must be released exactly once") && passed;
}

bool test_tag_index_mutation_lifecycle()
{
    litehtml_layout_callbacks cb{};
    auto* service = litehtml_layout_create(&cb);
    litehtml_layout_load_html(service,
        "<html><body><div id='host'><BUTTON id='a'>A</BUTTON><section id='sub'><button id='b'>B</button></section></div></body></html>", "", 600, 400);
    auto* host = litehtml_layout_get_element_by_id(service, "host");
    auto* a = litehtml_layout_get_element_by_id(service, "a");
    auto* b = litehtml_layout_get_element_by_id(service, "b");
    auto* sub = litehtml_layout_get_element_by_id(service, "sub");
    const auto matches = [&](const char* tag, std::initializer_list<litehtml_layout_element*> expected) {
        if(litehtml_layout_get_elements_by_tag_count(service, tag) != int(expected.size())) return false;
        int index = 0;
        for(auto* node : expected) {
            auto* found = litehtml_layout_get_element_by_tag(service, tag, index++);
            const bool same = found && litehtml_layout_element_get_node_id(found) == litehtml_layout_element_get_node_id(node);
            litehtml_layout_element_destroy(found);
            if(!same) return false;
        }
        return litehtml_layout_get_element_by_tag(service, tag, index) == nullptr;
    };
    bool passed = expect(matches("BuTtOn", {a,b}) && matches("button", {a,b}) && matches("video", {}),
                         "tag lookup must preserve case-insensitive preorder and empty results");
    litehtml_layout_element_append_child(host,a);
    passed = expect(matches("button", {b,a}), "tag index ignored sibling move") && passed;
    litehtml_layout_element_remove_child(host,sub);
    passed = expect(matches("button", {a}), "retained detached subtree remained in tag index") && passed;
    litehtml_layout_element_append_child(host,sub);
    passed = expect(matches("button", {a,b}), "reattached subtree missing from tag index") && passed;
    auto* replacement = litehtml_layout_create_element(service,"BuTtOn");
    passed = expect(matches("button", {a,b}), "detached element leaked into tag index") && passed;
    litehtml_layout_element_replace_child(host,replacement,sub);
    passed = expect(matches("button", {a,replacement}), "direct replace left stale tag index") && passed;
    litehtml_layout_element_set_inner_html(host,"<input id='fresh'><button id='c'>C</button>");
    auto* fresh = litehtml_layout_get_element_by_id(service,"fresh");
    auto* c = litehtml_layout_get_element_by_id(service,"c");
    passed = expect(matches("button", {c}) && matches("input", {fresh}), "innerHTML tags not immediately visible") && passed;
    litehtml_layout_render(service,600,0);
    passed = expect(matches("button", {c}), "render invalidated tag identity") && passed;
    litehtml_layout_element_set_inner_html(host,"plain text");
    passed = expect(matches("button", {}) && matches("input", {}), "text replacement retained old tags") && passed;
    litehtml_layout_load_html(service,"<html><body><video id='new'></video></body></html>","",600,400);
    auto* video = litehtml_layout_get_element_by_id(service,"new");
    passed = expect(matches("button", {}) && matches("video", {video}), "reload reused prior tag index") && passed;
    litehtml_layout_load_html(service,
        "<html><head><style>.generated::before{content:url(fixture.png)}</style></head><body><div id='pseudo'></div></body></html>", "", 600, 400);
    auto* pseudo = litehtml_layout_get_element_by_id(service,"pseudo");
    litehtml_layout_render(service,600,0);
    passed = expect(matches("img", {}), "unexpected generated image before class change") && passed;
    for(int step=0; step<3; ++step) {
        litehtml_layout_element_set_attribute(pseudo,"class",step==1 ? "" : "generated");
        litehtml_layout_render(service,600,0);
        passed = expect(litehtml_layout_get_elements_by_tag_count(service,"img") == (step==1 ? 0 : 1),
                        "style-generated image left a stale tag index") && passed;
    }
    litehtml_layout_element_destroy(pseudo);
    for(auto* node : {video,c,fresh,replacement,sub,b,a,host}) litehtml_layout_element_destroy(node);
    litehtml_layout_destroy(service);
    return passed;
}

bool test_style_cascade_and_background_reset()
{
    struct state { int gradients = 0; } observed;
    litehtml_layout_callbacks cb{}; cb.user = &observed;
    cb.draw_linear_gradient = [](const litehtml_background_layer*, const litehtml_linear_gradient*, void* user) {
        ++static_cast<state*>(user)->gradients;
    };
    auto* service = litehtml_layout_create(&cb);
    const char* html = "<html><head><style>"
        ".box{width:20px;height:30px}.box{width:30px!important}#box{width:60px}"
        ".box{width:40px!important}.box.painted{background-image:linear-gradient(red,blue)}"
        "</style></head><body><div id='box' class='box painted' style='width:80px'></div>"
        "<div id='plain' style='width:20px;height:20px'></div></body></html>";
    bool passed = expect(litehtml_layout_load_html(service,html,"",500,300)!=0,"cascade fixture load");
    auto* box = litehtml_layout_get_element_by_id(service,"box");
    const auto check = [&](float width, int gradients) {
        observed.gradients=0;
        if(!litehtml_layout_render(service,500,0)) return false;
        litehtml_layout_draw(service);
        litehtml_rect rect{};
        return litehtml_layout_element_get_placement(box,&rect) && std::fabs(rect.width-width)<.01f &&
            observed.gradients==gradients;
    };
    passed = expect(check(40,1),"later important declaration must beat normal inline style") && passed;
    litehtml_layout_element_set_attribute(box,"style","width:70px!important");
    passed = expect(check(70,1),"important inline declaration must win") && passed;
    litehtml_layout_element_set_attribute(box,"class","box");
    passed = expect(check(70,0),"background defaults did not clear the previous gradient") && passed;
    litehtml_layout_element_remove_attribute(box,"style");
    litehtml_layout_element_set_attribute(box,"class","box painted");
    passed = expect(check(40,1),"restored cascade/background leaked another element's computed state") && passed;
    litehtml_layout_element_destroy(box); litehtml_layout_destroy(service);
    return passed;
}

bool test_variable_shorthand_expansion_and_refresh()
{
    litehtml_layout_callbacks cb{};
    auto* service = litehtml_layout_create(&cb);
    bool passed = expect(litehtml_layout_load_html(service,
        "<html><head><style>body{margin:0}#parent{--w:60px;--pad:3px 7px;--edge:2px solid black}"
        "#child{width:var(--w)!important;height:var(--missing,20px);padding:var(--pad);border:var(--edge);"
        "margin:var(--missing-margin,1px 2px);background:var(--bg,red);border-radius:var(--radius,4px)}"
        "</style></head><body><div id='parent'><div id='child' style='width:90px'></div></div></body></html>",
        "",500,300)!=0,"var shorthand fixture load");
    auto* parent = litehtml_layout_get_element_by_id(service,"parent");
    auto* child = litehtml_layout_get_element_by_id(service,"child");
    for(int step=0;step<3;++step) {
        if(step) litehtml_layout_element_set_attribute(parent,"style",step==1 ? "--w:80px;--pad:5px 9px" : "--w:60px;--pad:3px 7px");
        passed = expect(litehtml_layout_render(service,500,0)!=0,"var shorthand render") && passed;
        litehtml_rect rect{};
        const float width = step==1 ? 80.f : 60.f;
        const float left = step==1 ? 13.f : 11.f;
        const bool placed = litehtml_layout_element_get_placement(child,&rect) != 0;
        std::cout << "variable shorthand geometry " << step << ": " << rect.x << "," << rect.y << " " << rect.width << " x " << rect.height << '\n';
        passed = expect(placed && std::fabs(rect.width-width)<.01f &&
                        std::fabs(rect.height-20.f)<.01f && std::fabs(rect.x-left)<.01f,
                        "var shorthand expansion lost inherited, fallback or important values") && passed;
    }
    litehtml_layout_element_destroy(child); litehtml_layout_element_destroy(parent); litehtml_layout_destroy(service);
    return passed;
}

bool test_shared_background_layers_survive_style_moves()
{
    struct background_state { std::vector<std::string> urls; bool nonzero = true; } state;
    litehtml_layout_callbacks cb{};
    cb.user = &state;
    cb.get_image_size = [](const char*, const char*, litehtml_size* size, void*) { size->width=10; size->height=10; };
    cb.draw_image = [](const litehtml_background_layer* layer, const char* url, const char*, void* user) {
        auto& output = *static_cast<background_state*>(user);
        output.urls.emplace_back(url ? url : "");
        output.nonzero &= layer->border_box.width > 0 && layer->border_box.height > 0;
    };
    auto* service = litehtml_layout_create(&cb);
    bool passed = expect(litehtml_layout_load_html(service,
        "<html><head><style>body{margin:0}.shared{width:80px;height:20px;"
        "background:url(first.png) no-repeat,url(second.png) no-repeat;"
        "background-size:10px 10px,10px 10px}"
        "#a{background-image:url(override.png)!important;background-image:url(ignored.png)}"
        "</style></head><body><div id='a' class='shared'></div><div id='b' class='shared'></div>"
        "<div id='c' class='shared'></div></body></html>", "", 500,300)!=0,
        "shared background fixture loads");
    auto* b = litehtml_layout_get_element_by_id(service,"b");
    for(int step=0;step<3;++step) {
        if(step) litehtml_layout_element_set_attribute(b,"class",step==1 ? "" : "shared");
        state.urls.clear(); state.nonzero=true;
        passed = expect(litehtml_layout_render(service,500,0)!=0,"shared background render") && passed;
        litehtml_layout_draw(service);
        const auto count = [&](const char* url) { return std::count(state.urls.begin(),state.urls.end(),url); };
        const int shared = step==1 ? 1 : 2;
        passed = expect(state.nonzero && count("override.png")==1 && count("ignored.png")==0 &&
                        count("first.png")==shared && count("second.png")==shared &&
                        state.urls.size()==size_t(1+shared*2),
                        "moving parser results consumed shared rules or overwrote important background") && passed;
    }
    litehtml_layout_element_destroy(b); litehtml_layout_destroy(service);
    return passed;
}

bool test_split_inline_link_hit_after_rebuild()
{
    struct split_state { std::vector<litehtml_rect> boxes; int clicks=0; } state;
    litehtml_layout_callbacks cb{}; cb.user=&state;
    cb.create_font = [](const litehtml_font_description* descr, litehtml_font_metrics* fm, void*) -> void* {
        fm->font_size=descr->size; fm->height=20; fm->ascent=15; fm->descent=5;
        fm->x_height=8; fm->ch_width=7; fm->draw_spaces=1;
        return reinterpret_cast<void*>(1);
    };
    cb.text_width = [](const char* text, void*, void*) { return float(std::strlen(text))*7; };
    cb.draw_text = [](const char* text, void*, litehtml_color, const litehtml_rect* rect, void* user) {
        if(text && (std::strcmp(text,"head")==0 || std::strcmp(text,"middle")==0 || std::strcmp(text,"tail")==0))
            static_cast<split_state*>(user)->boxes.push_back(*rect);
    };
    cb.on_anchor_click = [](const char* url, void* user) {
        if(url && std::strcmp(url,"destination")==0) ++static_cast<split_state*>(user)->clicks;
    };
    auto* service=litehtml_layout_create(&cb);
    bool passed=expect(litehtml_layout_load_html(service,
        "<html><body style='margin:0'><div id='host'><a href='destination'>head"
        "<span style='display:block'>middle</span>tail</a></div></body></html>","",400,300)!=0,
        "split inline link fixture loads");
    auto* host=litehtml_layout_get_element_by_id(service,"host");
    for(int step=0;step<3;++step) {
        if(step) litehtml_layout_element_set_attribute(host,"style",step==1 ? "width:80px" : "width:300px");
        state.boxes.clear(); state.clicks=0;
        passed=expect(litehtml_layout_render(service,400,0)!=0,"split link renders after rebuild") && passed;
        litehtml_layout_draw(service);
        passed=expect(state.boxes.size()==3,"split link has three actual text fragments") && passed;
        for(const auto& box:state.boxes) {
            passed=expect(box.width>0 && box.height>0,"split link text has nonzero geometry") && passed;
            const float x=box.x+box.width/2, y=box.y+box.height/2;
            litehtml_layout_on_mouse_move(service,x,y);
            litehtml_layout_on_mouse_down(service,x,y);
            litehtml_layout_on_mouse_up(service,x,y);
        }
        passed=expect(state.clicks==3,"every split link fragment remains hittable after rebuilding references") && passed;
    }
    litehtml_layout_element_destroy(host); litehtml_layout_destroy(service);
    return passed;
}

bool test_parser_scratch_preserves_fragment_order()
{
    litehtml_layout_callbacks cb{};
    auto* service = litehtml_layout_create(&cb);
    const std::string content = "<span id='first'>Alpha &amp; beta \xE4\xB8\xAD\xE6\x96\x87</span>"
        "<!--ignored--><span id='last'>tail</span><script>var x = 'a b';</script>"
        "<table><tbody><tr><td id='cell'>cell text</td></tr></tbody></table>";
    const std::string html = "<html><body><div id='host'>" + content + "</div></body></html>";
    bool passed = expect(litehtml_layout_load_html(service, html.c_str(), "", 400, 300) != 0,
                         "parser scratch fixture loads");
    auto* host = litehtml_layout_get_element_by_id(service, "host");
    for(int step = 0; step < 3; ++step) {
        if(step) passed = expect(litehtml_layout_element_set_inner_html(host, content.c_str()) != 0,
                                 "parser fragment replaces repeatedly") && passed;
        const char* tags[] = {"span", "span", "script", "table"};
        int named_index = 0;
        for(int index = 0; index < litehtml_layout_element_get_child_count(host); ++index) {
            auto* child = litehtml_layout_element_get_child(host, index);
            const char* tag = child ? litehtml_layout_element_get_tag_name(child) : nullptr;
            // The current C API also enumerates comments with an empty tag.
            // Compare named elements without imposing a new traversal contract.
            if(tag && *tag) {
                passed = expect(named_index < 4 && std::strcmp(tag, tags[named_index]) == 0,
                                "parser sibling order survives scratch reuse") && passed;
                ++named_index;
            }
            litehtml_layout_element_destroy(child);
        }
        passed = expect(named_index == 4, "all named parser siblings remain present") && passed;
        const char* ids[] = {"first", "last", "cell"};
        const char* texts[] = {"Alpha & beta \xE4\xB8\xAD\xE6\x96\x87", "tail", "cell text"};
        for(int index = 0; index < 3; ++index) {
            auto* node = litehtml_layout_get_element_by_id(service, ids[index]);
            const char* text = node ? litehtml_layout_element_get_text(node) : nullptr;
            passed = expect(text && std::strcmp(text, texts[index]) == 0,
                            "split text entities Unicode and table content preserved") && passed;
            litehtml_layout_element_destroy(node);
        }
        const char* script = litehtml_layout_get_script(service, 0);
        passed = expect(litehtml_layout_get_script_count(service) == 1 && script &&
                        std::strcmp(script, "var x = 'a b';") == 0,
                        "script virtual append preserves unsplit source") && passed;
    }
    litehtml_layout_element_destroy(host);
    litehtml_layout_destroy(service);
    return passed;
}

int main()
{
    if(!test_parser_scratch_preserves_fragment_order()) return EXIT_FAILURE;
    return test_split_inline_link_hit_after_rebuild() && test_shared_background_layers_survive_style_moves() && test_variable_shorthand_expansion_and_refresh() && test_style_cascade_and_background_reset() && test_tag_index_mutation_lifecycle() && test_font_cache_key_property_isolation() && test_font_cache_reuses_handles_and_metrics() && test_mixed_block_inline_transfer_preserves_paint() && test_grid_inline_initializes_once() && test_parallel_rows_commit_differential() && test_flex_grid_container_placement_registration() && test_text_render_children_differential() && test_html_id_index_mutation_lifecycle() && test_full_render_and_dirty_geometry_validation() && test_plain_text_replacement_preserves_selector_matches() && test_rebuild_does_not_accumulate_render_references() && test_equal_size_numeric_text_replacement() && test_initial_base_url() && test_html_base_overrides_initial_url() &&
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
                   test_viewport_dimensions_must_be_finite_positive() &&
				   test_element_attribute_names_are_enumerable() &&
				   test_font_callback_carries_text_decoration()
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
