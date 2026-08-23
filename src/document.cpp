#include "html.h"
#include "document.h"
#include "document_container.h"
#include "el_anchor.h"
#include "el_base.h"
#include "el_body.h"
#include "el_break.h"
#include "el_cdata.h"
#include "el_comment.h"
#include "el_div.h"
#include "el_font.h"
#include "el_image.h"
#include "el_link.h"
#include "el_para.h"
#include "el_script.h"
#include "el_space.h"
#include "el_style.h"
#include "el_table.h"
#include "el_td.h"
#include "el_text.h"
#include "el_title.h"
#include "el_tr.h"
#include "gumbo.h"
#include "html_tag.h"
#include "render_block.h"
#include "render_item.h"
#include "render_table.h"
#include "stylesheet.h"
#include "types.h"

#include <chrono>

namespace litehtml
{
    namespace
    {
        bool selector_has_pseudo_element(const css_selector& selector)
        {
            for(const auto& part : selector.m_right.m_attrs)
            {
                if(part.type == select_pseudo_element)
                {
                    return true;
                }
                for(const auto& nested : part.selector_list)
                {
                    if(nested && selector_has_pseudo_element(*nested))
                    {
                        return true;
                    }
                }
            }
            return selector.m_left && selector_has_pseudo_element(*selector.m_left);
        }

        bool selector_has_structural_pseudo(const css_selector& selector)
        {
            for(const auto& part : selector.m_right.m_attrs)
            {
                if(part.type == select_pseudo_class &&
                   (part.name == _only_child_ || part.name == _only_of_type_ || part.name == _first_child_ ||
                    part.name == _first_of_type_ || part.name == _last_child_ || part.name == _last_of_type_ ||
                    part.name == _nth_child_ || part.name == _nth_of_type_ || part.name == _nth_last_child_ ||
                    part.name == _nth_last_of_type_))
                {
                    return true;
                }
                for(const auto& nested : part.selector_list)
                {
                    if(nested && selector_has_structural_pseudo(*nested)) return true;
                }
            }
            return selector.m_left && selector_has_structural_pseudo(*selector.m_left);
        }

        struct style_topology_entry
        {
            const element* node;
            style_display  display;
            element_position position;
            string_id      tag;
            size_t         child_count;
            uint64_t       layout_signature;
        };

        uint64_t layout_signature(const css_properties& css)
        {
            uint64_t hash = 1469598103934665603ull;
            auto add = [&hash](const std::string& value) {
                for(const unsigned char ch : value) { hash ^= ch; hash *= 1099511628211ull; }
                hash ^= 0xff; hash *= 1099511628211ull;
            };
            add(std::to_string(static_cast<int>(css.get_display())));
            add(std::to_string(static_cast<int>(css.get_position())));
            add(std::to_string(static_cast<int>(css.get_float())));
            add(std::to_string(static_cast<int>(css.get_overflow())));
            add(css.get_width().to_string()); add(css.get_height().to_string());
            add(css.get_min_width().to_string()); add(css.get_min_height().to_string());
            add(css.get_max_width().to_string()); add(css.get_max_height().to_string());
            add(css.get_margins().to_string()); add(css.get_padding().to_string());
            add(std::to_string(static_cast<float>(css.get_font_size())));
            add(css.get_flex_basis().to_string()); add(std::to_string(css.get_flex_grow()));
            add(std::to_string(css.get_flex_shrink()));
            add(css.get_grid_template_columns()); add(css.get_grid_template_rows());
            return hash;
        }

        void collect_style_topology(const element::ptr& root, std::vector<style_topology_entry>& result)
        {
            if(!root) return;
            result.push_back({root.get(), root->css().get_display(), root->css().get_position(), root->tag(),
                              root->children().size(), layout_signature(root->css())});
            for(const auto& child : root->children())
            {
                collect_style_topology(child, result);
            }
        }

        bool is_out_of_flow_positioned(const element& el)
        {
            const auto position = el.css().get_position();
            return position == element_position_absolute || position == element_position_fixed;
        }

        bool is_safe_layout_containment(const element& el)
        {
            const auto& css = el.css();
            const auto& width = css.get_width();
            const auto& height = css.get_height();
            return css.has_layout_containment() && !css.has_unsupported_containment() &&
                   !width.is_predefined() && !height.is_predefined() &&
                   width.units() != css_units_percentage && height.units() != css_units_percentage;
        }
    } // namespace

    document::document(document_container* container)
    {
        m_container = container;
    }

    document::~document()
    {
        m_over_element = m_active_element = nullptr;
        if(m_container)
        {
            for(auto& font : m_fonts)
            {
                m_container->delete_font(font.second.font);
            }
        }
    }

    document::ptr document::createFromString(const estring& str, document_container* container,
                                             const std::string& master_styles, const std::string& user_styles)
    {
        // Create litehtml::document
        document::ptr doc = std::make_shared<document>(container);

        // Parse document into GumboOutput
        GumboOutput* output = doc->parse_html(str);

        // mode must be set before doc->create_node because it is used in html_tag::set_attr
        switch(output->document->v.document.doc_type_quirks_mode)
        {
        case GUMBO_DOCTYPE_NO_QUIRKS:
            doc->m_mode = no_quirks_mode;
            break;
        case GUMBO_DOCTYPE_QUIRKS:
            doc->m_mode = quirks_mode;
            break;
        case GUMBO_DOCTYPE_LIMITED_QUIRKS:
            doc->m_mode = limited_quirks_mode;
            break;
        }

        // Create litehtml::elements.
        elements_list root_elements;
        doc->create_node(output->root, root_elements, true, true);
        element::ptr root;
        if(!root_elements.empty())
        {
            root = root_elements.back();
        }

        // Destroy GumboOutput
        gumbo_destroy_output(&kGumboDefaultOptions, output);

        doc->finalize_from_external_root(root, master_styles, user_styles);

        return doc;
    }

    void document::set_document_mode(document_mode mode)
    {
        m_mode = mode;
    }

    void document::finalize_from_external_root(const std::shared_ptr<element>& root, const std::string& master_styles,
                                               const std::string& user_styles)
    {
        // Finalization accumulates state that is never rolled back: the parsed stylesheets, the media
        // lists, the tabular elements, the render tree, and the used selectors of every element. A
        // second run would duplicate all of it, so a document is finalized once, exactly like a parsed
        // one. Build a new document to rebuild.
        if(m_finalized)
        {
            return;
        }
        m_finalized = true;

        m_root = root;

        if(master_styles != "")
        {
            m_master_css.parse_css_stylesheet(master_styles, "", shared_from_this());
            m_master_css.sort_selectors();
        }
        if(user_styles != "")
        {
            m_user_css.parse_css_stylesheet(user_styles, "", shared_from_this());
            m_user_css.sort_selectors();
        }

        // Let's process created elements tree
        if(m_root)
        {
            container()->get_media_features(m_media);

            m_root->set_pseudo_class(_root_, true);

            // apply master CSS
            m_root->apply_stylesheet(m_master_css);

            // parse elements attributes
            m_root->parse_attributes();

            // parse style sheets linked in document
            for(const auto& css : m_css)
            {
                media_query_list_list::ptr media;
                if(css.media != "")
                {
                    auto mq_list = parse_media_query_list(css.media, shared_from_this());
                    media        = std::make_shared<media_query_list_list>();
                    media->add(mq_list);
                }
                m_styles.parse_css_stylesheet(css.text, css.baseurl, shared_from_this(), media);
            }
            // Sort css selectors using CSS rules.
            m_styles.sort_selectors();
            rebuild_selector_dependencies();

            // Apply media features.
            update_media_lists(m_media);

            // Apply parsed styles.
            m_root->apply_stylesheet(m_styles);

            // Apply user styles if any
            m_root->apply_stylesheet(m_user_css);

            // Initialize element::m_css
            m_root->compute_styles();

            // Create rendering tree
            m_root_render = m_root->create_render_item(nullptr);

            // Now the m_tabular_elements is filled with tabular elements.
            // We have to check the tabular elements for missing table elements
            // and create the anonymous boxes in visual table layout
            fix_tables_layout();

            // Finally initialize elements
            // init() returns pointer to the render_init element because it can change its type
            if(m_root_render)
            {
                m_root_render = m_root_render->init();
            }
        }
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#change-the-encoding
    encoding adjust_meta_encoding(encoding meta_encoding, encoding current_encoding)
    {
        // 1.
        if(current_encoding == encoding::utf_16le || current_encoding == encoding::utf_16be)
        {
            return current_encoding;
        }

        // 2.
        if(meta_encoding == encoding::utf_16le || meta_encoding == encoding::utf_16be)
        {
            return encoding::utf_8;
        }

        // 3.
        if(meta_encoding == encoding::x_user_defined)
        {
            return encoding::windows_1252;
        }

        // 4,5,6.
        return meta_encoding;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inhead:change-the-encoding
    encoding get_meta_encoding(GumboNode* root)
    {
        // find <head>
        GumboNode* head = nullptr;
        for(size_t i = 0; i < root->v.element.children.length; i++)
        {
            auto* node = static_cast<GumboNode*>(root->v.element.children.data[i]);
            if(node->type == GUMBO_NODE_ELEMENT && node->v.element.tag == GUMBO_TAG_HEAD)
            {
                head = node;
                break;
            }
        }
        if(!head)
        {
            return encoding::null;
        }

        // go through <meta> tags in <head>
        for(size_t i = 0; i < head->v.element.children.length; i++)
        {
            auto* node = static_cast<GumboNode*>(head->v.element.children.data[i]);
            if(node->type != GUMBO_NODE_ELEMENT || node->v.element.tag != GUMBO_TAG_META)
            {
                continue;
            }

            auto* charset    = gumbo_get_attribute(&node->v.element.attributes, "charset");
            auto* http_equiv = gumbo_get_attribute(&node->v.element.attributes, "http-equiv");
            auto* content    = gumbo_get_attribute(&node->v.element.attributes, "content");
            // 1. If the element has a charset attribute...
            if(charset)
            {
                auto encoding = get_encoding(charset->value);
                if(encoding != encoding::null)
                {
                    return encoding;
                }
            }
            // 2. Otherwise, if the element has an http-equiv attribute...
            else if(http_equiv && t_strcasecmp(http_equiv->value, "content-type") == 0 && content)
            {
                auto encoding = extract_encoding_from_meta_element(content->value);
                if(encoding != encoding::null)
                {
                    return encoding;
                }
            }
        }

        return encoding::null;
    }

    // substitute for gumbo_parse that handles encodings
    GumboOutput* document::parse_html(estring str)
    {
        // https://html.spec.whatwg.org/multipage/parsing.html#the-input-byte-stream
        encoding_sniffing_algorithm(str);
        // cannot store output in local variable because gumbo keeps pointers into it,
        // which will be accessed later in gumbo_tag_from_original_text
        if(str.encoding == encoding::utf_8)
        {
            m_text = str;
        } else
        {
            decode(str, str.encoding, m_text);
        }

        // Gumbo does not support callbacks on node creation, so we cannot change encoding while parsing.
        // Instead, we parse entire file and then handle <meta> tags.

        // Using gumbo_parse_with_options to pass string length (m_text may contain NUL chars).
        GumboOutput* output = gumbo_parse_with_options(&kGumboDefaultOptions, m_text.data(), m_text.size());

        if(str.confidence == confidence::certain)
        {
            return output;
        }

        // Otherwise: confidence is tentative.

        // If valid HTML encoding is specified in <meta> tag...
        encoding meta_encoding = get_meta_encoding(output->root);
        if(meta_encoding != encoding::null)
        {
            // ...and it is different from currently used encoding...
            encoding new_encoding = adjust_meta_encoding(meta_encoding, str.encoding);
            if(new_encoding != str.encoding)
            {
                // ...reparse with the new encoding.
                gumbo_destroy_output(&kGumboDefaultOptions, output);
                m_text.clear();

                if(new_encoding == encoding::utf_8)
                {
                    m_text = str;
                } else
                {
                    decode(str, new_encoding, m_text);
                }
                output = gumbo_parse_with_options(&kGumboDefaultOptions, m_text.data(), m_text.size());
            }
        }

        return output;
    }

    void document::create_node(void* gnode, elements_list& elements, bool parseTextNode, bool process_root)
    {
        auto* node = static_cast<GumboNode*>(gnode);
        switch(node->type)
        {
        case GUMBO_NODE_ELEMENT:
            {
                if(process_root)
                {
                    string_map      attrs;
                    GumboAttribute* attr = nullptr;
                    for(unsigned int i = 0; i < node->v.element.attributes.length; i++)
                    {
                        attr              = static_cast<GumboAttribute*>(node->v.element.attributes.data[i]);
                        attrs[attr->name] = attr->value;
                    }

                    element::ptr ret;
                    const char*  tag = gumbo_normalized_tagname(node->v.element.tag);
                    if(tag[0])
                    {
                        ret = create_element(tag, attrs);
                    } else
                    {
                        if(node->v.element.original_tag.data && node->v.element.original_tag.length)
                        {
                            std::string str;
                            gumbo_tag_from_original_text(&node->v.element.original_tag);
                            str.append(node->v.element.original_tag.data, node->v.element.original_tag.length);
                            ret = create_element(str.c_str(), attrs);
                        }
                    }
                    if(!strcmp(tag, "script"))
                    {
                        parseTextNode = false;
                    }
                    if(ret)
                    {
                        elements_list child;
                        for(unsigned int i = 0; i < node->v.element.children.length; i++)
                        {
                            child.clear();
                            create_node(static_cast<GumboNode*>(node->v.element.children.data[i]), child, parseTextNode,
                                        true);
                            std::for_each(child.begin(), child.end(),
                                          [&ret](element::ptr& el) { ret->appendChild(el); });
                        }
                        elements.push_back(ret);
                    }
                } else
                {
                    for(unsigned int i = 0; i < node->v.element.children.length; i++)
                    {
                        create_node(static_cast<GumboNode*>(node->v.element.children.data[i]), elements, parseTextNode,
                                    true);
                    }
                }
            }
            break;
        case GUMBO_NODE_TEXT:
            {
                if(!parseTextNode)
                {
                    elements.push_back(std::make_shared<el_text>(node->v.text.text, shared_from_this()));
                } else
                {
                    m_container->split_text(
                        node->v.text.text,
                        [this, &elements](const char* text) {
                            elements.push_back(std::make_shared<el_text>(text, shared_from_this()));
                        },
                        [this, &elements](const char* text) {
                            elements.push_back(std::make_shared<el_space>(text, shared_from_this()));
                        });
                }
            }
            break;
        case GUMBO_NODE_CDATA:
            {
                element::ptr ret = std::make_shared<el_cdata>(shared_from_this());
                ret->set_data(node->v.text.text);
                elements.push_back(ret);
            }
            break;
        case GUMBO_NODE_COMMENT:
            {
                element::ptr ret = std::make_shared<el_comment>(shared_from_this());
                ret->set_data(node->v.text.text);
                elements.push_back(ret);
            }
            break;
        case GUMBO_NODE_WHITESPACE:
            {
                std::string str = node->v.text.text;
                for(size_t i = 0; i < str.length(); i++)
                {
                    elements.push_back(std::make_shared<el_space>(str.substr(i, 1).c_str(), shared_from_this()));
                }
            }
            break;
        default:
            break;
        }
    }

    element::ptr document::create_element(const char* tag_name, const string_map& attributes)
    {
        element::ptr  newTag;
        document::ptr this_doc = shared_from_this();
        if(m_container)
        {
            newTag = m_container->create_element(tag_name, attributes, this_doc);
        }
        if(!newTag)
        {
            if(!strcmp(tag_name, "br"))
            {
                newTag = std::make_shared<el_break>(this_doc);
            } else if(!strcmp(tag_name, "p"))
            {
                newTag = std::make_shared<el_para>(this_doc);
            } else if(!strcmp(tag_name, "img"))
            {
                newTag = std::make_shared<el_image>(this_doc);
            } else if(!strcmp(tag_name, "table"))
            {
                newTag = std::make_shared<el_table>(this_doc);
            } else if(!strcmp(tag_name, "td") || !strcmp(tag_name, "th"))
            {
                newTag = std::make_shared<el_td>(this_doc);
            } else if(!strcmp(tag_name, "link"))
            {
                newTag = std::make_shared<el_link>(this_doc);
            } else if(!strcmp(tag_name, "title"))
            {
                newTag = std::make_shared<el_title>(this_doc);
            } else if(!strcmp(tag_name, "a"))
            {
                newTag = std::make_shared<el_anchor>(this_doc);
            } else if(!strcmp(tag_name, "tr"))
            {
                newTag = std::make_shared<el_tr>(this_doc);
            } else if(!strcmp(tag_name, "style"))
            {
                newTag = std::make_shared<el_style>(this_doc);
            } else if(!strcmp(tag_name, "base"))
            {
                newTag = std::make_shared<el_base>(this_doc);
            } else if(!strcmp(tag_name, "body"))
            {
                newTag = std::make_shared<el_body>(this_doc);
            } else if(!strcmp(tag_name, "div"))
            {
                newTag = std::make_shared<el_div>(this_doc);
            } else if(!strcmp(tag_name, "script"))
            {
                newTag = std::make_shared<el_script>(this_doc);
            } else if(!strcmp(tag_name, "font"))
            {
                newTag = std::make_shared<el_font>(this_doc);
            } else
            {
                newTag = std::make_shared<html_tag>(this_doc);
            }
        }

        if(newTag)
        {
            newTag->set_tagName(tag_name);
            for(const auto& attribute : attributes)
            {
                newTag->set_attr(attribute.first.c_str(), attribute.second.c_str());
            }
        }

        return newTag;
    }

    uint_ptr document::add_font(const font_description& descr, font_metrics* fm)
    {
        uint_ptr ret = 0;

        std::string key = descr.hash();

        if(m_fonts.find(key) == m_fonts.end())
        {
            font_item fi = {0, {}};

            fi.font      = m_container->create_font(descr, this, &fi.metrics);
            m_fonts[key] = fi;
            ret          = fi.font;
            if(fm)
            {
                *fm = fi.metrics;
            }
        }
        return ret;
    }

    uint_ptr document::get_font(const font_description& descr, font_metrics* fm)
    {
        if(descr.size == 0_px)
        {
            return 0;
        }

        auto key = descr.hash();

        auto el = m_fonts.find(key);

        if(el != m_fonts.end())
        {
            if(fm)
            {
                *fm = el->second.metrics;
            }
            return el->second.font;
        }
        return add_font(descr, fm);
    }

    pixel_t document::render(pixel_t max_width, render_type rt)
    {
        pixel_t ret = 0_px;
        prepare_scoped_styles();
        if(m_render_tree_dirty)
        {
            rebuild_render_tree();
        }
        if(m_root && m_root_render)
        {
            position viewport;
            m_container->get_viewport(viewport);
            containing_block_context cb_context;
            cb_context.width       = max_width;
            cb_context.width.type  = containing_block_context::cbc_value_type_absolute;
            cb_context.height      = viewport.height;
            cb_context.height.type = containing_block_context::cbc_value_type_absolute;

            if(rt == render_fixed_only)
            {
                m_fixed_boxes.clear();
                m_root_render->render_positioned(rt);
            } else
            {
                ret = m_root_render->render(0_px, 0_px, cb_context, nullptr).natural_width;
                if(m_root_render->fetch_positioned())
                {
                    m_fixed_boxes.clear();
                    m_root_render->render_positioned(rt);
                }
                m_size.width  = 0;
                m_size.height = 0;
                m_root_render->calc_document_size(m_size);
            }
        }
        return ret;
    }

    pixel_t document::render_dirty(const std::shared_ptr<element>& root, pixel_t max_width, render_type rt)
    {
        const auto pending_root = m_scoped_styles_dirty_root.lock();
        if(!root || root == m_root || root->get_document().get() != this || m_styles_dirty || m_render_tree_dirty ||
           !pending_root || pending_root != root)
        {
            return render(max_width, rt);
        }

        // Absolute/fixed subtrees are independent by construction. A normal-flow
        // subtree may also stop at a proven fixed-size layout containment box.
        const auto old_display  = root->css().get_display();
        const auto old_position = root->css().get_position();
        auto layout_root = root;
        bool containment_candidate = false;
        for(auto node = root; node; node = node->parent())
        {
            if(node->css().has_layout_containment() || node->css().has_unsupported_containment())
            {
                containment_candidate = true;
                if(is_safe_layout_containment(*node)) layout_root = node;
                break;
            }
        }
        const bool containment_hit = layout_root != root || is_safe_layout_containment(*root);
        if(!is_out_of_flow_positioned(*root) && !containment_hit)
        {
            if(containment_candidate) ++m_style_invalidation_stats.containment_fallback_count;
            return render(max_width, rt);
        }
        if(containment_hit) ++m_style_invalidation_stats.containment_hit_count;

        bool layout_changed = true;
        const bool topology_changed = rematch_styles(root, m_scoped_selector_match_required, &layout_changed);
        m_scoped_selector_match_required = false;
        m_scoped_styles_dirty_root.reset();
        auto item = layout_root->get_render_item();
        if(topology_changed || !item || (!is_out_of_flow_positioned(*root) && !containment_hit) || root->css().get_display() != old_display ||
           root->css().get_position() != old_position)
        {
            // A changed render-item kind or positioning mode requires rebuilding
            // the render tree before the normal document render.
            m_render_tree_dirty = true;
            return render(max_width, rt);
        }
        if(!layout_changed)
        {
            ++m_style_invalidation_stats.geometry_cache_hit_count;
            return 0_px;
        }
        ++m_style_invalidation_stats.geometry_cache_miss_count;
        std::vector<style_topology_entry> layout_nodes;
        collect_style_topology(layout_root, layout_nodes);
        m_style_invalidation_stats.layout_visited_elements += layout_nodes.size();

        position viewport;
        m_container->get_viewport(viewport);
        containing_block_context cb_context;
        cb_context.width = max_width;
        cb_context.width.type = containing_block_context::cbc_value_type_absolute;
        cb_context.height = viewport.height;
        cb_context.height.type = containing_block_context::cbc_value_type_absolute;
        const auto placement = item->pos();
        const auto result = item->render(placement.x - item->content_offset_left(),
                                         placement.y - item->content_offset_top(), cb_context, nullptr);
        if(m_root_render->fetch_positioned())
        {
            m_fixed_boxes.clear();
            m_root_render->render_positioned(rt);
        }
        m_size.width = 0;
        m_size.height = 0;
        m_root_render->calc_document_size(m_size);
        return result.natural_width;
    }

    void document::invalidate_styles()
    {
        // Attributes are also populated while parsing. Initial finalization
        // already computes those styles, so defer only later DOM mutations.
        if(m_finalized)
        {
            m_styles_dirty = true;
            m_render_tree_dirty = true;
            m_scoped_selector_match_required = false;
            m_scoped_styles_dirty_root.reset();
        }
    }

    void document::invalidate_styles(const std::shared_ptr<element>& root)
    {
        invalidate_attribute_styles(root, "style");
    }

    bool document::is_connected(const std::shared_ptr<element>& root) const
    {
        if(!root || root->get_document().get() != this) return false;
        auto top = root;
        while(top->parent())
        {
            top = top->parent();
        }
        return top == m_root;
    }

    void document::schedule_scoped_style_match(const std::shared_ptr<element>& root, bool match_selectors)
    {
        if(m_styles_dirty || !is_connected(root)) return;
        m_scoped_selector_match_required = m_scoped_selector_match_required || match_selectors;
        const auto pending = m_scoped_styles_dirty_root.lock();
        if(!pending)
        {
            m_scoped_styles_dirty_root = root;
            return;
        }
        if(pending == root) return;

        std::vector<element::ptr> pending_ancestors;
        for(auto node = pending; node; node = node->parent())
        {
            pending_ancestors.push_back(node);
        }
        for(auto node = root; node; node = node->parent())
        {
            if(std::find(pending_ancestors.begin(), pending_ancestors.end(), node) != pending_ancestors.end())
            {
                m_scoped_styles_dirty_root = node;
                return;
            }
        }
        invalidate_styles();
    }

    void document::invalidate_attribute_styles(const std::shared_ptr<element>& root, const char* attribute,
                                                const char* old_value, const char* new_value)
    {
        if(!m_finalized || !attribute || !is_connected(root)) return;

        const auto name = _id(lowcase(attribute));
        auto scope = style_match_scope::none;
        if(const auto found = m_attribute_dependencies.find(name); found != m_attribute_dependencies.end())
        {
            scope = found->second;
        }
        auto merge_scope = [&scope](style_match_scope dependency) {
            if(static_cast<int>(dependency) > static_cast<int>(scope)) scope = dependency;
        };
        if(name == _id("class"))
        {
            auto merge_classes = [&](const char* value) {
                if(!value) return;
                std::string normalized = value;
                if(mode() == quirks_mode) lcase(normalized);
                for(const auto& token : split_string(normalized, whitespace, "", ""))
                {
                    if(const auto found = m_class_dependencies.find(_id(token)); found != m_class_dependencies.end())
                        merge_scope(found->second);
                }
            };
            merge_classes(old_value);
            merge_classes(new_value);
        }
        if(name == _id("id"))
        {
            auto merge_id = [&](const char* value) {
                if(!value || !*value) return;
                std::string normalized = value;
                if(mode() == quirks_mode) lcase(normalized);
                if(const auto found = m_id_dependencies.find(_id(normalized)); found != m_id_dependencies.end())
                    merge_scope(found->second);
            };
            merge_id(old_value);
            merge_id(new_value);
        }
        if(scope == style_match_scope::full)
        {
            invalidate_styles();
            return;
        }
        // Even when no selector references the attribute, inline and legacy
        // presentational attributes can alter computed styles on this subtree.
        const bool simple_computed_style_refresh = scope == style_match_scope::none &&
                                                   (name == _style_ || name == _id("class") || name == _id("id"));
        schedule_scoped_style_match(root, !simple_computed_style_refresh);
    }

    void document::invalidate_structure_styles(const std::shared_ptr<element>& root)
    {
        if(!m_finalized || !is_connected(root)) return;
        m_render_tree_dirty = true;
        if(m_structure_requires_full_match)
        {
            invalidate_styles();
            return;
        }
        schedule_scoped_style_match(root, true);
    }

    void document::rebuild_selector_dependencies()
    {
        m_attribute_dependencies.clear();
        m_class_dependencies.clear();
        m_id_dependencies.clear();
        m_structure_requires_full_match = false;

        auto merge_scope = [](style_match_scope& current, style_match_scope incoming) {
            if(static_cast<int>(incoming) > static_cast<int>(current)) current = incoming;
        };
        auto record = [&](const css_attribute_selector& attr, style_match_scope scope) {
            if(attr.type == select_class)
            {
                merge_scope(m_class_dependencies[attr.name], scope);
            } else if(attr.type == select_id)
            {
                merge_scope(m_id_dependencies[attr.name], scope);
            } else if(attr.type == select_attr)
            {
                merge_scope(m_attribute_dependencies[attr.name], scope);
            }
        };

        std::function<void(const css_selector&, bool, bool)> scan_selector;
        scan_selector = [&](const css_selector& selector, bool sibling_to_subject, bool forced_full) {
            const bool selector_creates_content = selector_has_pseudo_element(selector);
            const auto scope = sibling_to_subject || forced_full || selector_creates_content
                                   ? style_match_scope::full
                                   : style_match_scope::subtree;
            for(const auto& attr : selector.m_right.m_attrs)
            {
                record(attr, scope);
                // Selector lists inside :is/:not/:nth-* can make another node's
                // match depend on the changed element. Keep this first version
                // conservative rather than trying to model each pseudo-class.
                for(const auto& nested : attr.selector_list)
                {
                    if(nested) scan_selector(*nested, false, true);
                }
            }
            if(selector.m_left)
            {
                const bool sibling = selector.m_combinator == combinator_adjacent_sibling ||
                                     selector.m_combinator == combinator_general_sibling;
                scan_selector(*selector.m_left, sibling_to_subject || sibling, forced_full || selector_creates_content);
            }
        };
        auto scan_stylesheet = [&](const css& stylesheet) {
            for(const auto& selector : stylesheet.selectors())
            {
                if(selector)
                {
                    // Scoped rematching intentionally preserves generated
                    // pseudo nodes. A structural mutation can make a relational
                    // or structural pseudo-element rule stop matching, which
                    // requires the full reset path to remove the old node.
                    if(selector_has_pseudo_element(*selector) &&
                       (selector->m_left || selector_has_structural_pseudo(*selector)))
                    {
                        m_structure_requires_full_match = true;
                    }
                    scan_selector(*selector, false, false);
                }
            }
        };
        scan_stylesheet(m_master_css);
        scan_stylesheet(m_styles);
        scan_stylesheet(m_user_css);
    }

    bool document::rematch_styles(const std::shared_ptr<element>& root, bool match_selectors, bool* layout_changed)
    {
        if(!root) return false;
        const auto started = std::chrono::steady_clock::now();
        std::vector<style_topology_entry> before;
        std::vector<style_topology_entry> after;
        collect_style_topology(root, before);

        if(match_selectors)
        {
            root->invalidate_selector_cache();
            root->reset_matched_styles();
            root->apply_stylesheet(m_master_css);
            root->parse_attributes();
            root->apply_stylesheet(m_styles);
            root->apply_stylesheet(m_user_css);
        } else
        {
            // style/class/id changes with no selector dependency cannot alter
            // which rules match. Reuse the element's previously relevant rule
            // set and only rebuild declarations/computed values.
            root->refresh_styles();
        }
        root->compute_styles();

        collect_style_topology(root, after);
        bool topology_changed = before.size() != after.size();
        bool geometry_changed = topology_changed;
        for(size_t i = 0; !topology_changed && i < before.size(); ++i)
        {
            if(before[i].node != after[i].node || before[i].display != after[i].display ||
               before[i].position != after[i].position || before[i].tag != after[i].tag ||
               before[i].child_count != after[i].child_count)
            {
                topology_changed = true;
            }
            if(before[i].layout_signature != after[i].layout_signature) geometry_changed = true;
        }
        if(layout_changed) *layout_changed = geometry_changed;
        const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                        std::chrono::steady_clock::now() - started)
                                                        .count());
        if(match_selectors)
        {
            ++m_style_invalidation_stats.subtree_match_count;
            m_style_invalidation_stats.subtree_match_elements += before.size();
            m_style_invalidation_stats.subtree_match_ns += elapsed;
        } else
        {
            ++m_style_invalidation_stats.computed_refresh_count;
            m_style_invalidation_stats.computed_refresh_elements += before.size();
            m_style_invalidation_stats.computed_refresh_ns += elapsed;
        }
        if(topology_changed) ++m_style_invalidation_stats.render_tree_fallback_count;
        return topology_changed;
    }

    void document::rebuild_all_styles()
    {
        m_styles_dirty = false;
        m_scoped_selector_match_required = false;
        m_scoped_styles_dirty_root.reset();
        if(!m_root) return;
        const auto started = std::chrono::steady_clock::now();
        std::vector<style_topology_entry> elements;
        collect_style_topology(m_root, elements);

        // Rebuild selector state from the complete stylesheets. refresh_styles()
        // only revisits selectors that matched at least partially before the
        // mutation and cannot activate a newly matching sibling/class rule.
        m_root->reset_styles();
        m_root->apply_stylesheet(m_master_css);
        m_root->parse_attributes();
        m_root->apply_stylesheet(m_styles);
        m_root->apply_stylesheet(m_user_css);
        m_root->compute_styles();
        ++m_style_invalidation_stats.full_match_count;
        m_style_invalidation_stats.full_match_elements += elements.size();
        m_style_invalidation_stats.full_match_ns +=
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - started)
                                      .count());
        m_render_tree_dirty = true;
    }

    const style_invalidation_stats& document::style_stats() const
    {
        return m_style_invalidation_stats;
    }

    void document::reset_style_stats()
    {
        m_style_invalidation_stats = {};
    }

    css::selector_index_diagnostics document::selector_index_stats() const
    {
        css::selector_index_diagnostics out;
        auto merge = [&out](const css::selector_index_diagnostics& value) {
            out.build_ns += value.build_ns;
            out.query_count += value.query_count;
            out.total_rules_considered += value.total_rules_considered;
            out.candidate_rules += value.candidate_rules;
            out.bytes += value.bytes;
            out.enabled = out.enabled || value.enabled;
        };
        merge(m_master_css.index_diagnostics());
        merge(m_styles.index_diagnostics());
        merge(m_user_css.index_diagnostics());
        return out;
    }

    void document::record_selector_cache(bool hit, bool bypass, bool stale, bool evicted, uint64_t validation_ns, size_t entries)
    {
        if(hit) ++m_selector_cache_stats.hit_count;
        else if(bypass) ++m_selector_cache_stats.bypass_count;
        else ++m_selector_cache_stats.miss_count;
        if(stale) ++m_selector_cache_stats.stale_count;
        if(evicted) ++m_selector_cache_stats.evict_count;
        m_selector_cache_stats.validation_ns += validation_ns;
        m_selector_cache_stats.peak_entries = std::max<uint64_t>(m_selector_cache_stats.peak_entries, entries);
    }

    void document::benchmark_refresh_selector_matches()
    {
        if(!m_root) return;
        m_root->reset_matched_styles();
        m_root->apply_stylesheet(m_master_css);
        m_root->parse_attributes();
        m_root->apply_stylesheet(m_styles);
        m_root->apply_stylesheet(m_user_css);
        m_root->compute_styles();
    }

    void document::prepare_scoped_styles()
    {
        if(m_styles_dirty)
        {
            rebuild_all_styles();
            return;
        }
        const auto root = m_scoped_styles_dirty_root.lock();
        const bool match_selectors = m_scoped_selector_match_required;
        m_scoped_selector_match_required = false;
        m_scoped_styles_dirty_root.reset();
        if(root && rematch_styles(root, match_selectors))
        {
            m_render_tree_dirty = true;
        }
    }

    void document::rebuild_render_tree()
    {
        m_render_tree_dirty = false;
        if(!m_root) return;

        // display can change, so the old tree may lack newly visible items.
        m_tabular_elements.clear();
        m_root_render = m_root->create_render_item(nullptr);
        fix_tables_layout();
        if(m_root_render)
        {
            m_root_render = m_root_render->init();
        }
    }

    void document::draw(uint_ptr hdc, pixel_t x, pixel_t y, const position* clip)
    {
        if(m_root && m_root_render)
        {
            m_root->draw(hdc, x, y, clip, m_root_render);
            m_root_render->draw_stacking_context(hdc, x, y, clip, true);
        }
    }

    pixel_t document::to_pixels(const css_length& val, const font_metrics& metrics, pixel_t size) const
    {
        if(val.is_predefined())
        {
            return 0_px;
        }
        pixel_t ret;
        switch(val.units())
        {
        case css_units_percentage:
            ret = val.calc_percent(size);
            break;
        case css_units_em:
            ret = metrics.font_size * val.val();
            break;

        // https://drafts.csswg.org/css-values-4/#absolute-lengths
        case css_units_pt:
            ret = m_container->pt_to_px(val.val());
            break;
        case css_units_in:
            ret = m_container->pt_to_px(val.val() * 72); // 1in = 72pt
            break;
        case css_units_pc:
            ret = m_container->pt_to_px(val.val() * 12); // 1pc = (1/6)in = 12pt
            break;
        case css_units_cm:
            ret = m_container->pt_to_px(val.val() * 72 / 2.54f); // 1cm = (1/2.54)in = (72/2.54)pt
            break;
        case css_units_mm:
            ret = m_container->pt_to_px(val.val() * 72 / 2.54f / 10);
            break;

        case css_units_vw:
            ret = pixel_t(m_media.width.value() * val.val() / 100.0f);
            break;
        case css_units_vh:
            ret = pixel_t(m_media.height.value() * val.val() / 100.0f);
            break;
        case css_units_vmin:
            ret = pixel_t(std::min(m_media.height.value(), m_media.width.value()) * val.val() / 100.0f);
            break;
        case css_units_vmax:
            ret = pixel_t(std::max(m_media.height.value(), m_media.width.value()) * val.val() / 100.0f);
            break;
        case css_units_rem:
            ret = pixel_t(m_root->css().get_font_size().value() * val.val());
            break;
        case css_units_ex:
            ret = pixel_t(metrics.x_height.value() * val.val());
            break;
        case css_units_ch:
            ret = pixel_t(metrics.ch_width.value() * val.val());
            break;
        default:
            ret = pixel_t(val.val());
            break;
        }
        return ret;
    }

    void document::cvt_units(css_length& val, const font_metrics& metrics, pixel_t size) const
    {
        if(val.is_predefined())
        {
            return;
        }
        if(val.units() != css_units_percentage)
        {
            val.set_value(static_cast<float>(to_pixels(val, metrics, size)), css_units_px);
        }
    }

    pixel_t document::width() const
    {
        return m_size.width;
    }

    pixel_t document::height() const
    {
        return m_size.height;
    }

    void document::add_stylesheet(const char* str, const char* baseurl, const char* media)
    {
        if(str && str[0])
        {
            m_css.emplace_back(str, baseurl, media);
        }
    }

    bool document::on_mouse_over(pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y,
                                 const std::function<void(const position&)>& redraw_box)
    {
        if(!m_root || !m_root_render)
        {
            return false;
        }

        element::ptr over_el = m_root_render->get_element_by_point(x, y, client_x, client_y, nullptr);

        bool state_was_changed = false;

        if(over_el != m_over_element)
        {
            if(m_over_element)
            {
                if(m_over_element->on_mouse_leave())
                {
                    m_container->on_mouse_event(m_over_element, mouse_event_leave);
                    state_was_changed = true;
                }
            }
            m_over_element = over_el;
        }

        std::string cursor;

        if(m_over_element)
        {
            if(m_over_element->on_mouse_over())
            {
                state_was_changed = true;
            }
            cursor = m_over_element->css().get_cursor();
        }

        m_container->set_cursor(cursor.c_str());

        if(state_was_changed)
        {
            m_container->on_mouse_event(m_over_element, mouse_event_enter);
            return m_root->find_styles_changes(redraw_box);
        }
        return false;
    }

    std::vector<scroll_values> document::on_scroll(pixel_t dx, pixel_t dy, pixel_t x, pixel_t y, pixel_t client_x,
                                                   pixel_t client_y) const
    {
        if(dy == 0_px && dx == 0_px)
        {
            return {};
        }

        element::ptr vscroll_el;
        element::ptr hscroll_el;

        if(dy != 0_px)
        {
            vscroll_el = m_root_render->get_element_by_point(
                x, y, client_x, client_y,
                [dy](const std::shared_ptr<render_item>& el) -> bool { return el->is_v_scrollable(dy); });
        }

        if(dx != 0_px)
        {
            hscroll_el = m_root_render->get_element_by_point(
                x, y, client_x, client_y,
                [dx](const std::shared_ptr<render_item>& el) -> bool { return el->is_h_scrollable(dx); });
        }

        if(!vscroll_el && !hscroll_el)
        {
            return {};
        }

        if(vscroll_el == hscroll_el)
        {
            scroll_values sv;
            sv.dx         = hscroll_el->h_scroll(dx);
            sv.dy         = vscroll_el->v_scroll(dy);
            sv.scroll_box = hscroll_el->get_placement();
            return {sv};
        }

        std::vector<scroll_values> ret;
        ret.reserve(2);

        if(vscroll_el)
        {
            scroll_values sv;
            sv.dy         = vscroll_el->v_scroll(dy);
            sv.scroll_box = vscroll_el->get_placement();
            ret.push_back(sv);
        }

        if(hscroll_el)
        {
            scroll_values sv;
            sv.dx         = hscroll_el->h_scroll(dx);
            sv.scroll_box = hscroll_el->get_placement();
            ret.push_back(sv);
        }

        return ret;
    }

    bool document::on_mouse_leave(const std::function<void(const position&)>& redraw_box)
    {
        if(!m_root || !m_root_render)
        {
            return false;
        }
        if(m_over_element)
        {
            auto el        = m_over_element;
            m_over_element = nullptr;
            if(el->on_mouse_leave())
            {
                m_container->on_mouse_event(el, mouse_event_leave);
                return m_root->find_styles_changes(redraw_box);
            }
        }
        return false;
    }

    bool document::on_lbutton_down(pixel_t x, pixel_t y, pixel_t client_x, pixel_t client_y,
                                   const std::function<void(const position&)>& redraw_box)
    {
        if(!m_root || !m_root_render)
        {
            return false;
        }

        element::ptr over_el = m_root_render->get_element_by_point(x, y, client_x, client_y, nullptr);
        m_active_element     = over_el;

        bool state_was_changed = false;

        if(over_el != m_over_element)
        {
            if(m_over_element)
            {
                if(m_over_element->on_mouse_leave())
                {
                    m_container->on_mouse_event(m_over_element, mouse_event_leave);
                    state_was_changed = true;
                }
            }
            m_over_element = over_el;
            if(m_over_element)
            {
                if(m_over_element->on_mouse_over())
                {
                    state_was_changed = true;
                }
            }
        }

        std::string cursor;

        if(m_over_element)
        {
            if(m_over_element->on_lbutton_down())
            {
                state_was_changed = true;
            }
            cursor = m_over_element->css().get_cursor();
        }

        m_container->set_cursor(cursor.c_str());

        if(state_was_changed)
        {
            m_container->on_mouse_event(m_over_element, mouse_event_enter);
            return m_root->find_styles_changes(redraw_box);
        }

        return false;
    }

    bool document::on_lbutton_up(pixel_t /*x*/, pixel_t /*y*/, pixel_t /*client_x*/, pixel_t /*client_y*/,
                                 const std::function<void(const position&)>& redraw_box, bool activate_default)
    {
        if(!m_root || !m_root_render)
        {
            return false;
        }
        // :active belongs to the element that received mouse-down, not to the
        // element currently under the physical pointer. This distinction is
        // essential when a captured drag crosses another widget: CSS :hover
        // follows the pointer, while the pressed element remains active until
        // release/cancel. Only a release over that same element activates its
        // native default action.
        const auto active_element = m_active_element;
        const bool is_click = activate_default && active_element && active_element == m_over_element;
        m_active_element = nullptr;
        if(active_element && active_element->on_lbutton_up(is_click))
        {
            return m_root->find_styles_changes(redraw_box);
        }
        return false;
    }

    bool document::on_button_cancel(const std::function<void(const position&)>& redraw_box)
    {
        m_active_element = nullptr;
        return on_mouse_leave(redraw_box);
    }

    void document::get_fixed_boxes(position::vector& fixed_boxes)
    {
        fixed_boxes = m_fixed_boxes;
    }

    void document::add_fixed_box(const position& pos)
    {
        m_fixed_boxes.push_back(pos);
    }

    bool document::media_changed()
    {
        container()->get_media_features(m_media);
        if(update_media_lists(m_media))
        {
            m_root->refresh_styles();
            m_root->compute_styles();
            // The set of rendered elements can change across a media breakpoint
            // (e.g. display:none <-> block on responsive nav/hero blocks). The render
            // tree is built once in createFromString() from the computed display values,
            // so rebuild it here to add/remove render items for elements whose display
            // just changed; otherwise a newly-shown element keeps no render item and
            // never lays out (it collapses to zero).
            m_root_render = m_root->create_render_item(nullptr);
            if(m_root_render)
            {
                m_root_render = m_root_render->init();
            }
            return true;
        }
        return false;
    }

    bool document::lang_changed()
    {
        if(!m_media_lists.empty())
        {
            std::string culture;
            container()->get_language(m_lang, culture);
            if(!culture.empty())
            {
                m_culture = m_lang + '-' + culture;
            } else
            {
                m_culture.clear();
            }
            m_root->refresh_styles();
            m_root->compute_styles();
            return true;
        }
        return false;
    }

    // Apply media features (determine which selectors are active).
    bool document::update_media_lists(const media_features& features)
    {
        bool update_styles = false;
        for(auto& media_list : m_media_lists)
        {
            if(media_list->apply_media_features(features))
            {
                update_styles = true;
            }
        }
        return update_styles;
    }

    void document::add_media_list(const media_query_list_list::ptr& list)
    {
        if(list && !contains(m_media_lists, list))
        {
            m_media_lists.push_back(list);
        }
    }

    void document::fix_tables_layout()
    {
        for(const auto& el_ptr : m_tabular_elements)
        {
            switch(el_ptr->src_el()->css().get_display())
            {
            case display_inline_table:
            case display_table:
                fix_table_children(el_ptr, display_table_row_group, "table-row-group");
                break;
            case display_table_footer_group:
            case display_table_row_group:
            case display_table_header_group:
                {
                    auto parent = el_ptr->parent();
                    if(parent)
                    {
                        if(parent->src_el()->css().get_display() != display_inline_table)
                        {
                            fix_table_parent(el_ptr, display_table, "table");
                        }
                    }
                    fix_table_children(el_ptr, display_table_row, "table-row");
                }
                break;
            case display_table_row:
                fix_table_parent(el_ptr, display_table_row_group, "table-row-group");
                fix_table_children(el_ptr, display_table_cell, "table-cell");
                break;
            case display_table_cell:
                fix_table_parent(el_ptr, display_table_row, "table-row");
                break;
            // TODO: make table layout fix for table-caption, table-column etc. elements
            case display_table_caption:
            case display_table_column:
            case display_table_column_group:
            default:
                break;
            }
        }
    }

    void document::fix_table_children(const std::shared_ptr<render_item>& el_ptr, style_display disp,
                                      const char* disp_str)
    {
        std::list<std::shared_ptr<render_item>> tmp;
        auto                                    first_iter = el_ptr->children().begin();
        auto                                    cur_iter   = el_ptr->children().begin();

        auto flush_elements = [&]() {
            element::ptr annon_tag = std::make_shared<html_tag>(el_ptr->src_el(), std::string("display:") + disp_str);
            std::shared_ptr<render_item> annon_ri;
            if(annon_tag->css().get_display() == display_table_cell)
            {
                annon_tag->set_tagName("table_cell");
                annon_ri = std::make_shared<render_item_block>(annon_tag);
            } else if(annon_tag->css().get_display() == display_table_row)
            {
                annon_ri = std::make_shared<render_item_table_row>(annon_tag);
            } else
            {
                annon_ri = std::make_shared<render_item_table_part>(annon_tag);
            }
            for(const auto& el : tmp)
            {
                annon_ri->add_child(el);
            }
            // add annon item as tabular for future processing
            add_tabular(annon_ri);
            annon_ri->parent(el_ptr);
            first_iter = el_ptr->children().insert(first_iter, annon_ri);
            cur_iter   = std::next(first_iter);
            while(cur_iter != el_ptr->children().end() && (*cur_iter)->parent() != el_ptr)
            {
                cur_iter = el_ptr->children().erase(cur_iter);
            }
            first_iter = cur_iter;
            tmp.clear();
        };

        while(cur_iter != el_ptr->children().end())
        {
            if((*cur_iter)->src_el()->css().get_display() != disp)
            {
                if(!(*cur_iter)->src_el()->is_table_skip() || ((*cur_iter)->src_el()->is_table_skip() && !tmp.empty()))
                {
                    if(disp != display_table_row_group ||
                       (*cur_iter)->src_el()->css().get_display() != display_table_caption)
                    {
                        if(tmp.empty())
                        {
                            first_iter = cur_iter;
                        }
                        tmp.push_back((*cur_iter));
                    }
                }
                cur_iter++;
            } else if(!tmp.empty())
            {
                flush_elements();
            } else
            {
                cur_iter++;
            }
        }
        if(!tmp.empty())
        {
            flush_elements();
        }
    }

    void document::fix_table_parent(const std::shared_ptr<render_item>& el_ptr, style_display disp,
                                    const char* disp_str)
    {
        auto parent = el_ptr->parent();

        if(parent->src_el()->css().get_display() != disp)
        {
            auto this_element = std::find_if(parent->children().begin(), parent->children().end(),
                                             [&](const std::shared_ptr<render_item>& el) { return el == el_ptr; });
            if(this_element != parent->children().end())
            {
                style_display el_disp = el_ptr->src_el()->css().get_display();
                auto          first   = this_element;
                auto          last    = this_element;
                auto          cur     = this_element;

                // find first element with same display
                while(true)
                {
                    if(cur == parent->children().begin())
                    {
                        break;
                    }
                    cur--;
                    if((*cur)->src_el()->is_table_skip() || (*cur)->src_el()->css().get_display() == el_disp)
                    {
                        first = cur;
                    } else
                    {
                        break;
                    }
                }

                // find last element with same display
                cur = this_element;
                while(true)
                {
                    cur++;
                    if(cur == parent->children().end())
                    {
                        break;
                    }

                    if((*cur)->src_el()->is_table_skip() || (*cur)->src_el()->css().get_display() == el_disp)
                    {
                        last = cur;
                    } else
                    {
                        break;
                    }
                }

                // extract elements with the same display and wrap them with anonymous object
                element::ptr annon_tag =
                    std::make_shared<html_tag>(parent->src_el(), std::string("display:") + disp_str);
                std::shared_ptr<render_item> annon_ri;
                if(annon_tag->css().get_display() == display_table ||
                   annon_tag->css().get_display() == display_inline_table)
                {
                    annon_ri = std::make_shared<render_item_table>(annon_tag);
                } else if(annon_tag->css().get_display() == display_table_row)
                {
                    annon_ri = std::make_shared<render_item_table_row>(annon_tag);
                } else
                {
                    annon_ri = std::make_shared<render_item_table_part>(annon_tag);
                }
                std::for_each(first, std::next(last, 1),
                              [&annon_ri](std::shared_ptr<render_item>& el) { annon_ri->add_child(el); });
                first = parent->children().erase(first, std::next(last));
                parent->children().insert(first, annon_ri);
                add_tabular(annon_ri);
                annon_ri->parent(parent);
            }
        }
    }

    void document::append_children_from_string(element& parent, const char* str, bool replace_existing)
    {
        // parent must belong to this document
        if(parent.get_document().get() != this)
        {
            return;
        }

        GumboOptions opts = kGumboDefaultOptions;
        // This is require to prevent creating html, head, body tags around the fragment
        // Although Gumbo always creates html tag anyway. We have to ignore it in create_node.
        opts.fragment_context = GUMBO_TAG_BODY;
        // parse document into GumboOutput
        GumboOutput* output = gumbo_parse_with_options(&opts, str, strlen(str));

        // Create litehtml::elements.
        elements_list child_elements;
        // Create elements excluding the root node
        create_node(output->root, child_elements, true, false);

        // Destroy GumboOutput
        gumbo_destroy_output(&kGumboDefaultOptions, output);

        if(replace_existing)
        {
            parent.clearRecursive();
        }

        // Attach the parsed DOM only. The mutation invalidation path below
        // performs one scoped style match and one render-tree rebuild, avoiding
        // eager work that would immediately be discarded.
        for(const auto& child : child_elements)
        {
            parent.appendChild(child);
        }
    }

    bool document::set_inner_html(const element::ptr& parent, const char* str)
    {
        if(!parent || !str || parent->get_document().get() != this)
        {
            return false;
        }

        append_children_from_string(*parent, str, true);
        invalidate_structure_styles(parent);
        return true;
    }

    bool document::append_child(const element::ptr& parent, const element::ptr& child)
    {
        if(!parent || !child || parent->get_document().get() != this || child->get_document().get() != this ||
           !std::dynamic_pointer_cast<html_tag>(parent))
        {
            return false;
        }

        // A node cannot become its own descendant.
        for(auto ancestor = parent; ancestor; ancestor = ancestor->parent())
        {
            if(ancestor == child)
            {
                return false;
            }
        }

        const auto old_parent = child->parent();
        if(old_parent && !old_parent->removeChild(child))
        {
            return false;
        }
        if(!parent->appendChild(child))
        {
            if(old_parent)
            {
                old_parent->appendChild(child);
            }
            return false;
        }

        if(old_parent && old_parent != parent)
        {
            invalidate_structure_styles(old_parent);
        }
        invalidate_structure_styles(parent);
        return true;
    }

    bool document::remove_child(const element::ptr& parent, const element::ptr& child)
    {
        if(!parent || !child || parent->get_document().get() != this || child->get_document().get() != this ||
           child->parent() != parent || !parent->removeChild(child))
        {
            return false;
        }

        invalidate_structure_styles(parent);
        return true;
    }

    bool document::replace_child(const element::ptr& parent, const element::ptr& replacement,
                                 const element::ptr& child)
    {
        if(!parent || !replacement || !child || parent->get_document().get() != this ||
           replacement->get_document().get() != this || child->get_document().get() != this ||
           child->parent() != parent)
        {
            return false;
        }
        if(replacement == child)
        {
            return true;
        }

        // Replacing a child with one of the parent's ancestors would create a
        // cycle that neither the DOM tree nor render tree can represent.
        for(auto ancestor = parent; ancestor; ancestor = ancestor->parent())
        {
            if(ancestor == replacement)
            {
                return false;
            }
        }

        auto tag = std::dynamic_pointer_cast<html_tag>(parent);
        if(!tag)
        {
            return false;
        }
        const auto old_parent = replacement->parent();
        if(old_parent && !old_parent->removeChild(replacement))
        {
            return false;
        }
        auto& children = tag->children();
        const auto found = std::find(children.begin(), children.end(), child);
        if(found == children.end())
        {
            return false;
        }
        child->parent(nullptr);
        replacement->parent(parent);
        *found = replacement;

        if(old_parent && old_parent != parent)
        {
            invalidate_structure_styles(old_parent);
        }
        invalidate_structure_styles(parent);
        return true;
    }

    void document::dump(dumper& cout)
    {
        if(m_root_render)
        {
            m_root_render->dump(cout);
        }
    }

} // namespace litehtml
