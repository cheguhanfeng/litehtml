#ifndef LITEHTML_EL_SCRIPT_H
#define LITEHTML_EL_SCRIPT_H

#include "element.h"

namespace litehtml
{
    class el_script : public element
    {
        std::string m_text;
        std::string m_src;

      public:
        explicit el_script(const std::shared_ptr<document>& doc);

        void        parse_attributes() override;
        void        set_attr(const char* name, const char* val) override;
        bool        appendChild(const ptr& el) override;
        string_id   tag() const override;
        const char* get_tagName() const override;

        /** Source text collected from this inline script element. */
        const std::string& text() const { return m_text; }
        const char* src() const { return m_src.empty() ? nullptr : m_src.c_str(); }
    };
} // namespace litehtml

#endif // LITEHTML_EL_SCRIPT_H
