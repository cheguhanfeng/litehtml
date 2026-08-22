#ifndef LITEHTML_DOCUMENT_H
#define LITEHTML_DOCUMENT_H

#include "stylesheet.h"
#include "encodings.h"
#include "font_description.h"
#include "master_css.h"
#include "types.h"

#include <functional>
#include <cstdint>
#include <map>
#include <vector>

using GumboOutput = struct GumboInternalOutput;

namespace litehtml
{
    struct style_invalidation_stats
    {
        uint64_t computed_refresh_count    = 0;
        uint64_t subtree_match_count       = 0;
        uint64_t full_match_count          = 0;
        uint64_t computed_refresh_elements = 0;
        uint64_t subtree_match_elements    = 0;
        uint64_t full_match_elements       = 0;
        uint64_t computed_refresh_ns       = 0;
        uint64_t subtree_match_ns          = 0;
        uint64_t full_match_ns             = 0;
        uint64_t render_tree_fallback_count = 0;
    };

    struct css_text
    {
        using vector = std::vector<css_text>;

        std::string text;
        std::string baseurl;
        std::string media;

        css_text() = default;

        css_text(const char* txt, const char* url, const char* media_str) :
            text(txt ? txt : ""),
            baseurl(url ? url : ""),
            media(media_str ? media_str : "")
        {
        }
    };

    class dumper
    {
      public:
        virtual ~dumper()                                                        = default;
        virtual void begin_node(const std::string& descr)                        = 0;
        virtual void end_node()                                                  = 0;
        virtual void begin_attrs_group(const std::string& descr)                 = 0;
        virtual void end_attrs_group()                                           = 0;
        virtual void add_attr(const std::string& name, const std::string& value) = 0;
    };

    class html_tag;
    class render_item;

    class document : public std::enable_shared_from_this<document>
    {
      public:
        using ptr      = std::shared_ptr<document>;
        using weak_ptr = std::weak_ptr<document>;

      private:
        std::shared_ptr<element>                m_root;
        std::shared_ptr<render_item>            m_root_render;
        document_container*                     m_container;
        fonts_map                               m_fonts;
        css_text::vector                        m_css;
        litehtml::css                           m_styles;
        litehtml::web_color                     m_def_color;
        litehtml::css                           m_master_css;
        litehtml::css                           m_user_css;
        litehtml::size                          m_size;
        position::vector                        m_fixed_boxes;
        std::shared_ptr<element>                m_over_element;
        std::shared_ptr<element>                m_active_element;
        std::list<std::shared_ptr<render_item>> m_tabular_elements;
        media_query_list_list::vector           m_media_lists;
        media_features                          m_media;
        std::string                             m_lang;
        std::string                             m_culture;
        std::string                             m_text;
        document_mode                           m_mode      = no_quirks_mode;
        bool                                    m_finalized = false;
        bool                                    m_styles_dirty      = false;
        bool                                    m_render_tree_dirty = false;

        enum class style_match_scope : uint8_t
        {
            none,
            subtree,
            full
        };

        // Selector dependencies are summarized once after CSS parsing. An
        // attribute mutation can then decide whether matching is confined to
        // its subtree or must conservatively cover the full document.
        std::map<string_id, style_match_scope>  m_attribute_dependencies;
        style_match_scope                       m_class_dependency = style_match_scope::none;
        style_match_scope                       m_id_dependency    = style_match_scope::none;
        bool                                    m_structure_requires_full_match = false;
        bool                                    m_scoped_selector_match_required = false;
        std::weak_ptr<element>                  m_scoped_styles_dirty_root;
        style_invalidation_stats                m_style_invalidation_stats;

      public:
        document(document_container* objContainer);
        virtual ~document();

        document_container* container() const
        {
            return m_container;
        }
        document_mode mode() const
        {
            return m_mode;
        }
        uint_ptr  get_font(const font_description& descr, font_metrics* fm);
        pixel_t   render(pixel_t max_width, render_type rt = render_all);
        // Re-render a DOM subtree when its render item is self-contained.  The
        // method deliberately falls back to a normal render when the mutation
        // can affect normal-flow siblings or requires a render-tree rebuild.
        pixel_t   render_dirty(const std::shared_ptr<element>& root, pixel_t max_width, render_type rt = render_all);
        void      draw(uint_ptr hdc, pixel_t x, pixel_t y, const position* clip);
        web_color get_def_color() const
        {
            return m_def_color;
        }
        void                         cvt_units(css_length& val, const font_metrics& metrics, pixel_t size) const;
        pixel_t                      to_pixels(const css_length& val, const font_metrics& metrics, pixel_t size) const;
        pixel_t                      width() const;
        pixel_t                      height() const;
        pixel_t                      content_width() const;
        pixel_t                      content_height() const;
        void                         add_stylesheet(const char* str, const char* baseurl, const char* media);
        bool                         on_mouse_over(pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y,
                                                   const std::function<void(const position&)>& redraw_box);
        std::vector<scroll_values>   on_scroll(pixel_t dx, pixel_t dy, pixel_t x, pixel_t y, pixel_t client_x,
                                               pixel_t client_y) const;
        bool                         on_lbutton_down(pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y,
                                                     const std::function<void(const position&)>& redraw_box);
        bool                         on_lbutton_up(pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y,
                                                   const std::function<void(const position&)>& redraw_box, bool activate_default = true);
        bool                         on_button_cancel(const std::function<void(const position&)>& redraw_box);
        bool                         on_mouse_leave(const std::function<void(const position&)>& redraw_box);
        std::shared_ptr<element>     create_element(const char* tag_name, const string_map& attributes);
        std::shared_ptr<element>     root();
        std::shared_ptr<render_item> root_render();
        void                         get_fixed_boxes(position::vector& fixed_boxes);
        void                         add_fixed_box(const position& pos);
        void                         add_media_list(const media_query_list_list::ptr& list);
        bool                         media_changed();
        bool                         lang_changed();
        bool                         match_lang(const std::string& lang);
        void                         add_tabular(const std::shared_ptr<render_item>& el);
        // Schedules a full stylesheet match and render-tree refresh.
        void                         invalidate_styles();
        // Compatibility overload for inline-style mutations.
        void                         invalidate_styles(const std::shared_ptr<element>& root);
        // Queue an attribute mutation using the cached selector dependency
        // summary. Unsafe sibling/nested/pseudo-element dependencies upgrade
        // to a full stylesheet match.
        void                         invalidate_attribute_styles(const std::shared_ptr<element>& root,
                                                                 const char* attribute);
        // Structural mutations always rebuild render items, but their CSS
        // matching can normally stay within the affected parent subtree.
        void                         invalidate_structure_styles(const std::shared_ptr<element>& root);
        const style_invalidation_stats& style_stats() const;
        void                            reset_style_stats();
        std::shared_ptr<const element> get_over_element() const
        {
            return m_over_element;
        }

        void append_children_from_string(element& parent, const char* str, bool replace_existing);
        /** Replace an element's children and schedule the required rebuild. */
        bool set_inner_html(const std::shared_ptr<element>& parent, const char* str);
        /** Attach a caller-created element and schedule the required rebuild. */
        bool append_child(const std::shared_ptr<element>& parent, const std::shared_ptr<element>& child);
        /** Detach a direct child and schedule the required rebuild. */
        bool remove_child(const std::shared_ptr<element>& parent, const std::shared_ptr<element>& child);
        /** Replace a direct child and schedule the required rebuild. */
        bool replace_child(const std::shared_ptr<element>& parent, const std::shared_ptr<element>& replacement,
                           const std::shared_ptr<element>& child);
        void dump(dumper& cout);

        // see doc/document_createFromString.txt
        static document::ptr createFromString(const estring& str, document_container* container,
                                              const std::string& master_styles = litehtml::master_css,
                                              const std::string& user_styles   = {});

        // The mode must be set before any element is created, because it is used in html_tag::set_attr.
        void set_document_mode(document_mode mode);
        // Finish a document whose element tree was built by the caller with create_element() and
        // element::appendChild() instead of being parsed from an HTML string. Runs the same
        // finalization sequence createFromString() runs after parsing. A document is finalized
        // once, when its tree is complete; any further call does nothing.
        void finalize_from_external_root(const std::shared_ptr<element>& root,
                                         const std::string&              master_styles = litehtml::master_css,
                                         const std::string&              user_styles   = {});

      private:
        uint_ptr add_font(const font_description& descr, font_metrics* fm);

        GumboOutput* parse_html(estring str);
        void         create_node(void* gnode, elements_list& elements, bool parseTextNode, bool process_root);
        bool         update_media_lists(const media_features& features);
        void         fix_tables_layout();
        void         rebuild_selector_dependencies();
        void         rebuild_all_styles();
        void         prepare_scoped_styles();
        bool         rematch_styles(const std::shared_ptr<element>& root, bool match_selectors);
        void         schedule_scoped_style_match(const std::shared_ptr<element>& root, bool match_selectors);
        bool         is_connected(const std::shared_ptr<element>& root) const;
        void         rebuild_render_tree();
        void fix_table_children(const std::shared_ptr<render_item>& el_ptr, style_display disp, const char* disp_str);
        void fix_table_parent(const std::shared_ptr<render_item>& el_ptr, style_display disp, const char* disp_str);
    };

    inline std::shared_ptr<element> document::root()
    {
        return m_root;
    }

    inline std::shared_ptr<render_item> document::root_render()
    {
        return m_root_render;
    }

    inline void document::add_tabular(const std::shared_ptr<render_item>& el)
    {
        m_tabular_elements.push_back(el);
    }

    inline bool document::match_lang(const std::string& lang)
    {
        return lang == m_lang || lang == m_culture;
    }
} // namespace litehtml

#endif // LITEHTML_DOCUMENT_H
