// Static-link diagnostic: exercise complete native background geometry, which
// the narrower C drawing callbacks do not expose. Also count actual compute allocations.
#include "litehtml/document.h"
#include "litehtml/document_container.h"
#include "litehtml/html_tag.h"
#include "litehtml/render_item.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <vector>

static bool counting = false;
static size_t allocations = 0;
void* operator new(size_t n) { if(counting) ++allocations; if(auto* p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

using namespace litehtml;
struct probe_container : document_container {
    uint_ptr create_font(const font_description& d, const document*, font_metrics* fm) override {
        fm->font_size=d.size; fm->height=d.size; fm->ascent=d.size; fm->x_height=d.size/2;
        return 1;
    }
    void delete_font(uint_ptr) override {}
    pixel_t text_width(const char* text, uint_ptr) override { return pixel_t(float(std::strlen(text))*7); }
    void draw_text(uint_ptr,const char*,uint_ptr,web_color,const position&) override {}
    pixel_t pt_to_px(float v) const override { return pixel_t(v); }
    pixel_t get_default_font_size() const override { return 16_px; }
    const char* get_default_font_name() const override { return "Arial"; }
    void draw_list_marker(uint_ptr,const list_marker&) override {}
    void load_image(const char*,const char*,bool) override {}
    void get_image_size(const char*,const char*,size& s) override { s={13_px,17_px}; }
    void draw_image(uint_ptr,const background_layer&,const std::string&,const std::string&) override {}
    void draw_solid_fill(uint_ptr,const background_layer&,const web_color&) override {}
    void draw_linear_gradient(uint_ptr,const background_layer&,const background_layer::linear_gradient&) override {}
    void draw_radial_gradient(uint_ptr,const background_layer&,const background_layer::radial_gradient&) override {}
    void draw_conic_gradient(uint_ptr,const background_layer&,const background_layer::conic_gradient&) override {}
    void draw_borders(uint_ptr,const borders&,const position&,bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const document::ptr&,const element::ptr&) override {}
    void on_anchor_click(const char*,const element::ptr&) override {}
    void on_mouse_event(const element::ptr&,mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(std::string&,text_transform) override {}
    void import_css(std::string&,const std::string&,std::string&) override {}
    void set_clip(const position&,const border_radiuses&) override {}
    void del_clip() override {}
    void get_viewport(position& p) const override { p={0_px,0_px,800_px,600_px}; }
    element::ptr create_element(const char*,const string_map&,const document::ptr&) override { return nullptr; }
    void get_media_features(media_features& m) const override { m.width=800_px;m.height=600_px; }
    void get_language(std::string&,std::string&) const override {}
};

std::vector<float> geometry(const element::ptr& el) {
    std::vector<float> result;
    const auto& bg=el->css().get_bg(); auto ri=el->get_render_item();
    if(!ri) throw std::runtime_error("missing renderer");
    result.push_back(float(bg.get_layers_number()));
    for(int i=0;i<bg.get_layers_number();++i) {
        background_layer layer;
        if(!bg.get_layer(i,{0_px,0_px,80_px,60_px},el.get(),ri,layer)) throw std::runtime_error("missing layer");
        for(const auto* box:{&layer.border_box,&layer.clip_box,&layer.origin_box})
            for(auto value:{box->x,box->y,box->width,box->height}) result.push_back(value.value());
        result.push_back(float(layer.attachment)); result.push_back(float(layer.repeat));
    }
    return result;
}

int main() {
    probe_container container;
    const std::string defaults="background-position:0% 0%;background-size:auto auto;background-repeat:repeat;"
        "background-attachment:scroll;background-clip:border-box;background-origin:padding-box;";
    // Position inheritance uses the native longhands: the existing shorthand
    // inherit expansion does not include background-position's x/y components.
    const std::string inherited="background-position-x:inherit;background-position-y:inherit;background-size:inherit;background-repeat:inherit;"
        "background-attachment:inherit;background-clip:inherit;background-origin:inherit;";
    const std::string explicit_values="background-position:25% 75%;background-size:20px 30px;background-repeat:no-repeat;"
        "background-attachment:fixed;background-clip:content-box;background-origin:content-box;";
    auto doc=document::createFromString("<html><body><div id='parent'><div id='implicit'></div></div>"
        "<div id='explicit'></div><div id='plain'></div></body></html>",&container);
    auto implicit=doc->get_element_by_id("implicit"), explicit_el=doc->get_element_by_id("explicit");
    auto parent=doc->get_element_by_id("parent");
    const std::string box="display:block;width:80px;height:60px;padding:7px;border:3px solid black;";
    size_t cases=0; std::vector<float> fingerprint;
    for(const char* image:{"", "background-image:url(a.png);", "background-image:url(a.png),url(b.png);"}) {
        const std::string common=box+image+"background-color:red;";
        for(int step=0;step<4;++step) {
            parent->set_attr("style",step==2 ? explicit_values.c_str() : "");
            implicit->set_attr("style",(common+(step==0 ? "" : step==3 ? "" : inherited)).c_str());
            explicit_el->set_attr("style",(common+(step==2 ? explicit_values : defaults)).c_str());
            doc->render(800_px);
            auto left=geometry(implicit),right=geometry(explicit_el);
            if(left!=right) {
                std::cerr<<"background mismatch case "<<cases<<'\n';
                for(size_t i=0;i<left.size();++i) if(i>=right.size() || left[i]!=right[i])
                    std::cerr<<i<<": "<<left[i]<<" vs "<<(i<right.size()?right[i]:-999)<<'\n';
                return 1;
            }
            fingerprint.insert(fingerprint.end(),left.begin(),left.end()); ++cases;
        }
    }
    auto plain=std::dynamic_pointer_cast<html_tag>(doc->get_element_by_id("plain"));
    { css_properties warm; warm.compute(plain.get(),doc); }
    allocations=0; counting=true;
    for(int i=0;i<100;++i) { css_properties fresh; fresh.compute(plain.get(),doc); }
    counting=false;
    std::cout<<"{\"cases\":"<<cases<<",\"computeAllocations100\":"<<allocations<<",\"geometry\":[";
    for(size_t i=0;i<fingerprint.size();++i) std::cout<<(i ? "," : "")<<fingerprint[i];
    std::cout<<"]}\n";
}
