#ifndef LITEHTML_FONT_DESCRIPTION_H
#define LITEHTML_FONT_DESCRIPTION_H

#include "css_length.h"
#include "web_color.h"
#include <string>
#include <cstdint>
#include <cstring>
#include <tuple>

namespace litehtml
{
    struct font_description
    {
        std::string family;                    // Font Family
        pixel_t     size;                      // Font size
        font_style  style = font_style_normal; // Font stype, see the enum font_style
        int         weight;                    // Font weight.
        int         decoration_line =
            text_decoration_line_none;   // Decoration line. A bitset of flags of the enum text_decoration_line
        css_length decoration_thickness; // Decoration line thickness in pixels. See predefined values in
                                         // enumtext_decoration_thickness
        text_decoration_style decoration_style =
            text_decoration_style_solid; // Decoration line style. See enum text_decoration_style
        web_color   decoration_color = web_color::current_color;     // Decoration line color
        std::string emphasis_style;                                  // Text emphasis style
        web_color   emphasis_color    = web_color::current_color;    // Text emphasis color
        int         emphasis_position = text_emphasis_position_over; // Text emphasis position

        std::string hash() const
        {
            std::string out;
            out += family;
            out += ":sz=" + std::to_string(size.value());
            out += ":st=" + std::to_string(static_cast<int>(style));
            out += ":w=" + std::to_string(weight);
            out += ":dl=" + std::to_string(decoration_line);
            out += ":dt=" + decoration_thickness.to_string();
            out += ":ds=" + std::to_string(static_cast<int>(decoration_style));
            out += ":dc=" + decoration_color.to_string();
            out += ":ephs=" + emphasis_style;
            out += ":ephc=" + emphasis_color.to_string();
            out += ":ephp=" + std::to_string(emphasis_position);

            return out;
        }
    };

    // Compare cache lookups without allocating or formatting a string key.
    // Keep hash() available for callers that need the legacy textual description.
    struct font_description_less
    {
      private:
        static std::uint32_t float_bits(float value)
        {
            std::uint32_t bits;
            static_assert(sizeof(bits) == sizeof(value), "font cache expects 32-bit floats");
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        static auto length_key(const css_length& length)
        {
            // Predefined lengths can retain inactive units from an earlier value.
            return std::make_tuple(length.is_predefined(),
                length.is_predefined() ? 0 : static_cast<int>(length.units()),
                length.is_predefined() ? static_cast<std::uint32_t>(length.predef()) : float_bits(length.val()));
        }

        static auto color_key(const web_color& color)
        {
            const auto rgba = color.is_current_color ? 0u :
                (std::uint32_t(color.red) << 24) | (std::uint32_t(color.green) << 16) |
                (std::uint32_t(color.blue) << 8) | std::uint32_t(color.alpha);
            return std::make_tuple(color.is_current_color, rgba);
        }

      public:
        bool operator()(const font_description& a, const font_description& b) const
        {
            // Bitwise float keys give a strict ordering even for NaNs and avoid
            // the old decimal key's rounding collisions. String fields are references.
            return std::tuple_cat(std::tie(a.family), std::make_tuple(float_bits(a.size.value()), a.style,
                       a.weight, a.decoration_line, length_key(a.decoration_thickness), a.decoration_style,
                       color_key(a.decoration_color)), std::tie(a.emphasis_style),
                       std::make_tuple(color_key(a.emphasis_color), a.emphasis_position)) <
                   std::tuple_cat(std::tie(b.family), std::make_tuple(float_bits(b.size.value()), b.style,
                       b.weight, b.decoration_line, length_key(b.decoration_thickness), b.decoration_style,
                       color_key(b.decoration_color)), std::tie(b.emphasis_style),
                       std::make_tuple(color_key(b.emphasis_color), b.emphasis_position));
        }
    };
} // namespace litehtml

#endif // LITEHTML_FONT_DESCRIPTION_H
