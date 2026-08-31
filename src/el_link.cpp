#include "el_link.h"
#include "document.h"
#include "document_container.h"
#include "html.h"
#include <cstring>

litehtml::el_link::el_link(const std::shared_ptr<document>& doc) :
    litehtml::html_tag(doc)
{
}

void litehtml::el_link::parse_attributes()
{
    if(get_document()->stylesheet_collection_suppressed()) return;
    bool processed = false;

    document::ptr doc = get_document();

    const char* rel = get_attr("rel");
    bool is_stylesheet = false;
    bool is_alternate  = false;
    for(const auto& token : split_string(lowcase(rel ? rel : ""), whitespace, "", ""))
    {
        is_stylesheet = is_stylesheet || token == "stylesheet";
        is_alternate  = is_alternate || token == "alternate";
    }
    if(is_stylesheet && !is_alternate)
    {
        const char* media = get_attr("media");
        const char* href  = get_attr("href");
        if(href && href[0])
        {
            std::string css_text;
            std::string css_baseurl;
            doc->container()->import_css(css_text, href, css_baseurl);
            if(!css_text.empty())
            {
                doc->add_stylesheet(css_text.c_str(), css_baseurl.c_str(), media);
                processed = true;
            }
        }
    }

    if(!processed)
    {
        doc->container()->link(doc, shared_from_this());
    }
}
