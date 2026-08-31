#ifndef LITEHTML_EL_SCRIPT_H
#define LITEHTML_EL_SCRIPT_H

#include "element.h"

namespace litehtml
{
    class el_script : public element
    {
        std::string m_text;
        std::string m_src;
        bool        m_has_src = false;

      public:
        explicit el_script(const std::shared_ptr<document>& doc);

        void        parse_attributes() override;
        void        set_attr(const char* name, const char* val) override;
        bool        remove_attr(const char* name) override;
        const char* get_attr(const char* name, const char* def = nullptr) const override;
        bool        appendChild(const ptr& el) override;
        void        clearRecursive() override;
        string_id   tag() const override;
        const char* get_tagName() const override;

        /** Source text collected from this inline script element. */
        const std::string& text() const
        {
            return m_text;
        }
        const char* src() const
        {
            return m_has_src ? m_src.c_str() : nullptr;
        }
    };
} // namespace litehtml

#endif // LITEHTML_EL_SCRIPT_H
