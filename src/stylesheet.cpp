#include "html.h"
#include "stylesheet.h"
#include <chrono>
#include <atomic>

namespace
{
    std::atomic<bool> g_selector_index_enabled{true};
}
#include "css_parser.h"
#include "document.h"
#include "document_container.h"

namespace litehtml
{

    // ( <declaration> )  https://drafts.csswg.org/css-conditional-3/#typedef-supports-decl
    static bool eval_supports_declaration(const css_token_vector& tokens, document_container* container)
    {
        // a <supports-decl> holds exactly one declaration
        for(const auto& token : tokens)
        {
            if(token.ch == ';')
            {
                return false;
            }
        }
        // the declaration is supported if litehtml was able to parse it
        style st;
        st.add(tokens, "", container);
        return !st.empty();
    }

    // These return false when the syntax is invalid, which is not the same as a condition that is
    // false: an @supports rule with an invalid condition is invalid and its block is never applied.
    static bool parse_supports_condition(const css_token_vector& tokens, int& index, bool& result,
                                         document_container* container);

    // <supports-in-parens> = ( <supports-condition> ) | <supports-feature> | <general-enclosed>
    static bool parse_supports_in_parens(const css_token& token, bool& result, document_container* container)
    {
        // <general-enclosed> = <function-token> <any-value>? )
        // Any such function is unsupported, so it evaluates to false.
        if(token.type == CV_FUNCTION)
        {
            result = false;
            return true;
        }
        // everything but a block is not a <supports-in-parens> at all
        if(token.type != ROUND_BLOCK)
        {
            return false;
        }

        // ( <supports-condition> )
        int  index = 0;
        bool value = false;
        if(parse_supports_condition(token.value, index, value, container))
        {
            skip_whitespace(token.value, index);
            if(index == static_cast<int>(token.value.size()))
            {
                result = value;
                return true;
            }
        }
        // ( <declaration> ), or <general-enclosed> = ( <any-value>? ) which evaluates to false
        result = eval_supports_declaration(token.value, container);
        return true;
    }

    // https://drafts.csswg.org/css-conditional-3/#typedef-supports-condition
    // <supports-condition> = not <supports-in-parens>
    //                      | <supports-in-parens> [ and <supports-in-parens> ]*
    //                      | <supports-in-parens> [ or <supports-in-parens> ]*
    static bool parse_supports_condition(const css_token_vector& tokens, int& index, bool& result,
                                         document_container* container)
    {
        auto parse_operand = [&](bool& value) {
            skip_whitespace(tokens, index);
            if(!parse_supports_in_parens(at(tokens, index), value, container))
            {
                return false;
            }
            index++;
            return true;
        };
        auto next_ident = [&]() {
            skip_whitespace(tokens, index);
            return lowcase(at(tokens, index).ident());
        };

        if(next_ident() == "not")
        {
            index++;
            if(!parse_operand(result))
            {
                return false;
            }
            result = !result;
            return true;
        }

        if(!parse_operand(result))
        {
            return false;
        }

        std::string op = next_ident();
        if(op != "and" && op != "or")
        {
            return true; // a single <supports-in-parens>, the caller checks what follows it
        }
        // mixing `and` and `or` without parentheses is invalid, so the operator cannot change
        while(next_ident() == op)
        {
            index++;
            bool operand = false;
            if(!parse_operand(operand))
            {
                return false;
            }
            result = op == "and" ? result && operand : result || operand;
        }
        return true;
    }

    static bool eval_supports_condition(const css_token_vector& tokens, document_container* container)
    {
        int  index  = 0;
        bool result = false;
        if(!parse_supports_condition(tokens, index, result, container))
        {
            return false;
        }
        skip_whitespace(tokens, index);
        return index == static_cast<int>(tokens.size()) && result;
    }

    // https://www.w3.org/TR/css-syntax-3/#parse-a-css-stylesheet
    template <class Input> // Input == string or css_token_vector
    void css::parse_css_stylesheet(const Input& input, const std::string& baseurl, const std::shared_ptr<document>& doc,
                                   const media_query_list_list::ptr& media, bool top_level)
    {
        if(doc && media)
        {
            doc->add_media_list(media);
        }

        // To parse a CSS stylesheet, first parse a stylesheet.
        auto rules          = css_parser::parse_stylesheet(input, top_level);
        bool import_allowed = top_level;

        // Interpret all of the resulting top-level qualified rules as style rules, defined below.
        // If any style rule is invalid, or any at-rule is not recognized or is invalid according
        // to its grammar or context, it's a parse error. Discard that rule.
        for(const auto& rule : rules)
        {
            if(rule->type == raw_rule::qualified)
            {
                if(parse_style_rule(rule, baseurl, doc, media))
                {
                    import_allowed = false;
                }
                continue;
            }

            // Otherwise: at-rule
            switch(_id(lowcase(rule->name)))
            {
            case _charset_: // ignored  https://www.w3.org/TR/css-syntax-3/#charset-rule
                break;

            case _import_:
                if(import_allowed)
                {
                    parse_import_rule(rule, baseurl, doc, media);
                } else
                {
                    css_parse_error("incorrect placement of @import rule");
                }
                break;

            // https://www.w3.org/TR/css-conditional-3/#at-media
            // @media <media-query-list> { <stylesheet> }
            case _media_:
                {
                    if(rule->block.type != CURLY_BLOCK)
                    {
                        break;
                    }
                    auto new_media = media;
                    auto mq_list   = parse_media_query_list(rule->prelude, doc);
                    // An empty media query list evaluates to true.
                    // https://drafts.csswg.org/mediaqueries-5/#example-6f06ee45
                    if(!mq_list.empty())
                    {
                        new_media = std::make_shared<media_query_list_list>(media ? *media : media_query_list_list());
                        new_media->add(mq_list);
                    }
                    parse_css_stylesheet(rule->block.value, baseurl, doc, new_media, false);
                    import_allowed = false;
                    break;
                }

            // https://drafts.csswg.org/css-conditional-3/#at-supports
            // @supports <supports-condition> { <stylesheet> }
            case _supports_:
                {
                    if(rule->block.type != CURLY_BLOCK)
                    {
                        break;
                    }
                    auto condition = normalize(rule->prelude, f_componentize);
                    if(eval_supports_condition(condition, doc->container()))
                    {
                        parse_css_stylesheet(rule->block.value, baseurl, doc, media, false);
                    }
                    import_allowed = false;
                    break;
                }

            // https://drafts.csswg.org/css-cascade-5/#at-layer
            // @layer <layer-name># ;
            // @layer <layer-name>? { <stylesheet> }
            case _layer_:
                {
                    // Cascade layers are not implemented: a layer statement only declares layer order, so it
                    // is ignored, and the rules of a layer block are used as if they were not layered.
                    if(rule->block.type == CURLY_BLOCK)
                    {
                        parse_css_stylesheet(rule->block.value, baseurl, doc, media, false);
                        import_allowed = false;
                    }
                    // a layer statement rule is allowed before @import and does not disallow it
                    break;
                }

            default:
                css_parse_error("unrecognized rule @" + rule->name);
            }
        }
    }

    // https://drafts.csswg.org/css-cascade-5/#at-import
    // `layer` and `supports` are not supported
    // @import [ <url> | <string> ] <media-query-list>?
    void css::parse_import_rule(const raw_rule::ptr& rule, const std::string& baseurl,
                                const std::shared_ptr<document>& doc, const media_query_list_list::ptr& media)
    {
        auto tokens = rule->prelude;
        int  index  = 0;
        skip_whitespace(tokens, index);
        auto        tok = at(tokens, index);
        std::string url;
        auto        parse_string = [](const css_token& tok, std::string& str) {
            if(tok.type != STRING)
            {
                return false;
            }
            str = tok.str();
            return true;
        };
        bool ok = parse_url(tok, url) || parse_string(tok, url);
        if(!ok)
        {
            css_parse_error("invalid @import rule");
            return;
        }
        document_container* container = doc->container();
        std::string         css_text;
        std::string         css_baseurl = baseurl;
        container->import_css(css_text, url, css_baseurl);

        auto new_media = media;
        tokens         = slice(tokens, index + 1);
        auto mq_list   = parse_media_query_list(tokens, doc);
        if(!mq_list.empty())
        {
            new_media = std::make_shared<media_query_list_list>(media ? *media : media_query_list_list());
            new_media->add(mq_list);
        }

        parse_css_stylesheet(css_text, css_baseurl, doc, new_media, true);
    }

    // https://www.w3.org/TR/css-syntax-3/#style-rules
    bool css::parse_style_rule(const raw_rule::ptr& rule, const std::string& baseurl,
                               const std::shared_ptr<document>& doc, const media_query_list_list::ptr& media)
    {
        // The prelude of the qualified rule is parsed as a <selector-list>. If this returns failure, the entire style
        // rule is invalid.
        auto list = parse_selector_list(rule->prelude, strict_mode, doc->mode());
        if(list.empty())
        {
            css_parse_error("invalid selector");
            return false;
        }

        style::ptr style = std::make_shared<litehtml::style>(); // style block
        // The content of the qualified rule's block is parsed as a style block's contents.
        style->add(rule->block.value, baseurl, doc->container());

        for(const auto& sel : list)
        {
            sel->m_style       = style;
            sel->m_media_query = media;
            sel->calc_specificity();
            add_selector(sel);
        }
        return true;
    }

    void css::sort_selectors()
    {
        std::sort(m_selectors.begin(), m_selectors.end(),
                  [](const css_selector::ptr& v1, const css_selector::ptr& v2) { return (*v1) < (*v2); });
        rebuild_selector_index();
    }

    void css::rebuild_selector_index()
    {
        const auto start = std::chrono::steady_clock::now();
        m_index = {};
        // Tiny stylesheets are faster on the existing contiguous loop and avoid
        // paying index allocation/build cost during document startup.
        m_index.enabled = selector_index_enabled() && m_selectors.size() >= 32;
        if(m_index.enabled)
        {
            for(size_t i = 0; i < m_selectors.size(); ++i)
            {
                const auto& right = m_selectors[i]->m_right;
                const css_attribute_selector* id_key = nullptr;
                const css_attribute_selector* class_key = nullptr;
                const css_attribute_selector* attr_key = nullptr;
                for(const auto& attr : right.m_attrs)
                {
                    if(attr.type == select_id && !id_key) id_key = &attr;
                    else if(attr.type == select_class && !class_key) class_key = &attr;
                    else if(attr.type == select_attr && !attr_key) attr_key = &attr;
                }
                // Every selector has exactly one primary rightmost key. A selector
                // that can match an element is therefore present in one of the
                // element's queried buckets; the full matcher remains authoritative.
                if(id_key) m_index.ids[id_key->name].push_back(i);
                else if(class_key) m_index.classes[class_key->name].push_back(i);
                else if(right.m_tag != star_id) m_index.tags[right.m_tag].push_back(i);
                else if(attr_key) m_index.attributes[_s(attr_key->name)].push_back(i);
                else m_index.universal.push_back(i);
            }
            auto account = [this](const auto& buckets) {
                for(const auto& pair : buckets)
                    m_index.bytes += sizeof(pair.first) + sizeof(size_t) * pair.second.capacity();
            };
            account(m_index.ids); account(m_index.classes); account(m_index.tags); account(m_index.attributes);
            m_index.bytes += sizeof(size_t) * m_index.universal.capacity();
        }
        m_index.build_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        m_index_diagnostics = {};
        m_index_diagnostics.build_ns = m_index.build_ns;
        m_index_diagnostics.bytes = m_index.bytes;
        m_index_diagnostics.enabled = m_index.enabled;
    }

    css_selector::vector css::candidate_selectors(string_id tag, string_id id,
                                                  const std::vector<string_id>& classes,
                                                  const string_map& attributes) const
    {
        ++m_index_diagnostics.query_count;
        m_index_diagnostics.total_rules_considered += m_selectors.size();
        if(!m_index.enabled)
        {
            m_index_diagnostics.candidate_rules += m_selectors.size();
            return m_selectors;
        }
        std::vector<uint8_t> selected(m_selectors.size(), 0);
        auto mark = [&selected](const std::vector<size_t>* bucket) {
            if(bucket) for(size_t index : *bucket) selected[index] = 1;
        };
        mark(&m_index.universal);
        if(const auto it = m_index.ids.find(id); it != m_index.ids.end()) mark(&it->second);
        for(string_id cls : classes)
            if(const auto it = m_index.classes.find(cls); it != m_index.classes.end()) mark(&it->second);
        if(const auto it = m_index.tags.find(tag); it != m_index.tags.end()) mark(&it->second);
        for(const auto& attr : attributes)
            if(const auto it = m_index.attributes.find(lowcase(attr.first)); it != m_index.attributes.end()) mark(&it->second);

        css_selector::vector result;
        for(size_t i = 0; i < selected.size(); ++i)
            if(selected[i]) result.push_back(m_selectors[i]);
        m_index_diagnostics.candidate_rules += result.size();
        return result;
    }

    void css::set_selector_index_enabled(bool enabled)
    {
        g_selector_index_enabled.store(enabled, std::memory_order_relaxed);
    }

    bool css::selector_index_enabled()
    {
        return g_selector_index_enabled.load(std::memory_order_relaxed);
    }

} // namespace litehtml
