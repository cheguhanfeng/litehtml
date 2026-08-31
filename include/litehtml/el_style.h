#ifndef LITEHTML_EL_STYLE_H
#define LITEHTML_EL_STYLE_H

#include "html_tag.h"

namespace litehtml
{
    class el_style : public html_tag
    {
      public:
        explicit el_style(const std::shared_ptr<document>& doc);

        void        parse_attributes() override;
        void        compute_styles(bool recursive) override;
        string_id   tag() const override;
        const char* get_tagName() const override;
    };
} // namespace litehtml

#endif // LITEHTML_EL_STYLE_H
