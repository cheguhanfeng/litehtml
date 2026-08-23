#ifndef LITEHTML_STYLESHEET_H
#define LITEHTML_STYLESHEET_H

#include <utility>

#include "css_selector.h"
#include "css_tokenizer.h"
#include <cstdint>
#include <map>

namespace litehtml
{

    // https://www.w3.org/TR/cssom-1/#css-declarations
    struct raw_declaration
    {
        using vector = std::vector<raw_declaration>;

        // property name
        std::string name;
        // default value is specified here to get rid of gcc warning "missing initializer for member"
        css_token_vector value;

        bool important = false;

        operator bool() const
        {
            return name != "";
        }
    };

    // intermediate half-parsed rule that is used internally by the parser
    class raw_rule
    {
      public:
        using ptr    = std::shared_ptr<raw_rule>;
        using vector = std::vector<ptr>;

        enum rule_type
        {
            qualified,
            at
        };

        raw_rule(rule_type type, std::string name = {}) :
            type(type),
            name(std::move(name))
        {
        }

        rule_type type;
        // An at-rule has a name, a prelude consisting of a list of component values, and an optional block consisting
        // of a simple {} block.
        std::string name;
        // https://www.w3.org/TR/css-syntax-3/#qualified-rule
        // A qualified rule has a prelude consisting of a list of component values, and a block consisting of a simple
        // {} block. Note: Most qualified rules will be style rules, where the prelude is a selector and the block a
        // list of declarations.
        css_token_vector prelude;
        css_token        block;
    };

    class css
    {
        css_selector::vector m_selectors;

        struct selector_index
        {
            std::map<string_id, std::vector<size_t>> ids;
            std::map<string_id, std::vector<size_t>> classes;
            std::map<string_id, std::vector<size_t>> tags;
            std::map<std::string, std::vector<size_t>> attributes;
            std::vector<size_t> universal;
            uint64_t build_ns = 0;
            size_t bytes = 0;
            bool enabled = false;
        };
        selector_index m_index;

      public:
        struct selector_index_diagnostics
        {
            uint64_t build_ns = 0;
            uint64_t query_count = 0;
            uint64_t total_rules_considered = 0;
            uint64_t candidate_rules = 0;
            size_t bytes = 0;
            bool enabled = false;
        };

      private:
        mutable selector_index_diagnostics m_index_diagnostics;

      public:
        const css_selector::vector& selectors() const
        {
            return m_selectors;
        }

        template <class Input>
        void parse_css_stylesheet(const Input& input, const std::string& baseurl, const std::shared_ptr<document>& doc,
                                  const media_query_list_list::ptr& media = nullptr, bool top_level = true);

        void sort_selectors();

        css_selector::vector candidate_selectors(string_id tag, string_id id,
                                                  const std::vector<string_id>& classes,
                                                  const string_map& attributes) const;
        const selector_index_diagnostics& index_diagnostics() const { return m_index_diagnostics; }
        static void set_selector_index_enabled(bool enabled);
        static bool selector_index_enabled();

      private:
        bool parse_style_rule(const raw_rule::ptr& rule, const std::string& baseurl,
                              const std::shared_ptr<document>& doc, const media_query_list_list::ptr& media);
        void parse_import_rule(const raw_rule::ptr& rule, const std::string& baseurl,
                               const std::shared_ptr<document>& doc, const media_query_list_list::ptr& media);
        void add_selector(const css_selector::ptr& selector);
        void rebuild_selector_index();
    };

    inline void css::add_selector(const css_selector::ptr& selector)
    {
        selector->m_order = static_cast<int>(m_selectors.size());
        m_selectors.push_back(selector);
    }

} // namespace litehtml

#endif // LITEHTML_STYLESHEET_H
