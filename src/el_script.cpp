#include "el_script.h"
#include "document.h"
#include "html.h"

litehtml::el_script::el_script(const std::shared_ptr<document>& doc) :
    element(doc)
{
}

void litehtml::el_script::parse_attributes()
{
    // TODO: pass script text to document container
}

void litehtml::el_script::set_attr(const char* name, const char* val)
{
    if(name && t_strcasecmp(name, "src") == 0)
    {
        m_src     = val ? val : "";
        m_has_src = true;
    }
}

bool litehtml::el_script::remove_attr(const char* name)
{
    if(!name || t_strcasecmp(name, "src") != 0 || !m_has_src)
    {
        return false;
    }
    m_src.clear();
    m_has_src = false;
    return true;
}

const char* litehtml::el_script::get_attr(const char* name, const char* def) const
{
    return name && t_strcasecmp(name, "src") == 0 && m_has_src ? m_src.c_str() : def;
}

bool litehtml::el_script::appendChild(const ptr& el)
{
    el->get_text(m_text);
    return true;
}

void litehtml::el_script::clearRecursive()
{
    m_text.clear();
}

litehtml::string_id litehtml::el_script::tag() const
{
    return _script_;
}

const char* litehtml::el_script::get_tagName() const
{
    return "script";
}
