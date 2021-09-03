/*
MIT License

Copyright (c) 2019 Vilya Harvey

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "minipbrt.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#ifndef _WIN32
#include <errno.h>
#endif

#define MINIPBRT_STATIC_ARRAY_LENGTH(arr)  static_cast<uint32_t>(sizeof(arr) / sizeof((arr)[0]))


/// miniply - A simple and fast parser for PLY files
/// ================================================
///
/// For details about the PLY format see:
/// * http://paulbourke.net/dataformats/ply/
/// * https://en.wikipedia.org/wiki/PLY_(file_format)

namespace miniply {

  //
  // Constants
  //

  static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

  // Standard PLY element names
  extern const char* kPLYVertexElement; // "vertex"
  extern const char* kPLYFaceElement;   // "face"


  //
  // PLY Parsing types
  //

  enum class PLYFileType {
    ASCII,
    Binary,
    BinaryBigEndian,
  };


  enum class PLYPropertyType {
    Char,
    UChar,
    Short,
    UShort,
    Int,
    UInt,
    Float,
    Double,

    None, //!< Special value used in Element::listCountType to indicate a non-list property.
  };


  struct PLYProperty {
    std::string name;
    PLYPropertyType type      = PLYPropertyType::None; //!< Type of the data. Must be set to a value other than None.
    PLYPropertyType countType = PLYPropertyType::None; //!< None indicates this is not a list type, otherwise it's the type for the list count.
    uint32_t offset           = 0;                  //!< Byte offset from the start of the row.
    uint32_t stride           = 0;

    std::vector<uint8_t> listData;
    std::vector<uint32_t> rowCount; // Entry `i` is the number of items (*not* the number of bytes) in row `i`.
  };


  struct PLYElement {
    std::string              name;              //!< Name of this element.
    std::vector<PLYProperty> properties;
    uint32_t                 count      = 0;    //!< The number of items in this element (e.g. the number of vertices if this is the vertex element).
    bool                     fixedSize  = true; //!< `true` if there are only fixed-size properties in this element, i.e. no list properties.
    uint32_t                 rowStride  = 0;    //!< The number of bytes from the start of one row to the start of the next, for this element.

    void calculate_offsets();

    /// Returns the index for the named property in this element, or `kInvalidIndex`
    /// if it can't be found.
    uint32_t find_property(const char* propName) const;

    /// Return the indices for several properties in one go. Use it like this:
    /// ```
    /// uint32_t indexes[3];
    /// if (elem.find_properties(indexes, 3, "foo", "bar", "baz")) { ... }
    /// ```
    /// `propIdxs` is where the property indexes will be stored. `numIdxs` is
    /// the number of properties we will look up. There must be exactly
    /// `numIdxs` parameters after `numIdxs`; each of the is a c-style string
    /// giving the name of a property.
    ///
    /// The return value will be true if all properties were found. If it was
    /// not true, you should not use any values from propIdxs.
    bool find_properties(uint32_t propIdxs[], uint32_t numIdxs, ...) const;

    /// Same as `find_properties`, for when you already have a `va_list`. This
    /// is called internally by both `PLYElement::find_properties` and
    /// `PLYReader::find_properties`.
    bool find_properties_va(uint32_t propIdxs[], uint32_t numIdxs, va_list names) const;

    /// Call this on the element at some point before you load its data, when
    /// you know that every row's list will have the same length. It will
    /// replace the single variable-size property with a set of new fixed-size
    /// properties: one for the list count, followed by one for each of the
    /// list values. This will allow miniply to load and extract the property
    /// data a lot more efficiently, giving a big performance increase.
    ///
    /// After you've called this, you must use PLYReader's `extract_columns`
    /// method to get the data, rather than `extract_list_column`.
    ///
    /// The `newPropIdxs` parameter must be an array with at least `listSize`
    /// entries. If the function returns true, this will have been populated
    /// with the indices of the new properties that represent the list values
    /// (i.e. not including the list count property, which will have the same
    /// index as the old list property).
    ///
    /// The function returns false if the property index is invalid, or the
    /// property it refers to is not a list property. In these cases it will
    /// not modify anything. Otherwise it will return true.
    bool convert_list_to_fixed_size(uint32_t listPropIdx, uint32_t listSize, uint32_t newPropIdxs[]);
  };


  class PLYReader {
  public:
    PLYReader(const char* filename);
    ~PLYReader();

    bool valid() const;
    bool has_element() const;
    const PLYElement* element() const;
    bool load_element();
    void next_element();

    PLYFileType file_type() const;
    int version_major() const;
    int version_minor() const;
    uint32_t num_elements() const;
    uint32_t find_element(const char* name) const;
    PLYElement* get_element(uint32_t idx);

    /// Check whether the current element has the given name.
    bool element_is(const char* name) const;

    /// Number of rows in the current element.
    uint32_t num_rows() const;

    /// Returns the index for the named property in the current element, or
    /// `kInvalidIndex` if it can't be found.
    uint32_t find_property(const char* name) const;

    /// Equivalent to calling `find_properties` on the current element.
    bool find_properties(uint32_t propIdxs[], uint32_t numIdxs, ...) const;

    /// Copy the data for the specified properties into `dest`, which must be
    /// an array with at least enough space to hold all of the extracted column
    /// data. `propIdxs` is an array containing the indexes of the properties
    /// to copy; it has `numProps` elements.
    ///
    /// `destType` specifies the data type for values stored in `dest`. All
    /// property values will be converted to this type if necessary.
    ///
    /// This function does some checks up front to pick the most efficient code
    /// path for extracting the data. It considers:
    /// (a) whether any data conversion is required.
    /// (b) whether all property values to be extracted are in contiguous
    ///     memory locations for any given item.
    /// (c) whether the data for all rows is contiguous in memory.
    /// In the best case it reduces to a single memcpy call. In the worst case
    /// we must iterate over all values to be copied, applying type conversions
    /// as we go.
    ///
    /// Note that this function does not handle list-valued properties. Use
    /// `extract_list_column()` for those instead.
    bool extract_properties(const uint32_t propIdxs[], uint32_t numProps, PLYPropertyType destType, void* dest) const;

    /// Get the array of item counts for a list property. Entry `i` in this
    /// array is the number of items in the `i`th list.
    const uint32_t* get_list_counts(uint32_t propIdx) const;

    const uint8_t* get_list_data(uint32_t propIdx) const;
    bool extract_list_property(uint32_t propIdx, PLYPropertyType destType, void* dest) const;

    uint32_t num_triangles(uint32_t propIdx) const;
    bool requires_triangulation(uint32_t propIdx) const;
    bool extract_triangles(uint32_t propIdx, const float pos[], uint32_t numVerts, PLYPropertyType destType, void* dest) const;

    bool find_pos(uint32_t propIdxs[3]) const;
    bool find_normal(uint32_t propIdxs[3]) const;
    bool find_texcoord(uint32_t propIdxs[2]) const;
    bool find_color(uint32_t propIdxs[3]) const;
    bool find_indices(uint32_t propIdxs[1]) const;

  private:
    bool refill_buffer();
    bool rewind_to_safe_char();
    bool accept();
    bool advance();
    bool next_line();
    bool match(const char* str);
    bool which(const char* values[], uint32_t* index);
    bool which_property_type(PLYPropertyType* type);
    bool keyword(const char* kw);
    bool identifier(char* dest, size_t destLen);

    template <class T> // T must be a type compatible with uint32_t.
    bool typed_which(const char* values[], T* index) {
      return which(values, reinterpret_cast<uint32_t*>(index));
    }

    bool int_literal(int* value);
    bool float_literal(float* value);
    bool double_literal(double* value);

    bool parse_elements();
    bool parse_element();
    bool parse_property(std::vector<PLYProperty>& properties);

    bool load_fixed_size_element(PLYElement& elem);
    bool load_variable_size_element(PLYElement& elem);

    bool load_ascii_scalar_property(PLYProperty& prop, size_t& destIndex);
    bool load_ascii_list_property(PLYProperty& prop);
    bool load_binary_scalar_property(PLYProperty& prop, size_t& destIndex);
    bool load_binary_list_property(PLYProperty& prop);
    bool load_binary_scalar_property_big_endian(PLYProperty& prop, size_t& destIndex);
    bool load_binary_list_property_big_endian(PLYProperty& prop);

    bool ascii_value(PLYPropertyType propType, uint8_t value[8]);

  private:
    FILE* m_f             = nullptr;
    char* m_buf           = nullptr;
    const char* m_bufEnd  = nullptr;
    const char* m_pos     = nullptr;
    const char* m_end     = nullptr;
    bool m_inDataSection  = false;
    bool m_atEOF          = false;
    int64_t m_bufOffset   = 0;

    bool m_valid          = false;

    PLYFileType m_fileType = PLYFileType::ASCII; //!< Whether the file was ascii, binary little-endian, or binary big-endian.
    int m_majorVersion     = 0;
    int m_minorVersion     = 0;
    std::vector<PLYElement> m_elements;         //!< Element descriptors for this file.

    size_t m_currentElement = 0;
    bool m_elementLoaded    = false;
    std::vector<uint8_t> m_elementData;

    char* m_tmpBuf = nullptr;
  };


  /// Given a polygon with `n` vertices, where `n` > 3, triangulate it and
  /// store the indices for the resulting triangles in `dst`. The `pos`
  /// parameter is the array of all vertex positions for the mesh; `indices` is
  /// the list of `n` indices for the polygon we're triangulating; and `dst` is
  /// where we write the new indices to.
  ///
  /// The triangulation will always produce `n - 2` triangles, so `dst` must
  /// have enough space for `3 * (n - 2)` indices.
  ///
  /// If `n == 3`, we simply copy the input indices to `dst`. If `n < 3`,
  /// nothing gets written to dst.
  ///
  /// The return value is the number of triangles.
  uint32_t triangulate_polygon(uint32_t n, const float pos[], uint32_t numVerts, const int indices[], int dst[]);

} // namespace miniply


namespace miniply {

  //
  // Public constants
  //

  // Standard PLY element names
  const char* kPLYVertexElement = "vertex";
  const char* kPLYFaceElement = "face";


  //
  // PLY constants
  //

  static constexpr uint32_t kPLYReadBufferSize = 128 * 1024;
  static constexpr uint32_t kPLYTempBufferSize = kPLYReadBufferSize;

  static const char* kPLYFileTypes[] = { "ascii", "binary_little_endian", "binary_big_endian", nullptr };
  static const uint32_t kPLYPropertySize[]= { 1, 1, 2, 2, 4, 4, 4, 8 };

  struct PLYTypeAlias {
    const char* name;
    PLYPropertyType type;
  };

  static const PLYTypeAlias kTypeAliases[] = {
    { "char",   PLYPropertyType::Char   },
    { "uchar",  PLYPropertyType::UChar  },
    { "short",  PLYPropertyType::Short  },
    { "ushort", PLYPropertyType::UShort },
    { "int",    PLYPropertyType::Int    },
    { "uint",   PLYPropertyType::UInt   },
    { "float",  PLYPropertyType::Float  },
    { "double", PLYPropertyType::Double },

    { "uint8",  PLYPropertyType::UChar  },
    { "uint16", PLYPropertyType::UShort },
    { "uint32", PLYPropertyType::UInt   },

    { "int8",   PLYPropertyType::Char   },
    { "int16",  PLYPropertyType::Short  },
    { "int32",  PLYPropertyType::Int    },

    { nullptr,  PLYPropertyType::None   }
  };


  //
  // Constants
  //

  static constexpr double kDoubleDigits[10] = { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0 };

  static constexpr float kPi = 3.14159265358979323846f;


  //
  // Vec2 type
  //

  struct Vec2 {
    float x, y;
  };

  static inline Vec2 operator - (Vec2 lhs, Vec2 rhs) { return Vec2{ lhs.x - rhs.x, lhs.y - rhs.y }; }

  static inline float dot(Vec2 lhs, Vec2 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }
  static inline float length(Vec2 v) { return std::sqrt(dot(v, v)); }
  static inline Vec2 normalize(Vec2 v) { float len = length(v); return Vec2{ v.x / len, v.y / len }; }


  //
  // Vec3 type
  //

  struct Vec3 {
    float x, y, z;
  };

  static inline Vec3 operator - (Vec3 lhs, Vec3 rhs) { return Vec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z }; }

  static inline float dot(Vec3 lhs, Vec3 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }
  static inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
  static inline Vec3 normalize(Vec3 v) { float len = length(v); return Vec3{ v.x / len, v.y / len, v.z / len }; }
  static inline Vec3 cross(Vec3 lhs, Vec3 rhs) { return Vec3{ lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x }; }


  //
  // Internal-only functions
  //

  static inline bool is_whitespace(char ch)
  {
    return ch == ' ' || ch == '\t' || ch == '\r';
  }


  static inline bool is_digit(char ch)
  {
    return ch >= '0' && ch <= '9';
  }


  static inline bool is_letter(char ch)
  {
    ch |= 32; // upper and lower case letters differ only at this bit.
    return ch >= 'a' && ch <= 'z';
  }


  static inline bool is_alnum(char ch)
  {
    return is_digit(ch) || is_letter(ch);
  }


  static inline bool is_keyword_start(char ch)
  {
    return is_letter(ch) || ch == '_';
  }


  static inline bool is_keyword_part(char ch)
  {
    return is_alnum(ch) || ch == '_';
  }


  static inline bool is_safe_buffer_end(char ch)
  {
    return (ch > 0 && ch <= 32) || (ch >= 127);
  }


  static int file_open(FILE** f, const char* filename, const char* mode)
  {
  #ifdef _WIN32
    return fopen_s(f, filename, mode);
  #else
    *f = fopen(filename, mode);
    return (*f != nullptr) ? 0 : errno;
  #endif
  }


  static inline int file_seek(FILE* file, int64_t offset, int origin)
  {
  #ifdef _WIN32
    return _fseeki64(file, offset, origin);
  #else
    static_assert(sizeof(off_t) == sizeof(int64_t), "off_t is not 64 bits.");
    return fseeko(file, offset, origin);
  #endif
  }


  static bool int_literal(const char* start, char const** end, int* val)
  {
    const char* pos = start;

    bool negative = false;
    if (*pos == '-') {
      negative = true;
      ++pos;
    }
    else if (*pos == '+') {
      ++pos;
    }

    bool hasLeadingZeroes = *pos == '0';
    if (hasLeadingZeroes) {
      do {
        ++pos;
      } while (*pos == '0');
    }

    int numDigits = 0;
    int localVal = 0;
    while (is_digit(*pos)) {
      // FIXME: this will overflow if we get too many digits.
      localVal = localVal * 10 + static_cast<int>(*pos - '0');
      ++numDigits;
      ++pos;
    }

    if (numDigits == 0 && hasLeadingZeroes) {
      numDigits = 1;
    }

    if (numDigits == 0 || is_letter(*pos) || *pos == '_') {
      return false;
    }
    else if (numDigits > 10) {
      // Overflow, literal value is larger than an int can hold.
      // FIXME: this won't catch *all* cases of overflow, make it exact.
      return false;
    }

    if (val != nullptr) {
      *val = negative ? -localVal : localVal;
    }
    if (end != nullptr) {
      *end = pos;
    }
    return true;
  }


  static bool double_literal(const char* start, char const** end, double* val)
  {
    const char* pos = start;

    bool negative = false;
    if (*pos == '-') {
      negative = true;
      ++pos;
    }
    else if (*pos == '+') {
      ++pos;
    }

    double localVal = 0.0;

    bool hasIntDigits = is_digit(*pos);
    if (hasIntDigits) {
      do {
        localVal = localVal * 10.0 + kDoubleDigits[*pos - '0'];
        ++pos;
      } while (is_digit(*pos));
    }
    else if (*pos != '.') {
//      set_error("Not a floating point number");
      return false;
    }

    bool hasFracDigits = false;
    if (*pos == '.') {
      ++pos;
      hasFracDigits = is_digit(*pos);
      if (hasFracDigits) {
        double scale = 0.1;
        do {
          localVal += scale * kDoubleDigits[*pos - '0'];
          scale *= 0.1;
          ++pos;
        } while (is_digit(*pos));
      }
      else if (!hasIntDigits) {
//        set_error("Floating point number has no digits before or after the decimal point");
        return false;
      }
    }

    bool hasExponent = *pos == 'e' || *pos == 'E';
    if (hasExponent) {
      ++pos;
      bool negativeExponent = false;
      if (*pos == '-') {
        negativeExponent = true;
        ++pos;
      }
      else if (*pos == '+') {
        ++pos;
      }

      if (!is_digit(*pos)) {
//        set_error("Floating point exponent has no digits");
        return false; // error: exponent part has no digits.
      }

      double exponent = 0.0;
      do {
        exponent = exponent * 10.0 + kDoubleDigits[*pos - '0'];
        ++pos;
      } while (is_digit(*pos));

      if (val != nullptr) {
        if (negativeExponent) {
          exponent = -exponent;
        }
        localVal *= std::pow(10.0, exponent);
      }
    }

    if (*pos == '.' || *pos == '_' || is_alnum(*pos)) {
//      set_error("Floating point number has trailing chars");
      return false;
    }

    if (negative) {
      localVal = -localVal;
    }

    if (val != nullptr) {
      *val = localVal;
    }
    if (end != nullptr) {
      *end = pos;
    }
    return true;
  }


  static bool float_literal(const char* start, char const** end, float* val)
  {
    double tmp = 0.0;
    bool ok = double_literal(start, end, &tmp);
    if (ok && val != nullptr) {
      *val = static_cast<float>(tmp);
    }
    return ok;
  }


  static inline void endian_swap_2(uint8_t* data)
  {
    uint16_t tmp = *reinterpret_cast<uint16_t*>(data);
    tmp = static_cast<uint16_t>((tmp >> 8) | (tmp << 8));
    *reinterpret_cast<uint16_t*>(data) = tmp;
  }


  static inline void endian_swap_4(uint8_t* data)
  {
    uint32_t tmp = *reinterpret_cast<uint32_t*>(data);
    tmp = (tmp >> 16) | (tmp << 16);
    tmp = ((tmp & 0xFF00FF00) >> 8) | ((tmp & 0x00FF00FF) << 8);
    *reinterpret_cast<uint32_t*>(data) = tmp;
  }


  static inline void endian_swap_8(uint8_t* data)
  {
    uint64_t tmp = *reinterpret_cast<uint64_t*>(data);
    tmp = (tmp >> 32) | (tmp << 32);
    tmp = ((tmp & 0xFFFF0000FFFF0000) >> 16) | ((tmp & 0x0000FFFF0000FFFF) << 16);
    tmp = ((tmp & 0xFF00FF00FF00FF00) >> 8) | ((tmp & 0x00FF00FF00FF00FF) << 8);
    *reinterpret_cast<uint64_t*>(data) = tmp;
  }


  static inline void endian_swap(uint8_t* data, PLYPropertyType type)
  {
    switch (kPLYPropertySize[uint32_t(type)]) {
    case 2: endian_swap_2(data); break;
    case 4: endian_swap_4(data); break;
    case 8: endian_swap_8(data); break;
    default: break;
    }
  }


  static inline void endian_swap_array(uint8_t* data, PLYPropertyType type, int n)
  {
    switch (kPLYPropertySize[uint32_t(type)]) {
    case 2:
      for (const uint8_t* end = data + 2 * n; data < end; data += 2) {
        endian_swap_2(data);
      }
      break;
    case 4:
      for (const uint8_t* end = data + 4 * n; data < end; data += 4) {
        endian_swap_4(data);
      }
      break;
    case 8:
      for (const uint8_t* end = data + 8 * n; data < end; data += 8) {
        endian_swap_8(data);
      }
      break;
    default:
      break;
    }
  }


  template <class T>
  static void copy_and_convert_to(T* dest, const uint8_t* src, PLYPropertyType srcType)
  {
    switch (srcType) {
    case PLYPropertyType::Char:   *dest = static_cast<T>(*reinterpret_cast<const int8_t*>(src)); break;
    case PLYPropertyType::UChar:  *dest = static_cast<T>(*reinterpret_cast<const uint8_t*>(src)); break;
    case PLYPropertyType::Short:  *dest = static_cast<T>(*reinterpret_cast<const int16_t*>(src)); break;
    case PLYPropertyType::UShort: *dest = static_cast<T>(*reinterpret_cast<const uint16_t*>(src)); break;
    case PLYPropertyType::Int:    *dest = static_cast<T>(*reinterpret_cast<const int*>(src)); break;
    case PLYPropertyType::UInt:   *dest = static_cast<T>(*reinterpret_cast<const uint32_t*>(src)); break;
    case PLYPropertyType::Float:  *dest = static_cast<T>(*reinterpret_cast<const float*>(src)); break;
    case PLYPropertyType::Double: *dest = static_cast<T>(*reinterpret_cast<const double*>(src)); break;
    case PLYPropertyType::None:   break;
    }
  }


  static void copy_and_convert(uint8_t* dest, PLYPropertyType destType, const uint8_t* src, PLYPropertyType srcType)
  {
    switch (destType) {
    case PLYPropertyType::Char:   copy_and_convert_to(reinterpret_cast<int8_t*>  (dest), src, srcType); break;
    case PLYPropertyType::UChar:  copy_and_convert_to(reinterpret_cast<uint8_t*> (dest), src, srcType); break;
    case PLYPropertyType::Short:  copy_and_convert_to(reinterpret_cast<int16_t*> (dest), src, srcType); break;
    case PLYPropertyType::UShort: copy_and_convert_to(reinterpret_cast<uint16_t*>(dest), src, srcType); break;
    case PLYPropertyType::Int:    copy_and_convert_to(reinterpret_cast<int32_t*> (dest), src, srcType); break;
    case PLYPropertyType::UInt:   copy_and_convert_to(reinterpret_cast<uint32_t*>(dest), src, srcType); break;
    case PLYPropertyType::Float:  copy_and_convert_to(reinterpret_cast<float*>   (dest), src, srcType); break;
    case PLYPropertyType::Double: copy_and_convert_to(reinterpret_cast<double*>  (dest), src, srcType); break;
    case PLYPropertyType::None:   break;
    }
  }


  static inline bool compatible_types(PLYPropertyType srcType, PLYPropertyType destType)
  {
    return (srcType == destType) ||
        (srcType < PLYPropertyType::Float && (uint32_t(srcType) ^ 0x1) == uint32_t(destType));
  }


  //
  // PLYElement methods
  //

  void PLYElement::calculate_offsets()
  {
    fixedSize = true;
    for (PLYProperty& prop : properties) {
      if (prop.countType != PLYPropertyType::None) {
        fixedSize = false;
        break;
      }
    }

    // Note that each list property gets its own separate storage. Only fixed
    // size properties go into the common data block. The `rowStride` is the
    // size of a row in the common data block.
    rowStride = 0;
    for (PLYProperty& prop : properties) {
      if (prop.countType != PLYPropertyType::None) {
        continue;
      }
      prop.offset = rowStride;
      rowStride += kPLYPropertySize[uint32_t(prop.type)];
    }
  }


  uint32_t PLYElement::find_property(const char *propName) const
  {
    for (uint32_t i = 0, endI = uint32_t(properties.size()); i < endI; i++) {
      if (strcmp(propName, properties.at(i).name.c_str()) == 0) {
        return i;
      }
    }
    return kInvalidIndex;
  }


  bool PLYElement::find_properties(uint32_t propIdxs[], uint32_t numIdxs, ...) const
  {
    va_list args;
    va_start(args, numIdxs);
    bool foundAll = find_properties_va(propIdxs, numIdxs, args);
    va_end(args);
    return foundAll;
  }


  bool PLYElement::find_properties_va(uint32_t propIdxs[], uint32_t numIdxs, va_list names) const
  {
    for (uint32_t i = 0; i < numIdxs; i++) {
      propIdxs[i] = find_property(va_arg(names, const char*));
      if (propIdxs[i] == kInvalidIndex) {
        return false;
      }
    }
    return true;
  }


  bool PLYElement::convert_list_to_fixed_size(uint32_t listPropIdx, uint32_t listSize, uint32_t newPropIdxs[])
  {
    if (fixedSize || listPropIdx >= properties.size() || properties[listPropIdx].countType == PLYPropertyType::None) {
      return false;
    }

    PLYProperty oldListProp = properties[listPropIdx];

    // If the generated names are less than 256 chars, we will use an array on
    // the stack as temporary storage. In the rare case that they're longer,
    // we'll allocate an array of sufficient size on the heap and use that
    // instead. This means we'll avoid allocating in all but the most extreme
    // cases.
    char inlineBuf[256];
    size_t nameBufSize = oldListProp.name.size() + 12; // the +12 allows space for an '_', a number up to 10 digits long and the terminating null.
    char* nameBuf = inlineBuf;
    if (nameBufSize > sizeof(inlineBuf)) {
      nameBuf = new char[nameBufSize];
    }

    // Set up a property for the list count column.
    PLYProperty& countProp = properties[listPropIdx];
    snprintf(nameBuf, nameBufSize, "%s_count", oldListProp.name.c_str());
    countProp.name = nameBuf;
    countProp.type = oldListProp.countType;
    countProp.countType = PLYPropertyType::None;
    countProp.stride = kPLYPropertySize[uint32_t(oldListProp.countType)];

    if (listSize > 0) {
      // Set up additional properties for the list entries, 1 per entry.
      if (listPropIdx + 1 == properties.size()) {
        properties.resize(properties.size() + listSize);
      }
      else {
        properties.insert(properties.begin() + listPropIdx + 1, listSize, PLYProperty());
      }

      for (uint32_t i = 0; i < listSize; i++) {
        uint32_t propIdx = listPropIdx + 1 + i;

        PLYProperty& itemProp = properties[propIdx];
        snprintf(nameBuf, sizeof(nameBuf), "%s_%u", oldListProp.name.c_str(), i);
        itemProp.name = nameBuf;
        itemProp.type = oldListProp.type;
        itemProp.countType = PLYPropertyType::None;
        itemProp.stride = kPLYPropertySize[uint32_t(oldListProp.type)];

        newPropIdxs[i] = propIdx;
      }
    }

    if (nameBuf != inlineBuf) {
      delete[] nameBuf;
    }

    calculate_offsets();
    return true;
  }


  //
  // PLYReader methods
  //

  PLYReader::PLYReader(const char* filename)
  {
    m_buf = new char[kPLYReadBufferSize + 1];
    m_buf[kPLYReadBufferSize] = '\0';

    m_tmpBuf = new char[kPLYTempBufferSize + 1];
    m_tmpBuf[kPLYTempBufferSize] = '\0';

    m_bufEnd = m_buf + kPLYReadBufferSize;
    m_pos = m_bufEnd;
    m_end = m_bufEnd;

    if (file_open(&m_f, filename, "rb") != 0) {
      m_f = nullptr;
      m_valid = false;
      return;
    }
    m_valid = true;

    refill_buffer();

    m_valid = keyword("ply") && next_line() &&
              keyword("format") && advance() &&
              typed_which(kPLYFileTypes, &m_fileType) && advance() &&
              int_literal(&m_majorVersion) && advance() &&
              match(".") && advance() &&
              int_literal(&m_minorVersion) && next_line() &&
              parse_elements() &&
              keyword("end_header") && advance() && match("\n") && accept();
    if (!m_valid) {
      return;
    }
    m_inDataSection = true;
    if (m_fileType == PLYFileType::ASCII) {
      advance();
    }

    for (PLYElement& elem : m_elements) {
      elem.calculate_offsets();
    }
  }


  PLYReader::~PLYReader()
  {
    if (m_f != nullptr) {
      fclose(m_f);
    }
    delete[] m_buf;
    delete[] m_tmpBuf;
  }


  bool PLYReader::valid() const
  {
    return m_valid;
  }


  bool PLYReader::has_element() const
  {
    return m_valid && m_currentElement < m_elements.size();
  }


  const PLYElement* PLYReader::element() const
  {
    assert(has_element());
    return &m_elements[m_currentElement];
  }


  bool PLYReader::load_element()
  {
    assert(has_element());
    if (m_elementLoaded) {
      return true;
    }

    PLYElement& elem = m_elements[m_currentElement];
    return elem.fixedSize ? load_fixed_size_element(elem) : load_variable_size_element(elem);
  }


  void PLYReader::next_element()
  {
    if (!has_element()) {
      return;
    }

    // If the element was loaded, the read buffer should already be positioned at
    // the start of the next element.
    PLYElement& elem = m_elements[m_currentElement];
    m_currentElement++;

    if (m_elementLoaded) {
      // Clear any temporary storage used for list properties in the current element.
      for (PLYProperty& prop : elem.properties) {
        if (prop.countType == PLYPropertyType::None) {
          continue;
        }
        prop.listData.clear();
        prop.listData.shrink_to_fit();
        prop.rowCount.clear();
        prop.rowCount.shrink_to_fit();
      }

      // Clear temporary storage for the non-list properties in the current element.
      m_elementData.clear();
      m_elementLoaded = false;
      return;
    }

    // If the element wasn't loaded, we have to move the file pointer past its
    // contents. How we do that depends on whether this is an ASCII or binary
    // file and, if it's a binary, whether the element is fixed or variable
    // size.
    if (m_fileType == PLYFileType::ASCII) {
      for (uint32_t row = 0; row < elem.count; row++) {
        next_line();
      }
    }
    else if (elem.fixedSize) {
      int64_t elementStart = static_cast<int64_t>(m_pos - m_buf);
      int64_t elementSize = elem.rowStride * elem.count;
      int64_t elementEnd = elementStart + elementSize;
      if (elementEnd >= kPLYReadBufferSize) {
        m_bufOffset += elementEnd;
        file_seek(m_f, m_bufOffset, SEEK_SET);
        m_bufEnd = m_buf + kPLYReadBufferSize;
        m_pos = m_bufEnd;
        m_end = m_bufEnd;
        refill_buffer();
      }
      else {
        m_pos = m_buf + elementEnd;
        m_end = m_pos;
      }
    }
    else if (m_fileType == PLYFileType::Binary) {
      for (uint32_t row = 0; row < elem.count; row++) {
        for (const PLYProperty& prop : elem.properties) {
          if (prop.countType == PLYPropertyType::None) {
            uint32_t numBytes = kPLYPropertySize[uint32_t(prop.type)];
            if (m_pos + numBytes > m_bufEnd) {
              if (!refill_buffer() || m_pos + numBytes > m_bufEnd) {
                m_valid = false;
                return;
              }
            }
            m_pos += numBytes;
            m_end = m_pos;
            continue;
          }

          uint32_t numBytes = kPLYPropertySize[uint32_t(prop.countType)];
          if (m_pos + numBytes > m_bufEnd) {
            if (!refill_buffer() || m_pos + numBytes > m_bufEnd) {
              m_valid = false;
              return;
            }
          }

          int count = 0;
          copy_and_convert_to(&count, reinterpret_cast<const uint8_t*>(m_pos), prop.countType);

          if (count < 0) {
            m_valid = false;
            return;
          }

          numBytes += uint32_t(count) * kPLYPropertySize[uint32_t(prop.type)];
          if (m_pos + numBytes > m_bufEnd) {
            if (!refill_buffer() || m_pos + numBytes > m_bufEnd) {
              m_valid = false;
              return;
            }
          }
          m_pos += numBytes;
          m_end = m_pos;
        }
      }
    }
    else { // PLYFileType::BinaryBigEndian
      for (uint32_t row = 0; row < elem.count; row++) {
        for (const PLYProperty& prop : elem.properties) {
          if (prop.countType == PLYPropertyType::None) {
            uint32_t numBytes = kPLYPropertySize[uint32_t(prop.type)];
            if (m_pos + numBytes > m_bufEnd) {
              if (!refill_buffer() || m_pos + numBytes > m_bufEnd) {
                m_valid = false;
                return;
              }
            }
            m_pos += numBytes;
            m_end = m_pos;
            continue;
          }

          uint32_t numBytes = kPLYPropertySize[uint32_t(prop.countType)];
          if (m_pos + numBytes > m_bufEnd) {
            if (!refill_buffer() || m_pos + numBytes > m_bufEnd) {
              m_valid = false;
              return;
            }
          }

          int count = 0;
          uint8_t tmp[8];
          memcpy(tmp, m_pos, numBytes);
          endian_swap(tmp, prop.countType);
          copy_and_convert_to(&count, tmp, prop.countType);

          if (count < 0) {
            m_valid = false;
            return;
          }

          numBytes += uint32_t(count) * kPLYPropertySize[uint32_t(prop.type)];
          if (m_pos + numBytes > m_bufEnd) {
            if (!refill_buffer() || m_pos + numBytes > m_bufEnd) {
              m_valid = false;
              return;
            }
          }

          m_pos += numBytes;
          m_end = m_pos;
        }
      }
    }
  }


  PLYFileType PLYReader::file_type() const
  {
    return m_fileType;
  }


  int PLYReader::version_major() const
  {
    return m_majorVersion;
  }


  int PLYReader::version_minor() const
  {
    return m_minorVersion;
  }


  uint32_t PLYReader::num_elements() const
  {
    return m_valid ? static_cast<uint32_t>(m_elements.size()) : 0;
  }


  uint32_t PLYReader::find_element(const char* name) const
  {
    for (uint32_t i = 0, endI = num_elements(); i < endI; i++) {
      const PLYElement& elem = m_elements[i];
      if (strcmp(elem.name.c_str(), name) == 0) {
        return i;
      }
    }
    return kInvalidIndex;
  }


  PLYElement* PLYReader::get_element(uint32_t idx)
  {
    return (idx < num_elements()) ? &m_elements[idx] : nullptr;
  }


  bool PLYReader::element_is(const char* name) const
  {
    return has_element() && strcmp(element()->name.c_str(), name) == 0;
  }


  uint32_t PLYReader::num_rows() const
  {
    return has_element() ? element()->count : 0;
  }


  uint32_t PLYReader::find_property(const char* name) const
  {
    return has_element() ? element()->find_property(name) : kInvalidIndex;
  }


  bool PLYReader::find_properties(uint32_t propIdxs[], uint32_t numIdxs, ...) const
  {
    if (!has_element()) {
      return false;
    }
    va_list args;
    va_start(args, numIdxs);
    bool foundAll = element()->find_properties_va(propIdxs, numIdxs, args);
    va_end(args);
    return foundAll;
  }


  bool PLYReader::extract_properties(const uint32_t propIdxs[], uint32_t numProps, PLYPropertyType destType, void *dest) const
  {
    if (numProps == 0) {
      return false;
    }

    const PLYElement* elem = element();

    // Make sure all property indexes are valid and that none of the properties
    // are lists (this function only extracts non-list data).
    for (uint32_t i = 0; i < numProps; i++) {
      if (propIdxs[i] >= elem->properties.size()) {
        return false;
      }
    }

    // Find out whether we have contiguous columns. If so, we may be able to
    // use a more efficient data extraction technique.
    bool contiguousCols = true;
    uint32_t expectedOffset = elem->properties[propIdxs[0]].offset;
    for (uint32_t i = 0; i < numProps; i++) {
      uint32_t propIdx = propIdxs[i];
      const PLYProperty& prop = elem->properties[propIdx];
      if (prop.offset != expectedOffset) {
        contiguousCols = false;
        break;
      }
      expectedOffset = prop.offset + kPLYPropertySize[uint32_t(prop.type)];
    }

    // If the row we're extracting is contiguous in memory (i.e. there are no
    // gaps anywhere in a row - start, end or middle), we can use an even MORE
    // efficient data extraction technique.
    bool contiguousRows = contiguousCols &&
                          (elem->properties[propIdxs[0]].offset == 0) &&
                          (expectedOffset == elem->rowStride);

    // If no data conversion is required, we can memcpy chunks of data
    // directly over to `dest`. How big those chunks will be depends on whether
    // the columns and/or rows are contiguous, as determined above.
    bool conversionRequired = false;
    for (uint32_t i = 0; i < numProps; i++) {
      uint32_t propIdx = propIdxs[i];
      const PLYProperty& prop = elem->properties[propIdx];
      if (!compatible_types(prop.type, destType)) {
        conversionRequired = true;
        break;
      }
    }

    uint8_t* to = reinterpret_cast<uint8_t*>(dest);
    if (!conversionRequired) {
      // If no data conversion is required, we can just use memcpy to get
      // values into dest.
      if (contiguousRows) {
        // Most efficient case is when the rows are contiguous. It means we're
        // simply copying the entire data block for this element, which we can
        // do with a single memcpy.
        std::memcpy(to, m_elementData.data(), m_elementData.size());
      }
      else if (contiguousCols) {
        // If the rows aren't contiguous, but the columns we're extracting
        // within each row are, then we can do a single memcpy per row.
        const uint8_t* from = m_elementData.data() + elem->properties[propIdxs[0]].offset;
        const uint8_t* end = m_elementData.data() + m_elementData.size();
        const size_t numBytes = expectedOffset - elem->properties[propIdxs[0]].offset;
        while (from < end) {
          std::memcpy(to, from, numBytes);
          from += elem->rowStride;
          to += numBytes;
        }
      }
      else {
        // If the columns aren't contiguous, we must memcpy each one separately.
        const uint8_t* row = m_elementData.data();
        const uint8_t* end = m_elementData.data() + m_elementData.size();
        uint8_t* to = reinterpret_cast<uint8_t*>(dest);
        size_t colBytes = kPLYPropertySize[uint32_t(destType)]; // size of an output column in bytes.
        while (row < end) {
          for (uint32_t i = 0; i < numProps; i++) {
            uint32_t propIdx = propIdxs[i];
            const PLYProperty& prop = elem->properties[propIdx];
            std::memcpy(to, row + prop.offset, colBytes);
            to += colBytes;
          }
          row += elem->rowStride;
        }
      }
    }
    else {
      // We will have to do data type conversions on the column values here. We
      // cannot simply use memcpy in this case, every column has to be
      // processed separately.
      const uint8_t* row = m_elementData.data();
      const uint8_t* end = m_elementData.data() + m_elementData.size();
      uint8_t* to = reinterpret_cast<uint8_t*>(dest);
      size_t colBytes = kPLYPropertySize[uint32_t(destType)]; // size of an output column in bytes.
      while (row < end) {
        for (uint32_t i = 0; i < numProps; i++) {
          uint32_t propIdx = propIdxs[i];
          const PLYProperty& prop = elem->properties[propIdx];
          copy_and_convert(to, destType, row + prop.offset, prop.type);
          to += colBytes;
        }
        row += elem->rowStride;
      }
    }

    return true;
  }


  const uint32_t* PLYReader::get_list_counts(uint32_t propIdx) const
  {
    if (!has_element() || propIdx >= element()->properties.size() || element()->properties[propIdx].countType == PLYPropertyType::None) {
      return nullptr;
    }
    return element()->properties[propIdx].rowCount.data();
  }


  const uint8_t* PLYReader::get_list_data(uint32_t propIdx) const
  {
    if (!has_element() || propIdx >= element()->properties.size() || element()->properties[propIdx].countType == PLYPropertyType::None) {
      return nullptr;
    }
    return element()->properties[propIdx].listData.data();
  }


  bool PLYReader::extract_list_property(uint32_t propIdx, PLYPropertyType destType, void *dest) const
  {
    if (!has_element() || propIdx >= element()->properties.size() || element()->properties[propIdx].countType == PLYPropertyType::None) {
      return false;
    }

    const PLYProperty& prop = element()->properties[propIdx];
    if (compatible_types(prop.type, destType)) {
      // If no type conversion is required, we can just copy the list data
      // directly over with a single memcpy.
      std::memcpy(dest, prop.listData.data(), prop.listData.size());
    }
    else {
      // If type conversion is required we'll have to process each list value separately.
      const uint8_t* from = prop.listData.data();
      const uint8_t* end = prop.listData.data() + prop.listData.size();
      uint8_t* to = reinterpret_cast<uint8_t*>(dest);
      const size_t toBytes = kPLYPropertySize[uint32_t(destType)];
      const size_t fromBytes = kPLYPropertySize[uint32_t(prop.type)];
      while (from < end) {
        copy_and_convert(to, destType, from, prop.type);
        to += toBytes;
        from += fromBytes;
      }
    }

    return true;
  }


  uint32_t PLYReader::num_triangles(uint32_t propIdx) const
  {
    const uint32_t* counts = get_list_counts(propIdx);
    if (counts == nullptr) {
      return 0;
    }

    const uint32_t numRows = element()->count;
    uint32_t num = 0;
    for (uint32_t i = 0; i < numRows; i++) {
      if (counts[i] >= 3) {
        num += counts[i] - 2;
      }
    }
    return num;
  }


  bool PLYReader::requires_triangulation(uint32_t propIdx) const
  {
    const uint32_t* counts = get_list_counts(propIdx);
    if (counts == nullptr) {
      return false;
    }

    const uint32_t numRows = element()->count;
    for (uint32_t i = 0; i < numRows; i++) {
      if (counts[i] != 3) {
        return true;
      }
    }
    return false;
  }


  bool PLYReader::extract_triangles(uint32_t propIdx, const float pos[], uint32_t numVerts, PLYPropertyType destType, void *dest) const
  {
    if (!requires_triangulation(propIdx)) {
      return extract_list_property(propIdx, destType, dest);
    }

    const PLYElement* elem = element();
    const PLYProperty& prop = elem->properties[propIdx];

    const uint32_t* counts = prop.rowCount.data();
    const uint8_t*  data   = prop.listData.data();

    uint8_t* to = reinterpret_cast<uint8_t*>(dest);

    bool convertSrc = !compatible_types(elem->properties[propIdx].type, PLYPropertyType::Int);
    bool convertDst = !compatible_types(PLYPropertyType::Int, destType);

    size_t srcValBytes  = kPLYPropertySize[uint32_t(prop.type)];
    size_t destValBytes = kPLYPropertySize[uint32_t(destType)];

    if (convertSrc && convertDst) {
      std::vector<int> faceIndices, triIndices;
      faceIndices.reserve(32);
      triIndices.reserve(64);
      const uint8_t* face = data;
      for (uint32_t faceIdx = 0; faceIdx < elem->count; faceIdx++) {
        const uint8_t* faceEnd = face + srcValBytes * counts[faceIdx];
        faceIndices.clear();
        faceIndices.reserve(counts[faceIdx]);
        for (; face < faceEnd; face += srcValBytes) {
          int idx = -1;
          copy_and_convert_to(&idx, face, prop.type);
          faceIndices.push_back(idx);
        }

        triIndices.resize((counts[faceIdx] - 2) * 3);
        triangulate_polygon(counts[faceIdx], pos, numVerts, faceIndices.data(), triIndices.data());
        for (int idx : triIndices) {
          copy_and_convert(to, destType, reinterpret_cast<const uint8_t*>(&idx), PLYPropertyType::Int);
          to += destValBytes;
        }
      }
    }
    else if (convertSrc) {
      std::vector<int> faceIndices;
      faceIndices.reserve(32);
      const uint8_t* face = data;
      for (uint32_t faceIdx = 0; faceIdx < elem->count; faceIdx++) {
        const uint8_t* faceEnd = face + srcValBytes * counts[faceIdx];
        faceIndices.clear();
        faceIndices.reserve(counts[faceIdx]);
        for (; face < faceEnd; face += srcValBytes) {
          int idx = -1;
          copy_and_convert_to(&idx, face, prop.type);
          faceIndices.push_back(idx);
        }

        uint32_t numTris = triangulate_polygon(counts[faceIdx], pos, numVerts, faceIndices.data(), reinterpret_cast<int*>(to));
        to += numTris * 3 * destValBytes;
      }
    }
    else if (convertDst) {
      std::vector<int> triIndices;
      triIndices.reserve(64);
      const uint8_t* face = data;
      for (uint32_t faceIdx = 0; faceIdx < elem->count; faceIdx++) {
        triIndices.resize((counts[faceIdx] - 2) * 3);
        triangulate_polygon(counts[faceIdx], pos, numVerts, reinterpret_cast<const int*>(face), triIndices.data());
        for (int idx : triIndices) {
          copy_and_convert(to, destType, reinterpret_cast<const uint8_t*>(&idx), PLYPropertyType::Int);
          to += destValBytes;
        }
        face += srcValBytes * counts[faceIdx];
      }
    }
    else {
      const uint8_t* face = data;
      for (uint32_t faceIdx = 0; faceIdx < elem->count; faceIdx++) {
        uint32_t numTris = triangulate_polygon(counts[faceIdx], pos, numVerts, reinterpret_cast<const int*>(face), reinterpret_cast<int*>(to));
        face += counts[faceIdx] * srcValBytes;
        to += numTris * 3 * destValBytes;
      }
    }

    return true;
  }


  bool PLYReader::find_pos(uint32_t propIdxs[3]) const
  {
    return find_properties(propIdxs, 3, "x", "y", "z");
  }


  bool PLYReader::find_normal(uint32_t propIdxs[3]) const
  {
    return find_properties(propIdxs, 3, "nx", "ny", "nz");
  }


  bool PLYReader::find_texcoord(uint32_t propIdxs[2]) const
  {
    return find_properties(propIdxs, 2, "u", "v") ||
           find_properties(propIdxs, 2, "s", "t") ||
           find_properties(propIdxs, 2, "texture_u", "texture_v") ||
           find_properties(propIdxs, 2, "texture_s", "texture_t");
  }


  bool PLYReader::find_color(uint32_t propIdxs[3]) const
  {
    return find_properties(propIdxs, 3, "r", "g", "b");
  }


  bool PLYReader::find_indices(uint32_t propIdxs[1]) const
  {
    return find_properties(propIdxs, 1, "vertex_indices");
  }


  //
  // PLYReader private methods
  //

  bool PLYReader::refill_buffer()
  {
    if (m_f == nullptr || m_atEOF) {
      // Nothing left to read.
      return false;
    }

    if (m_pos == m_buf && m_end == m_bufEnd) {
      // Can't make any more room in the buffer!
      return false;
    }

    // Move everything from the start of the current token onwards, to the
    // start of the read buffer.
    int64_t bufSize = static_cast<int64_t>(m_bufEnd - m_buf);
    if (bufSize < kPLYReadBufferSize) {
      m_buf[bufSize] = m_buf[kPLYReadBufferSize];
      m_buf[kPLYReadBufferSize] = '\0';
      m_bufEnd = m_buf + kPLYReadBufferSize;
    }
    size_t keep = static_cast<size_t>(m_bufEnd - m_pos);
    if (keep > 0 && m_pos > m_buf) {
      std::memmove(m_buf, m_pos, sizeof(char) * keep);
      m_bufOffset += static_cast<int64_t>(m_pos - m_buf);
    }
    m_end = m_buf + (m_end - m_pos);
    m_pos = m_buf;

    // Fill the remaining space in the buffer with data from the file.
    size_t fetched = fread(m_buf + keep, sizeof(char), kPLYReadBufferSize - keep, m_f) + keep;
    m_atEOF = fetched < kPLYReadBufferSize;
    m_bufEnd = m_buf + fetched;

    if (!m_inDataSection || m_fileType == PLYFileType::ASCII) {
      return rewind_to_safe_char();
    }
    return true;
  }


  bool PLYReader::rewind_to_safe_char()
  {
    // If it looks like a token might run past the end of this buffer, move
    // the buffer end pointer back before it & rewind the file. This way the
    // next refill will pick up the whole of the token.
    if (!m_atEOF && (m_bufEnd[-1] == '\n' || !is_safe_buffer_end(m_bufEnd[-1]))) {
      const char* safe = m_bufEnd - 2;
      // If '\n' is the last char in the buffer, then a call to `next_line()`
      // will move `m_pos` to point at the null terminator but won't refresh
      // the buffer. It would be clearer to fix this in `next_line()` but I
      // believe it'll be more performant to simply treat `\n` as an unsafe
      // character here.
      while (safe >= m_end && (*safe == '\n' || !is_safe_buffer_end(*safe))) {
        --safe;
      }
      if (safe < m_end) {
        // No safe places to rewind to in the whole buffer!
        return false;
      }
      ++safe;
      m_buf[kPLYReadBufferSize] = *safe;
      m_bufEnd = safe;
    }
    m_buf[m_bufEnd - m_buf] = '\0';

    return true;
  }


  bool PLYReader::accept()
  {
    m_pos = m_end;
    return true;
  }


  // Advances to end of line or to next non-whitespace char.
  bool PLYReader::advance()
  {
    m_pos = m_end;
    while (true) {
      while (is_whitespace(*m_pos)) {
        ++m_pos;
      }
      if (m_pos == m_bufEnd) {
        m_end = m_pos;
        if (refill_buffer()) {
          continue;
        }
        return false;
      }
      break;
    }
    m_end = m_pos;
    return true;
  }


  bool PLYReader::next_line()
  {
    m_pos = m_end;
    do {
      while (*m_pos != '\n') {
        if (m_pos == m_bufEnd) {
          m_end = m_pos;
          if (refill_buffer()) {
            continue;
          }
          return false;
        }
        ++m_pos;
      }
      ++m_pos; // move past the newline char
      m_end = m_pos;
    } while (match("comment") || match("obj_info"));

    return true;
  }


  bool PLYReader::match(const char* str)
  {
    m_end = m_pos;
    while (m_end < m_bufEnd && *str != '\0' && *m_end == *str) {
      ++m_end;
      ++str;
    }
    if (*str != '\0') {
      return false;
    }
    return true;
  }


  bool PLYReader::which(const char* values[], uint32_t* index)
  {
    for (uint32_t i = 0; values[i] != nullptr; i++) {
      if (keyword(values[i])) {
        *index = i;
        return true;
      }
    }
    return false;
  }


  bool PLYReader::which_property_type(PLYPropertyType* type)
  {
    for (uint32_t i = 0; kTypeAliases[i].name != nullptr; i++) {
      if (keyword(kTypeAliases[i].name)) {
        *type = kTypeAliases[i].type;
        return true;
      }
    }
    return false;
  }


  bool PLYReader::keyword(const char* kw)
  {
    return match(kw) && !is_keyword_part(*m_end);
  }


  bool PLYReader::identifier(char* dest, size_t destLen)
  {
    m_end = m_pos;
    if (!is_keyword_start(*m_end) || destLen == 0) {
      return false;
    }
    do {
      ++m_end;
    } while (is_keyword_part(*m_end));

    size_t len = static_cast<size_t>(m_end - m_pos);
    if (len >= destLen) {
      return false; // identifier too large for dest!
    }
    std::memcpy(dest, m_pos, sizeof(char) * len);
    dest[len] = '\0';
    return true;
  }


  bool PLYReader::int_literal(int* value)
  {
    return miniply::int_literal(m_pos, &m_end, value);
  }


  bool PLYReader::float_literal(float* value)
  {
    return miniply::float_literal(m_pos, &m_end, value);
  }


  bool PLYReader::double_literal(double* value)
  {
    return miniply::double_literal(m_pos, &m_end, value);
  }


  bool PLYReader::parse_elements()
  {
    m_elements.reserve(4);
    while (m_valid && keyword("element")) {
      parse_element();
    }
    return true;
  }


  bool PLYReader::parse_element()
  {
    int count = 0;

    m_valid = keyword("element") && advance() &&
              identifier(m_tmpBuf, kPLYTempBufferSize) && advance() &&
              int_literal(&count) && next_line();
    if (!m_valid || count < 0) {
      return false;
    }

    m_elements.push_back(PLYElement());
    PLYElement& elem = m_elements.back();
    elem.name = m_tmpBuf;
    elem.count = static_cast<uint32_t>(count);
    elem.properties.reserve(10);

    while (m_valid && keyword("property")) {
      parse_property(elem.properties);
    }

    return true;
  }


  bool PLYReader::parse_property(std::vector<PLYProperty>& properties)
  {
    PLYPropertyType type      = PLYPropertyType::None;
    PLYPropertyType countType = PLYPropertyType::None;

    m_valid = keyword("property") && advance();
    if (!m_valid) {
      return false;
    }

    if (keyword("list")) {
      // This is a list property
      m_valid = advance() && which_property_type(&countType) && advance();
      if (!m_valid) {
        return false;
      }
    }

    m_valid = which_property_type(&type) && advance() &&
              identifier(m_tmpBuf, kPLYTempBufferSize) && next_line();
    if (!m_valid) {
      return false;
    }

    properties.push_back(PLYProperty());
    PLYProperty& prop = properties.back();
    prop.name = m_tmpBuf;
    prop.type = type;
    prop.countType = countType;

    return true;
  }


  bool PLYReader::load_fixed_size_element(PLYElement& elem)
  {
    size_t numBytes = elem.count * elem.rowStride;

    m_elementData.resize(numBytes);

    if (m_fileType == PLYFileType::ASCII) {
      size_t back = 0;

      for (uint32_t row = 0; row < elem.count; row++) {
        for (PLYProperty& prop : elem.properties) {
          if (!load_ascii_scalar_property(prop, back)) {
            m_valid = false;
            return false;
          }
        }
        next_line();
      }
    }
    else {
      uint8_t* dst = m_elementData.data();
      uint8_t* dstEnd = dst + numBytes;
      while (dst < dstEnd) {
        size_t bytesAvailable = static_cast<size_t>(m_bufEnd - m_pos);
        if (dst + bytesAvailable > dstEnd) {
          bytesAvailable = static_cast<size_t>(dstEnd - dst);
        }
        std::memcpy(dst, m_pos, bytesAvailable);
        m_pos += bytesAvailable;
        m_end = m_pos;
        dst += bytesAvailable;
        if (!refill_buffer()) {
          break;
        }
      }
      if (dst < dstEnd) {
        m_valid = false;
        return false;
      }

      // We assume the CPU is little endian, so if the file is big-endian we
      // need to do an endianness swap on every data item in the block.
      if (m_fileType == PLYFileType::BinaryBigEndian) {
        uint8_t* data = m_elementData.data();
        for (uint32_t row = 0; row < elem.count; row++) {
          for (PLYProperty& prop : elem.properties) {
            size_t numBytes = kPLYPropertySize[uint32_t(prop.type)];
            switch (numBytes) {
            case 2:
              endian_swap_2(data);
              break;
            case 4:
              endian_swap_4(data);
              break;
            case 8:
              endian_swap_8(data);
              break;
            default:
              break;
            }
            data += numBytes;
          }
        }
      }
    }

    m_elementLoaded = true;
    return true;
  }


  bool PLYReader::load_variable_size_element(PLYElement& elem)
  {
    m_elementData.resize(elem.count * elem.rowStride);

    // Preallocate enough space for each row in the property to contain three
    // items. This is based on the assumptions that (a) the most common use for
    // list properties is vertex indices; and (b) most faces are triangles.
    // This gives a performance boost because we won't have to grow the
    // listData vector as many times during loading.
    for (PLYProperty& prop : elem.properties) {
      if (prop.countType != PLYPropertyType::None) {
        prop.listData.reserve(elem.count * kPLYPropertySize[uint32_t(prop.type)] * 3);
      }
    }

    if (m_fileType == PLYFileType::Binary) {
      size_t back = 0;
      for (uint32_t row = 0; row < elem.count; row++) {
        for (PLYProperty& prop : elem.properties) {
          if (prop.countType == PLYPropertyType::None) {
            m_valid = load_binary_scalar_property(prop, back);
          }
          else {
            load_binary_list_property(prop);
          }
        }
      }
    }
    else if (m_fileType == PLYFileType::ASCII) {
      size_t back = 0;
      for (uint32_t row = 0; row < elem.count; row++) {
        for (PLYProperty& prop : elem.properties) {
          if (prop.countType == PLYPropertyType::None) {
            m_valid = load_ascii_scalar_property(prop, back);
          }
          else {
            load_ascii_list_property(prop);
          }
        }
        next_line();
      }
    }
    else { // m_fileType == PLYFileType::BinaryBigEndian
      size_t back = 0;
      for (uint32_t row = 0; row < elem.count; row++) {
        for (PLYProperty& prop : elem.properties) {
          if (prop.countType == PLYPropertyType::None) {
            m_valid = load_binary_scalar_property_big_endian(prop, back);
          }
          else {
            load_binary_list_property_big_endian(prop);
          }
        }
      }
    }

    m_elementLoaded = true;
    return true;
  }


  bool PLYReader::load_ascii_scalar_property(PLYProperty& prop, size_t& destIndex)
  {
    uint8_t value[8];
    if (!ascii_value(prop.type, value)) {
      return false;
    }

    size_t numBytes = kPLYPropertySize[uint32_t(prop.type)];
    std::memcpy(m_elementData.data() + destIndex, value, numBytes);
    destIndex += numBytes;
    return true;
  }


  bool PLYReader::load_ascii_list_property(PLYProperty& prop)
  {
    int count = 0;
    m_valid = (prop.countType < PLYPropertyType::Float) && int_literal(&count) && advance() && (count >= 0);
    if (!m_valid) {
      return false;
    }

    const size_t numBytes = kPLYPropertySize[uint32_t(prop.type)];

    size_t back = prop.listData.size();
    prop.rowCount.push_back(static_cast<uint32_t>(count));
    prop.listData.resize(back + numBytes * size_t(count));

    for (uint32_t i = 0; i < uint32_t(count); i++) {
      if (!ascii_value(prop.type, prop.listData.data() + back)) {
        m_valid = false;
        return false;
      }
      back += numBytes;
    }

    return true;
  }


  bool PLYReader::load_binary_scalar_property(PLYProperty& prop, size_t& destIndex)
  {
    size_t numBytes = kPLYPropertySize[uint32_t(prop.type)];
    if (m_pos + numBytes > m_bufEnd) {
      if (!refill_buffer() || m_pos + numBytes > m_bufEnd) {
        m_valid = false;
        return false;
      }
    }
    std::memcpy(m_elementData.data() + destIndex, m_pos, numBytes);
    m_pos += numBytes;
    m_end = m_pos;
    destIndex += numBytes;
    return true;
  }


  bool PLYReader::load_binary_list_property(PLYProperty& prop)
  {
    size_t countBytes = kPLYPropertySize[uint32_t(prop.countType)];
    if (m_pos + countBytes > m_bufEnd) {
      if (!refill_buffer() || m_pos + countBytes > m_bufEnd) {
        m_valid = false;
        return false;
      }
    }

    int count = 0;
    copy_and_convert_to(&count, reinterpret_cast<const uint8_t*>(m_pos), prop.countType);

    if (count < 0) {
      m_valid = false;
      return false;
    }

    m_pos += countBytes;
    m_end = m_pos;

    const size_t listBytes = kPLYPropertySize[uint32_t(prop.type)] * uint32_t(count);
    if (m_pos + listBytes > m_bufEnd) {
      if (!refill_buffer() || m_pos + listBytes > m_bufEnd) {
        m_valid = false;
        return false;
      }
    }
    size_t back = prop.listData.size();
    prop.rowCount.push_back(static_cast<uint32_t>(count));
    prop.listData.resize(back + listBytes);
    std::memcpy(prop.listData.data() + back, m_pos, listBytes);

    m_pos += listBytes;
    m_end = m_pos;
    return true;
  }


  bool PLYReader::load_binary_scalar_property_big_endian(PLYProperty &prop, size_t &destIndex)
  {
    size_t startIndex = destIndex;
    if (load_binary_scalar_property(prop, destIndex)) {
      endian_swap(m_elementData.data() + startIndex, prop.type);
      return true;
    }
    else {
      return false;
    }
  }


  bool PLYReader::load_binary_list_property_big_endian(PLYProperty &prop)
  {
    size_t countBytes = kPLYPropertySize[uint32_t(prop.countType)];
    if (m_pos + countBytes > m_bufEnd) {
      if (!refill_buffer() || m_pos + countBytes > m_bufEnd) {
        m_valid = false;
        return false;
      }
    }

    int count = 0;
    uint8_t tmp[8];
    std::memcpy(tmp, m_pos, countBytes);
    endian_swap(tmp, prop.countType);
    copy_and_convert_to(&count, tmp, prop.countType);

    if (count < 0) {
      m_valid = false;
      return false;
    }

    m_pos += countBytes;
    m_end = m_pos;

    const size_t typeBytes = kPLYPropertySize[uint32_t(prop.type)];
    const size_t listBytes = typeBytes * uint32_t(count);
    if (m_pos + listBytes > m_bufEnd) {
      if (!refill_buffer() || m_pos + listBytes > m_bufEnd) {
        m_valid = false;
        return false;
      }
    }
    size_t back = prop.listData.size();
    prop.rowCount.push_back(static_cast<uint32_t>(count));
    prop.listData.resize(back + listBytes);

    uint8_t* list = prop.listData.data() + back;
    std::memcpy(list, m_pos, listBytes);
    endian_swap_array(list, prop.type, count);

    m_pos += listBytes;
    m_end = m_pos;
    return true;
  }


  bool PLYReader::ascii_value(PLYPropertyType propType, uint8_t value[8])
  {
    int tmpInt = 0;

    switch (propType) {
    case PLYPropertyType::Char:
    case PLYPropertyType::UChar:
    case PLYPropertyType::Short:
    case PLYPropertyType::UShort:
      m_valid = int_literal(&tmpInt);
      break;
    case PLYPropertyType::Int:
    case PLYPropertyType::UInt:
      m_valid = int_literal(reinterpret_cast<int*>(value));
      break;
    case PLYPropertyType::Float:
      m_valid = float_literal(reinterpret_cast<float*>(value));
      break;
    case PLYPropertyType::Double:
    default:
      m_valid = double_literal(reinterpret_cast<double*>(value));
      break;
    }

    if (!m_valid) {
      return false;
    }
    advance();

    switch (propType) {
    case PLYPropertyType::Char:
      reinterpret_cast<int8_t*>(value)[0] = static_cast<int8_t>(tmpInt);
      break;
    case PLYPropertyType::UChar:
      value[0] = static_cast<uint8_t>(tmpInt);
      break;
    case PLYPropertyType::Short:
      reinterpret_cast<int16_t*>(value)[0] = static_cast<int16_t>(tmpInt);
      break;
    case PLYPropertyType::UShort:
      reinterpret_cast<uint16_t*>(value)[0] = static_cast<uint16_t>(tmpInt);
      break;
    default:
      break;
    }
    return true;
  }


  //
  // Polygon triangulation
  //

  static float angle_at_vert(uint32_t idx,
                             const std::vector<Vec2>& points2D,
                             const std::vector<uint32_t>& prev,
                             const std::vector<uint32_t>& next)
  {
    Vec2 xaxis = normalize(points2D[next[idx]] - points2D[idx]);
    Vec2 yaxis = Vec2{-xaxis.y, xaxis.x};
    Vec2 p2p0 = points2D[prev[idx]] - points2D[idx];
    float angle = std::atan2(dot(p2p0, yaxis), dot(p2p0, xaxis));
    if (angle <= 0.0f || angle >= kPi) {
      angle = 10000.0f;
    }
    return angle;
  }


  uint32_t triangulate_polygon(uint32_t n, const float pos[], uint32_t numVerts, const int indices[], int dst[])
  {
    if (n < 3) {
      return 0;
    }
    else if (n == 3) {
      dst[0] = indices[0];
      dst[1] = indices[1];
      dst[2] = indices[2];
      return 1;
    }
    else if (n == 4) {
      dst[0] = indices[0];
      dst[1] = indices[1];
      dst[2] = indices[3];

      dst[3] = indices[2];
      dst[4] = indices[3];
      dst[5] = indices[1];
      return 2;
    }

    // Check that all indices for this face are in the valid range before we
    // try to dereference them.
    for (uint32_t i = 0; i < n; i++) {
      if (indices[i] < 0 || uint32_t(indices[i]) >= numVerts) {
        return 0;
      }
    }

    const Vec3* vpos = reinterpret_cast<const Vec3*>(pos);

    // Calculate the geometric normal of the face
    Vec3 origin = vpos[indices[0]];
    Vec3 faceU = normalize(vpos[indices[1]] - origin);
    Vec3 faceNormal = normalize(cross(faceU, normalize(vpos[indices[n - 1]] - origin)));
    Vec3 faceV = normalize(cross(faceNormal, faceU));

    // Project the faces points onto the plane perpendicular to the normal.
    std::vector<Vec2> points2D(n, Vec2{0.0f, 0.0f});
    for (uint32_t i = 1; i < n; i++) {
      Vec3 p = vpos[indices[i]] - origin;
      points2D[i] = Vec2{dot(p, faceU), dot(p, faceV)};
    }

    std::vector<uint32_t> next(n, 0u);
    std::vector<uint32_t> prev(n, 0u);
    uint32_t first = 0;
    for (uint32_t i = 0, j = n - 1; i < n; i++) {
      next[j] = i;
      prev[i] = j;
      j = i;
    }

    // Do ear clipping.
    while (n > 3) {
      // Find the (remaining) vertex with the sharpest angle.
      uint32_t bestI = first;
      float bestAngle = angle_at_vert(first, points2D, prev, next);
      for (uint32_t i = next[first]; i != first; i = next[i]) {
        float angle = angle_at_vert(i, points2D, prev, next);
        if (angle < bestAngle) {
          bestI = i;
          bestAngle = angle;
        }
      }

      // Clip the triangle at bestI.
      uint32_t nextI = next[bestI];
      uint32_t prevI = prev[bestI];

      dst[0] = indices[bestI];
      dst[1] = indices[nextI];
      dst[2] = indices[prevI];
      dst += 3;

      if (bestI == first) {
        first = nextI;
      }
      next[prevI] = nextI;
      prev[nextI] = prevI;
      --n;
    }

    // Add the final triangle.
    dst[0] = indices[first];
    dst[1] = indices[next[first]];
    dst[2] = indices[prev[first]];

    return n - 2;
  }

} // namespace miniply


namespace minipbrt {

  //
  // Statements
  //

  enum class StatementID {
    // Common statements
    Identity,
    Translate,
    Scale,
    Rotate,
    LookAt,
    CoordinateSystem,
    CoordSysTransform,
    Transform,
    ConcatTransform,
    ActiveTransform,
    MakeNamedMedium,
    MediumInterface,
    Include,
    // World-only statements
    AttributeBegin,
    AttributeEnd,
    Shape,
    AreaLightSource,
    LightSource,
    Material,
    MakeNamedMaterial,
    NamedMaterial,
    ObjectBegin,
    ObjectEnd,
    ObjectInstance,
    Texture,
    TransformBegin,
    TransformEnd,
    ReverseOrientation,
    WorldEnd,
    // Preamble-only statements
    Accelerator,
    Camera,
    Film,
    Integrator,
    PixelFilter,
    Sampler,
    TransformTimes,
    WorldBegin,
  };


  struct StatementDeclaration {
    StatementID id;
    const char* name;        // Name of the directive. This is what we will match against in the file.
    const char* argPattern;  // Each char is the type of a required argument: 'e' = enum string, 's' = string, 'f' = float, anything else is an error.
    bool inPreamble;
    bool inWorld;
    const char** enum0;
    const char** enum1;
    int enum0default;
    int enum1default;
  };


  static const char* kActiveTransformValues[] = { "StartTime", "EndTime", "All", nullptr };
  static const char* kShapeTypes[] = { "cone", "curve", "cylinder", "disk", "hyperboloid", "paraboloid", "sphere", "trianglemesh", "heightfield", "loopsubdiv", "nurbs", "plymesh", nullptr };
  static const char* kAreaLightTypes[] = { "diffuse", nullptr };
  static const char* kLightTypes[] = { "distant", "goniometric", "infinite", "point", "projection", "spot", nullptr };
  static const char* kMaterialTypes[] = { "disney", "fourier", "glass", "hair", "kdsubsurface", "matte", "metal", "mirror", "mix", "none", "plastic", "substrate", "subsurface", "translucent", "uber", "", nullptr };
  static const char* kTextureDataTypes[] = { "float", "spectrum", "color", nullptr };
  // Note that checkerboard appears twice below, because there are 2D and 3D versions of it.
  static const char* kTextureTypes[] = { "bilerp", "checkerboard", "checkerboard", "constant", "dots", "fbm", "imagemap", "marble", "mix", "scale", "uv", "windy", "wrinkled", "ptex", nullptr };
  static const char* kAccelTypes[] = { "bvh", "kdtree", nullptr };
  static const char* kCameraTypes[] = { "perspective", "orthographic", "environment", "realistic", nullptr };
  static const char* kFilmTypes[] = { "image", nullptr };
  static const char* kIntegratorTypes[] = { "bdpt", "directlighting", "mlt", "path", "sppm", "whitted", "volpath", "ambientocclusion", nullptr };
  static const char* kPixelFilterTypes[] = { "box", "gaussian", "mitchell", "sinc", "triangle", nullptr };
  static const char* kSamplerTypes[] = { "02sequence", "lowdiscrepancy", "halton", "maxmindist", "random", "sobol", "stratified", nullptr };
  static const char* kMediumTypes[] = { "homogeneous", "heterogeneous", nullptr };

  static const char* kLightSampleStrategies[] = { "uniform", "power", "spatial", nullptr };
  static const char* kDirectLightSampleStrategies[] = { "uniform", "power", "spatial", nullptr };

  static const char* kTexCoordMappings[] = { "uv", "spherical", "cylindrical", "planar", nullptr };
  static const char* kCheckerboardAAModes[] = { "closedform", "none", nullptr };
  static const char* kWrapModes[] = { "repeat", "black", "clamp", nullptr };


  static const StatementDeclaration kStatements[] = {
    // Common statements, can appear in both preamble and world section.
    { StatementID::Identity,          "Identity",            "",                 true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::Translate,         "Translate",           "fff",              true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::Scale,             "Scale",               "fff",              true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::Rotate,            "Rotate",              "ffff",             true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::LookAt,            "LookAt",              "fffffffff",        true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::CoordinateSystem,  "CoordinateSystem",    "s",                true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::CoordSysTransform, "CoordSysTransform",   "s",                true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::Transform,         "Transform",           "ffffffffffffffff", true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::ConcatTransform,   "ConcatTransform",     "ffffffffffffffff", true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::ActiveTransform,   "ActiveTransform",     "k",                true,  true,  kActiveTransformValues, nullptr,       -1, -1  },
    { StatementID::MakeNamedMedium,   "MakeNamedMedium",     "s",                true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::MediumInterface,   "MediumInterface",     "ss",               true,  true,  nullptr,                nullptr,       -1, -1  },
    { StatementID::Include,           "Include",             "s",                true,  true,  nullptr,                nullptr,       -1, -1  },
    // World-only statements, can only appear after a WorldBegin statement
    { StatementID::AttributeBegin,     "AttributeBegin",     "",                 false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::AttributeEnd,       "AttributeEnd",       "",                 false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::Shape,              "Shape",              "e",                false, true,  kShapeTypes,            nullptr,       -1, -1 },
    { StatementID::AreaLightSource,    "AreaLightSource",    "e",                false, true,  kAreaLightTypes,        nullptr,       -1, -1 },
    { StatementID::LightSource,        "LightSource",        "e",                false, true,  kLightTypes,            nullptr,       -1, -1 },
    { StatementID::Material,           "Material",           "e",                false, true,  kMaterialTypes,         nullptr,        5, -1 },
    { StatementID::MakeNamedMaterial,  "MakeNamedMaterial",  "s",                false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::NamedMaterial,      "NamedMaterial",      "s",                false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::ObjectBegin,        "ObjectBegin",        "s",                false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::ObjectEnd,          "ObjectEnd",          "",                 false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::ObjectInstance,     "ObjectInstance",     "s",                false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::Texture,            "Texture",            "see",              false, true,  kTextureDataTypes,      kTextureTypes, -1, -1 },
    { StatementID::TransformBegin,     "TransformBegin",     "",                 false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::TransformEnd,       "TransformEnd",       "",                 false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::ReverseOrientation, "ReverseOrientation", "",                 false, true,  nullptr,                nullptr,       -1, -1 },
    { StatementID::WorldEnd,           "WorldEnd",           "",                 false, true,  nullptr,                nullptr,       -1, -1 },
    // Preamble-only statements, cannot appear after a WorldBegin statement
    { StatementID::Accelerator,        "Accelerator",        "e",                true,  false, kAccelTypes,            nullptr,       -1, -1 },
    { StatementID::Camera,             "Camera",             "e",                true,  false, kCameraTypes,           nullptr,       -1, -1 },
    { StatementID::Film,               "Film",               "e",                true,  false, kFilmTypes,             nullptr,       -1, -1 },
    { StatementID::Integrator,         "Integrator",         "e",                true,  false, kIntegratorTypes,       nullptr,       -1, -1 },
    { StatementID::PixelFilter,        "PixelFilter",        "e",                true,  false, kPixelFilterTypes,      nullptr,       -1, -1 },
    { StatementID::Sampler,            "Sampler",            "e",                true,  false, kSamplerTypes,          nullptr,       -1, -1 },
    { StatementID::TransformTimes,     "TransformTimes",     "ff",               true,  false, nullptr,                nullptr,       -1, -1 },
    { StatementID::WorldBegin,         "WorldBegin",         "",                 true,  false, nullptr,                nullptr,       -1, -1 },
  };
  static constexpr uint32_t kNumStatements = MINIPBRT_STATIC_ARRAY_LENGTH(kStatements);


  //
  // Parameter types
  //

  struct ParamTypeDeclaration {
    ParamType type;
    const char* name;
    uint32_t numComponents;
    uint32_t componentSize;
    const char* alias;
  };

  static const ParamTypeDeclaration kParamTypes[] = {
    { ParamType::Bool,      "bool",      1, sizeof(bool),  nullptr  },
    { ParamType::Int,       "integer",   1, sizeof(int),   nullptr  },
    { ParamType::Float,     "float",     1, sizeof(float), nullptr  },
    { ParamType::Point2,    "point2",    2, sizeof(float), nullptr  },
    { ParamType::Point3,    "point3",    3, sizeof(float), "point"  },
    { ParamType::Vector2,   "vector2",   2, sizeof(float), nullptr  },
    { ParamType::Vector3,   "vector3",   3, sizeof(float), "vector" },
    { ParamType::Normal3,   "normal3",   3, sizeof(float), "normal" },
    { ParamType::RGB,       "rgb",       3, sizeof(float), "color"  },
    { ParamType::XYZ,       "xyz",       3, sizeof(float), nullptr  },
    { ParamType::Blackbody, "blackbody", 2, sizeof(float), nullptr  },
    { ParamType::Samples,   "spectrum",  2, sizeof(float), nullptr  },
    { ParamType::String,    "string",    1, sizeof(char),  nullptr  },
    { ParamType::Texture,   "texture",   1, sizeof(char),  nullptr  },
  };
  static constexpr uint32_t kNumParamTypes = MINIPBRT_STATIC_ARRAY_LENGTH(kParamTypes);


  //
  // Material parameters
  //

  // These are used when checking whether a shape has any parameters which
  // override values on its material. We assume that any spectrum or texture
  // parameter is a material override. For float parameters we need to check
  // the parameter names to be sure, because lots of shapes have float
  // parameters of their own.

  static const char* kFloatParams_DisneyMaterial[]       = { "anisotropic", "clearcoat", "clearcoatgloss", "eta", "metallic", "sheen", "roughness", "sheen", "sheentint", "spectrans", "speculartint", nullptr };
  static const char* kFloatParams_FourierMaterial[]      = { nullptr };
  static const char* kFloatParams_GlassMaterial[]        = { "eta", "uroughness", "vroughness", nullptr };
  static const char* kFloatParams_HairMaterial[]         = { "eumelanin", "pheomelanin", "eta", "beta_m", "beta_n", "alpha", nullptr };
  static const char* kFloatParams_KdSubsurfaceMaterial[] = { "eta", "uroughness", "vroughness", nullptr };
  static const char* kFloatParams_MatteMaterial[]        = { "sigma", nullptr };
  static const char* kFloatParams_MetalMaterial[]        = { "uroughness", "vroughness", nullptr };
  static const char* kFloatParams_MirrorMaterial[]       = { nullptr };
  static const char* kFloatParams_MixMaterial[]          = { nullptr };
  static const char* kFloatParams_NoneMaterial[]         = { nullptr };
  static const char* kFloatParams_PlasticMaterial[]      = { "roughness", nullptr };
  static const char* kFloatParams_SubstrateMaterial[]    = { "uroughness", "vroughness", nullptr };
  static const char* kFloatParams_SubsurfaceMaterial[]   = { "scale", "eta", "uroughness", "vroughness", nullptr };
  static const char* kFloatParams_TranslucentMaterial[]  = { "roughness", nullptr };
  static const char* kFloatParams_UberMaterial[]         = { "eta", "uroughness", "vroughness", nullptr };

  static const char** kFloatParamsByMaterial[] = {
    kFloatParams_DisneyMaterial,
    kFloatParams_FourierMaterial,
    kFloatParams_GlassMaterial,
    kFloatParams_HairMaterial,
    kFloatParams_KdSubsurfaceMaterial,
    kFloatParams_MatteMaterial,
    kFloatParams_MetalMaterial,
    kFloatParams_MirrorMaterial,
    kFloatParams_MixMaterial,
    kFloatParams_NoneMaterial,
    kFloatParams_PlasticMaterial,
    kFloatParams_SubstrateMaterial,
    kFloatParams_SubsurfaceMaterial,
    kFloatParams_TranslucentMaterial,
    kFloatParams_UberMaterial,
  };


  //
  // Constants
  //

  static constexpr size_t kDefaultBufCapacity = 1024 * 1024 - 1;
  static constexpr size_t kTempBufferCapacity = 64 * 1024 - 1;

  static const double kDoubleDigits[10] = { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0 };

  static constexpr float kPi = 3.14159265358979323846f;

  static constexpr uint32_t kMaxTransformStackEntry = 127;
  static constexpr uint32_t kMaxAttributeStackEntry = 127;
  static constexpr size_t kMaxReservedTempSpace = 4 * 1024 * 1024;


  //
  // CIE curves, for converting spectra to RGB. Copied from the PBRT source code.
  //

  static const int sampledLambdaStart = 400;
  static const int sampledLambdaEnd = 700;
  static const int nCIESamples = 471;
  static const int nSpectralSamples = 60;

  static const float CIE_X[nCIESamples] = {
      // CIE X function values
      0.0001299000f,   0.0001458470f,   0.0001638021f,   0.0001840037f,
      0.0002066902f,   0.0002321000f,   0.0002607280f,   0.0002930750f,
      0.0003293880f,   0.0003699140f,   0.0004149000f,   0.0004641587f,
      0.0005189860f,   0.0005818540f,   0.0006552347f,   0.0007416000f,
      0.0008450296f,   0.0009645268f,   0.001094949f,    0.001231154f,
      0.001368000f,    0.001502050f,    0.001642328f,    0.001802382f,
      0.001995757f,    0.002236000f,    0.002535385f,    0.002892603f,
      0.003300829f,    0.003753236f,    0.004243000f,    0.004762389f,
      0.005330048f,    0.005978712f,    0.006741117f,    0.007650000f,
      0.008751373f,    0.01002888f,     0.01142170f,     0.01286901f,
      0.01431000f,     0.01570443f,     0.01714744f,     0.01878122f,
      0.02074801f,     0.02319000f,     0.02620736f,     0.02978248f,
      0.03388092f,     0.03846824f,     0.04351000f,     0.04899560f,
      0.05502260f,     0.06171880f,     0.06921200f,     0.07763000f,
      0.08695811f,     0.09717672f,     0.1084063f,      0.1207672f,
      0.1343800f,      0.1493582f,      0.1653957f,      0.1819831f,
      0.1986110f,      0.2147700f,      0.2301868f,      0.2448797f,
      0.2587773f,      0.2718079f,      0.2839000f,      0.2949438f,
      0.3048965f,      0.3137873f,      0.3216454f,      0.3285000f,
      0.3343513f,      0.3392101f,      0.3431213f,      0.3461296f,
      0.3482800f,      0.3495999f,      0.3501474f,      0.3500130f,
      0.3492870f,      0.3480600f,      0.3463733f,      0.3442624f,
      0.3418088f,      0.3390941f,      0.3362000f,      0.3331977f,
      0.3300411f,      0.3266357f,      0.3228868f,      0.3187000f,
      0.3140251f,      0.3088840f,      0.3032904f,      0.2972579f,
      0.2908000f,      0.2839701f,      0.2767214f,      0.2689178f,
      0.2604227f,      0.2511000f,      0.2408475f,      0.2298512f,
      0.2184072f,      0.2068115f,      0.1953600f,      0.1842136f,
      0.1733273f,      0.1626881f,      0.1522833f,      0.1421000f,
      0.1321786f,      0.1225696f,      0.1132752f,      0.1042979f,
      0.09564000f,     0.08729955f,     0.07930804f,     0.07171776f,
      0.06458099f,     0.05795001f,     0.05186211f,     0.04628152f,
      0.04115088f,     0.03641283f,     0.03201000f,     0.02791720f,
      0.02414440f,     0.02068700f,     0.01754040f,     0.01470000f,
      0.01216179f,     0.009919960f,    0.007967240f,    0.006296346f,
      0.004900000f,    0.003777173f,    0.002945320f,    0.002424880f,
      0.002236293f,    0.002400000f,    0.002925520f,    0.003836560f,
      0.005174840f,    0.006982080f,    0.009300000f,    0.01214949f,
      0.01553588f,     0.01947752f,     0.02399277f,     0.02910000f,
      0.03481485f,     0.04112016f,     0.04798504f,     0.05537861f,
      0.06327000f,     0.07163501f,     0.08046224f,     0.08973996f,
      0.09945645f,     0.1096000f,      0.1201674f,      0.1311145f,
      0.1423679f,      0.1538542f,      0.1655000f,      0.1772571f,
      0.1891400f,      0.2011694f,      0.2133658f,      0.2257499f,
      0.2383209f,      0.2510668f,      0.2639922f,      0.2771017f,
      0.2904000f,      0.3038912f,      0.3175726f,      0.3314384f,
      0.3454828f,      0.3597000f,      0.3740839f,      0.3886396f,
      0.4033784f,      0.4183115f,      0.4334499f,      0.4487953f,
      0.4643360f,      0.4800640f,      0.4959713f,      0.5120501f,
      0.5282959f,      0.5446916f,      0.5612094f,      0.5778215f,
      0.5945000f,      0.6112209f,      0.6279758f,      0.6447602f,
      0.6615697f,      0.6784000f,      0.6952392f,      0.7120586f,
      0.7288284f,      0.7455188f,      0.7621000f,      0.7785432f,
      0.7948256f,      0.8109264f,      0.8268248f,      0.8425000f,
      0.8579325f,      0.8730816f,      0.8878944f,      0.9023181f,
      0.9163000f,      0.9297995f,      0.9427984f,      0.9552776f,
      0.9672179f,      0.9786000f,      0.9893856f,      0.9995488f,
      1.0090892f,      1.0180064f,      1.0263000f,      1.0339827f,
      1.0409860f,      1.0471880f,      1.0524667f,      1.0567000f,
      1.0597944f,      1.0617992f,      1.0628068f,      1.0629096f,
      1.0622000f,      1.0607352f,      1.0584436f,      1.0552244f,
      1.0509768f,      1.0456000f,      1.0390369f,      1.0313608f,
      1.0226662f,      1.0130477f,      1.0026000f,      0.9913675f,
      0.9793314f,      0.9664916f,      0.9528479f,      0.9384000f,
      0.9231940f,      0.9072440f,      0.8905020f,      0.8729200f,
      0.8544499f,      0.8350840f,      0.8149460f,      0.7941860f,
      0.7729540f,      0.7514000f,      0.7295836f,      0.7075888f,
      0.6856022f,      0.6638104f,      0.6424000f,      0.6215149f,
      0.6011138f,      0.5811052f,      0.5613977f,      0.5419000f,
      0.5225995f,      0.5035464f,      0.4847436f,      0.4661939f,
      0.4479000f,      0.4298613f,      0.4120980f,      0.3946440f,
      0.3775333f,      0.3608000f,      0.3444563f,      0.3285168f,
      0.3130192f,      0.2980011f,      0.2835000f,      0.2695448f,
      0.2561184f,      0.2431896f,      0.2307272f,      0.2187000f,
      0.2070971f,      0.1959232f,      0.1851708f,      0.1748323f,
      0.1649000f,      0.1553667f,      0.1462300f,      0.1374900f,
      0.1291467f,      0.1212000f,      0.1136397f,      0.1064650f,
      0.09969044f,     0.09333061f,     0.08740000f,     0.08190096f,
      0.07680428f,     0.07207712f,     0.06768664f,     0.06360000f,
      0.05980685f,     0.05628216f,     0.05297104f,     0.04981861f,
      0.04677000f,     0.04378405f,     0.04087536f,     0.03807264f,
      0.03540461f,     0.03290000f,     0.03056419f,     0.02838056f,
      0.02634484f,     0.02445275f,     0.02270000f,     0.02108429f,
      0.01959988f,     0.01823732f,     0.01698717f,     0.01584000f,
      0.01479064f,     0.01383132f,     0.01294868f,     0.01212920f,
      0.01135916f,     0.01062935f,     0.009938846f,    0.009288422f,
      0.008678854f,    0.008110916f,    0.007582388f,    0.007088746f,
      0.006627313f,    0.006195408f,    0.005790346f,    0.005409826f,
      0.005052583f,    0.004717512f,    0.004403507f,    0.004109457f,
      0.003833913f,    0.003575748f,    0.003334342f,    0.003109075f,
      0.002899327f,    0.002704348f,    0.002523020f,    0.002354168f,
      0.002196616f,    0.002049190f,    0.001910960f,    0.001781438f,
      0.001660110f,    0.001546459f,    0.001439971f,    0.001340042f,
      0.001246275f,    0.001158471f,    0.001076430f,    0.0009999493f,
      0.0009287358f,   0.0008624332f,   0.0008007503f,   0.0007433960f,
      0.0006900786f,   0.0006405156f,   0.0005945021f,   0.0005518646f,
      0.0005124290f,   0.0004760213f,   0.0004424536f,   0.0004115117f,
      0.0003829814f,   0.0003566491f,   0.0003323011f,   0.0003097586f,
      0.0002888871f,   0.0002695394f,   0.0002515682f,   0.0002348261f,
      0.0002191710f,   0.0002045258f,   0.0001908405f,   0.0001780654f,
      0.0001661505f,   0.0001550236f,   0.0001446219f,   0.0001349098f,
      0.0001258520f,   0.0001174130f,   0.0001095515f,   0.0001022245f,
      0.00009539445f,  0.00008902390f,  0.00008307527f,  0.00007751269f,
      0.00007231304f,  0.00006745778f,  0.00006292844f,  0.00005870652f,
      0.00005477028f,  0.00005109918f,  0.00004767654f,  0.00004448567f,
      0.00004150994f,  0.00003873324f,  0.00003614203f,  0.00003372352f,
      0.00003146487f,  0.00002935326f,  0.00002737573f,  0.00002552433f,
      0.00002379376f,  0.00002217870f,  0.00002067383f,  0.00001927226f,
      0.00001796640f,  0.00001674991f,  0.00001561648f,  0.00001455977f,
      0.00001357387f,  0.00001265436f,  0.00001179723f,  0.00001099844f,
      0.00001025398f,  0.000009559646f, 0.000008912044f, 0.000008308358f,
      0.000007745769f, 0.000007221456f, 0.000006732475f, 0.000006276423f,
      0.000005851304f, 0.000005455118f, 0.000005085868f, 0.000004741466f,
      0.000004420236f, 0.000004120783f, 0.000003841716f, 0.000003581652f,
      0.000003339127f, 0.000003112949f, 0.000002902121f, 0.000002705645f,
      0.000002522525f, 0.000002351726f, 0.000002192415f, 0.000002043902f,
      0.000001905497f, 0.000001776509f, 0.000001656215f, 0.000001544022f,
      0.000001439440f, 0.000001341977f, 0.000001251141f
  };

  static const float CIE_Y[nCIESamples] = {
      // CIE Y function values
      0.000003917000f,  0.000004393581f,  0.000004929604f,  0.000005532136f,
      0.000006208245f,  0.000006965000f,  0.000007813219f,  0.000008767336f,
      0.000009839844f,  0.00001104323f,   0.00001239000f,   0.00001388641f,
      0.00001555728f,   0.00001744296f,   0.00001958375f,   0.00002202000f,
      0.00002483965f,   0.00002804126f,   0.00003153104f,   0.00003521521f,
      0.00003900000f,   0.00004282640f,   0.00004691460f,   0.00005158960f,
      0.00005717640f,   0.00006400000f,   0.00007234421f,   0.00008221224f,
      0.00009350816f,   0.0001061361f,    0.0001200000f,    0.0001349840f,
      0.0001514920f,    0.0001702080f,    0.0001918160f,    0.0002170000f,
      0.0002469067f,    0.0002812400f,    0.0003185200f,    0.0003572667f,
      0.0003960000f,    0.0004337147f,    0.0004730240f,    0.0005178760f,
      0.0005722187f,    0.0006400000f,    0.0007245600f,    0.0008255000f,
      0.0009411600f,    0.001069880f,     0.001210000f,     0.001362091f,
      0.001530752f,     0.001720368f,     0.001935323f,     0.002180000f,
      0.002454800f,     0.002764000f,     0.003117800f,     0.003526400f,
      0.004000000f,     0.004546240f,     0.005159320f,     0.005829280f,
      0.006546160f,     0.007300000f,     0.008086507f,     0.008908720f,
      0.009767680f,     0.01066443f,      0.01160000f,      0.01257317f,
      0.01358272f,      0.01462968f,      0.01571509f,      0.01684000f,
      0.01800736f,      0.01921448f,      0.02045392f,      0.02171824f,
      0.02300000f,      0.02429461f,      0.02561024f,      0.02695857f,
      0.02835125f,      0.02980000f,      0.03131083f,      0.03288368f,
      0.03452112f,      0.03622571f,      0.03800000f,      0.03984667f,
      0.04176800f,      0.04376600f,      0.04584267f,      0.04800000f,
      0.05024368f,      0.05257304f,      0.05498056f,      0.05745872f,
      0.06000000f,      0.06260197f,      0.06527752f,      0.06804208f,
      0.07091109f,      0.07390000f,      0.07701600f,      0.08026640f,
      0.08366680f,      0.08723280f,      0.09098000f,      0.09491755f,
      0.09904584f,      0.1033674f,       0.1078846f,       0.1126000f,
      0.1175320f,       0.1226744f,       0.1279928f,       0.1334528f,
      0.1390200f,       0.1446764f,       0.1504693f,       0.1564619f,
      0.1627177f,       0.1693000f,       0.1762431f,       0.1835581f,
      0.1912735f,       0.1994180f,       0.2080200f,       0.2171199f,
      0.2267345f,       0.2368571f,       0.2474812f,       0.2586000f,
      0.2701849f,       0.2822939f,       0.2950505f,       0.3085780f,
      0.3230000f,       0.3384021f,       0.3546858f,       0.3716986f,
      0.3892875f,       0.4073000f,       0.4256299f,       0.4443096f,
      0.4633944f,       0.4829395f,       0.5030000f,       0.5235693f,
      0.5445120f,       0.5656900f,       0.5869653f,       0.6082000f,
      0.6293456f,       0.6503068f,       0.6708752f,       0.6908424f,
      0.7100000f,       0.7281852f,       0.7454636f,       0.7619694f,
      0.7778368f,       0.7932000f,       0.8081104f,       0.8224962f,
      0.8363068f,       0.8494916f,       0.8620000f,       0.8738108f,
      0.8849624f,       0.8954936f,       0.9054432f,       0.9148501f,
      0.9237348f,       0.9320924f,       0.9399226f,       0.9472252f,
      0.9540000f,       0.9602561f,       0.9660074f,       0.9712606f,
      0.9760225f,       0.9803000f,       0.9840924f,       0.9874812f,
      0.9903128f,       0.9928116f,       0.9949501f,       0.9967108f,
      0.9980983f,       0.9991120f,       0.9997482f,       1.0000000f,
      0.9998567f,       0.9993046f,       0.9983255f,       0.9968987f,
      0.9950000f,       0.9926005f,       0.9897426f,       0.9864444f,
      0.9827241f,       0.9786000f,       0.9740837f,       0.9691712f,
      0.9638568f,       0.9581349f,       0.9520000f,       0.9454504f,
      0.9384992f,       0.9311628f,       0.9234576f,       0.9154000f,
      0.9070064f,       0.8982772f,       0.8892048f,       0.8797816f,
      0.8700000f,       0.8598613f,       0.8493920f,       0.8386220f,
      0.8275813f,       0.8163000f,       0.8047947f,       0.7930820f,
      0.7811920f,       0.7691547f,       0.7570000f,       0.7447541f,
      0.7324224f,       0.7200036f,       0.7074965f,       0.6949000f,
      0.6822192f,       0.6694716f,       0.6566744f,       0.6438448f,
      0.6310000f,       0.6181555f,       0.6053144f,       0.5924756f,
      0.5796379f,       0.5668000f,       0.5539611f,       0.5411372f,
      0.5283528f,       0.5156323f,       0.5030000f,       0.4904688f,
      0.4780304f,       0.4656776f,       0.4534032f,       0.4412000f,
      0.4290800f,       0.4170360f,       0.4050320f,       0.3930320f,
      0.3810000f,       0.3689184f,       0.3568272f,       0.3447768f,
      0.3328176f,       0.3210000f,       0.3093381f,       0.2978504f,
      0.2865936f,       0.2756245f,       0.2650000f,       0.2547632f,
      0.2448896f,       0.2353344f,       0.2260528f,       0.2170000f,
      0.2081616f,       0.1995488f,       0.1911552f,       0.1829744f,
      0.1750000f,       0.1672235f,       0.1596464f,       0.1522776f,
      0.1451259f,       0.1382000f,       0.1315003f,       0.1250248f,
      0.1187792f,       0.1127691f,       0.1070000f,       0.1014762f,
      0.09618864f,      0.09112296f,      0.08626485f,      0.08160000f,
      0.07712064f,      0.07282552f,      0.06871008f,      0.06476976f,
      0.06100000f,      0.05739621f,      0.05395504f,      0.05067376f,
      0.04754965f,      0.04458000f,      0.04175872f,      0.03908496f,
      0.03656384f,      0.03420048f,      0.03200000f,      0.02996261f,
      0.02807664f,      0.02632936f,      0.02470805f,      0.02320000f,
      0.02180077f,      0.02050112f,      0.01928108f,      0.01812069f,
      0.01700000f,      0.01590379f,      0.01483718f,      0.01381068f,
      0.01283478f,      0.01192000f,      0.01106831f,      0.01027339f,
      0.009533311f,     0.008846157f,     0.008210000f,     0.007623781f,
      0.007085424f,     0.006591476f,     0.006138485f,     0.005723000f,
      0.005343059f,     0.004995796f,     0.004676404f,     0.004380075f,
      0.004102000f,     0.003838453f,     0.003589099f,     0.003354219f,
      0.003134093f,     0.002929000f,     0.002738139f,     0.002559876f,
      0.002393244f,     0.002237275f,     0.002091000f,     0.001953587f,
      0.001824580f,     0.001703580f,     0.001590187f,     0.001484000f,
      0.001384496f,     0.001291268f,     0.001204092f,     0.001122744f,
      0.001047000f,     0.0009765896f,    0.0009111088f,    0.0008501332f,
      0.0007932384f,    0.0007400000f,    0.0006900827f,    0.0006433100f,
      0.0005994960f,    0.0005584547f,    0.0005200000f,    0.0004839136f,
      0.0004500528f,    0.0004183452f,    0.0003887184f,    0.0003611000f,
      0.0003353835f,    0.0003114404f,    0.0002891656f,    0.0002684539f,
      0.0002492000f,    0.0002313019f,    0.0002146856f,    0.0001992884f,
      0.0001850475f,    0.0001719000f,    0.0001597781f,    0.0001486044f,
      0.0001383016f,    0.0001287925f,    0.0001200000f,    0.0001118595f,
      0.0001043224f,    0.00009733560f,   0.00009084587f,   0.00008480000f,
      0.00007914667f,   0.00007385800f,   0.00006891600f,   0.00006430267f,
      0.00006000000f,   0.00005598187f,   0.00005222560f,   0.00004871840f,
      0.00004544747f,   0.00004240000f,   0.00003956104f,   0.00003691512f,
      0.00003444868f,   0.00003214816f,   0.00003000000f,   0.00002799125f,
      0.00002611356f,   0.00002436024f,   0.00002272461f,   0.00002120000f,
      0.00001977855f,   0.00001845285f,   0.00001721687f,   0.00001606459f,
      0.00001499000f,   0.00001398728f,   0.00001305155f,   0.00001217818f,
      0.00001136254f,   0.00001060000f,   0.000009885877f,  0.000009217304f,
      0.000008592362f,  0.000008009133f,  0.000007465700f,  0.000006959567f,
      0.000006487995f,  0.000006048699f,  0.000005639396f,  0.000005257800f,
      0.000004901771f,  0.000004569720f,  0.000004260194f,  0.000003971739f,
      0.000003702900f,  0.000003452163f,  0.000003218302f,  0.000003000300f,
      0.000002797139f,  0.000002607800f,  0.000002431220f,  0.000002266531f,
      0.000002113013f,  0.000001969943f,  0.000001836600f,  0.000001712230f,
      0.000001596228f,  0.000001488090f,  0.000001387314f,  0.000001293400f,
      0.000001205820f,  0.000001124143f,  0.000001048009f,  0.0000009770578f,
      0.0000009109300f, 0.0000008492513f, 0.0000007917212f, 0.0000007380904f,
      0.0000006881098f, 0.0000006415300f, 0.0000005980895f, 0.0000005575746f,
      0.0000005198080f, 0.0000004846123f, 0.0000004518100f
  };

  static const float CIE_Z[nCIESamples] = {
      // CIE Z function values
      0.0006061000f,    0.0006808792f,    0.0007651456f,    0.0008600124f,
      0.0009665928f,    0.001086000f,     0.001220586f,     0.001372729f,
      0.001543579f,     0.001734286f,     0.001946000f,     0.002177777f,
      0.002435809f,     0.002731953f,     0.003078064f,     0.003486000f,
      0.003975227f,     0.004540880f,     0.005158320f,     0.005802907f,
      0.006450001f,     0.007083216f,     0.007745488f,     0.008501152f,
      0.009414544f,     0.01054999f,      0.01196580f,      0.01365587f,
      0.01558805f,      0.01773015f,      0.02005001f,      0.02251136f,
      0.02520288f,      0.02827972f,      0.03189704f,      0.03621000f,
      0.04143771f,      0.04750372f,      0.05411988f,      0.06099803f,
      0.06785001f,      0.07448632f,      0.08136156f,      0.08915364f,
      0.09854048f,      0.1102000f,       0.1246133f,       0.1417017f,
      0.1613035f,       0.1832568f,       0.2074000f,       0.2336921f,
      0.2626114f,       0.2947746f,       0.3307985f,       0.3713000f,
      0.4162091f,       0.4654642f,       0.5196948f,       0.5795303f,
      0.6456000f,       0.7184838f,       0.7967133f,       0.8778459f,
      0.9594390f,       1.0390501f,       1.1153673f,       1.1884971f,
      1.2581233f,       1.3239296f,       1.3856000f,       1.4426352f,
      1.4948035f,       1.5421903f,       1.5848807f,       1.6229600f,
      1.6564048f,       1.6852959f,       1.7098745f,       1.7303821f,
      1.7470600f,       1.7600446f,       1.7696233f,       1.7762637f,
      1.7804334f,       1.7826000f,       1.7829682f,       1.7816998f,
      1.7791982f,       1.7758671f,       1.7721100f,       1.7682589f,
      1.7640390f,       1.7589438f,       1.7524663f,       1.7441000f,
      1.7335595f,       1.7208581f,       1.7059369f,       1.6887372f,
      1.6692000f,       1.6475287f,       1.6234127f,       1.5960223f,
      1.5645280f,       1.5281000f,       1.4861114f,       1.4395215f,
      1.3898799f,       1.3387362f,       1.2876400f,       1.2374223f,
      1.1878243f,       1.1387611f,       1.0901480f,       1.0419000f,
      0.9941976f,       0.9473473f,       0.9014531f,       0.8566193f,
      0.8129501f,       0.7705173f,       0.7294448f,       0.6899136f,
      0.6521049f,       0.6162000f,       0.5823286f,       0.5504162f,
      0.5203376f,       0.4919673f,       0.4651800f,       0.4399246f,
      0.4161836f,       0.3938822f,       0.3729459f,       0.3533000f,
      0.3348578f,       0.3175521f,       0.3013375f,       0.2861686f,
      0.2720000f,       0.2588171f,       0.2464838f,       0.2347718f,
      0.2234533f,       0.2123000f,       0.2011692f,       0.1901196f,
      0.1792254f,       0.1685608f,       0.1582000f,       0.1481383f,
      0.1383758f,       0.1289942f,       0.1200751f,       0.1117000f,
      0.1039048f,       0.09666748f,      0.08998272f,      0.08384531f,
      0.07824999f,      0.07320899f,      0.06867816f,      0.06456784f,
      0.06078835f,      0.05725001f,      0.05390435f,      0.05074664f,
      0.04775276f,      0.04489859f,      0.04216000f,      0.03950728f,
      0.03693564f,      0.03445836f,      0.03208872f,      0.02984000f,
      0.02771181f,      0.02569444f,      0.02378716f,      0.02198925f,
      0.02030000f,      0.01871805f,      0.01724036f,      0.01586364f,
      0.01458461f,      0.01340000f,      0.01230723f,      0.01130188f,
      0.01037792f,      0.009529306f,     0.008749999f,     0.008035200f,
      0.007381600f,     0.006785400f,     0.006242800f,     0.005749999f,
      0.005303600f,     0.004899800f,     0.004534200f,     0.004202400f,
      0.003900000f,     0.003623200f,     0.003370600f,     0.003141400f,
      0.002934800f,     0.002749999f,     0.002585200f,     0.002438600f,
      0.002309400f,     0.002196800f,     0.002100000f,     0.002017733f,
      0.001948200f,     0.001889800f,     0.001840933f,     0.001800000f,
      0.001766267f,     0.001737800f,     0.001711200f,     0.001683067f,
      0.001650001f,     0.001610133f,     0.001564400f,     0.001513600f,
      0.001458533f,     0.001400000f,     0.001336667f,     0.001270000f,
      0.001205000f,     0.001146667f,     0.001100000f,     0.001068800f,
      0.001049400f,     0.001035600f,     0.001021200f,     0.001000000f,
      0.0009686400f,    0.0009299200f,    0.0008868800f,    0.0008425600f,
      0.0008000000f,    0.0007609600f,    0.0007236800f,    0.0006859200f,
      0.0006454400f,    0.0006000000f,    0.0005478667f,    0.0004916000f,
      0.0004354000f,    0.0003834667f,    0.0003400000f,    0.0003072533f,
      0.0002831600f,    0.0002654400f,    0.0002518133f,    0.0002400000f,
      0.0002295467f,    0.0002206400f,    0.0002119600f,    0.0002021867f,
      0.0001900000f,    0.0001742133f,    0.0001556400f,    0.0001359600f,
      0.0001168533f,    0.0001000000f,    0.00008613333f,   0.00007460000f,
      0.00006500000f,   0.00005693333f,   0.00004999999f,   0.00004416000f,
      0.00003948000f,   0.00003572000f,   0.00003264000f,   0.00003000000f,
      0.00002765333f,   0.00002556000f,   0.00002364000f,   0.00002181333f,
      0.00002000000f,   0.00001813333f,   0.00001620000f,   0.00001420000f,
      0.00001213333f,   0.00001000000f,   0.000007733333f,  0.000005400000f,
      0.000003200000f,  0.000001333333f,  0.000000000000f,  0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f,             0.0f,
      0.0f,             0.0f,             0.0f
  };

  const float CIE_lambda[nCIESamples] = {
      360.0f, 361.0f, 362.0f, 363.0f, 364.0f, 365.0f, 366.0f, 367.0f, 368.0f, 369.0f, 370.0f, 371.0f, 372.0f, 373.0f, 374.0f,
      375.0f, 376.0f, 377.0f, 378.0f, 379.0f, 380.0f, 381.0f, 382.0f, 383.0f, 384.0f, 385.0f, 386.0f, 387.0f, 388.0f, 389.0f,
      390.0f, 391.0f, 392.0f, 393.0f, 394.0f, 395.0f, 396.0f, 397.0f, 398.0f, 399.0f, 400.0f, 401.0f, 402.0f, 403.0f, 404.0f,
      405.0f, 406.0f, 407.0f, 408.0f, 409.0f, 410.0f, 411.0f, 412.0f, 413.0f, 414.0f, 415.0f, 416.0f, 417.0f, 418.0f, 419.0f,
      420.0f, 421.0f, 422.0f, 423.0f, 424.0f, 425.0f, 426.0f, 427.0f, 428.0f, 429.0f, 430.0f, 431.0f, 432.0f, 433.0f, 434.0f,
      435.0f, 436.0f, 437.0f, 438.0f, 439.0f, 440.0f, 441.0f, 442.0f, 443.0f, 444.0f, 445.0f, 446.0f, 447.0f, 448.0f, 449.0f,
      450.0f, 451.0f, 452.0f, 453.0f, 454.0f, 455.0f, 456.0f, 457.0f, 458.0f, 459.0f, 460.0f, 461.0f, 462.0f, 463.0f, 464.0f,
      465.0f, 466.0f, 467.0f, 468.0f, 469.0f, 470.0f, 471.0f, 472.0f, 473.0f, 474.0f, 475.0f, 476.0f, 477.0f, 478.0f, 479.0f,
      480.0f, 481.0f, 482.0f, 483.0f, 484.0f, 485.0f, 486.0f, 487.0f, 488.0f, 489.0f, 490.0f, 491.0f, 492.0f, 493.0f, 494.0f,
      495.0f, 496.0f, 497.0f, 498.0f, 499.0f, 500.0f, 501.0f, 502.0f, 503.0f, 504.0f, 505.0f, 506.0f, 507.0f, 508.0f, 509.0f,
      510.0f, 511.0f, 512.0f, 513.0f, 514.0f, 515.0f, 516.0f, 517.0f, 518.0f, 519.0f, 520.0f, 521.0f, 522.0f, 523.0f, 524.0f,
      525.0f, 526.0f, 527.0f, 528.0f, 529.0f, 530.0f, 531.0f, 532.0f, 533.0f, 534.0f, 535.0f, 536.0f, 537.0f, 538.0f, 539.0f,
      540.0f, 541.0f, 542.0f, 543.0f, 544.0f, 545.0f, 546.0f, 547.0f, 548.0f, 549.0f, 550.0f, 551.0f, 552.0f, 553.0f, 554.0f,
      555.0f, 556.0f, 557.0f, 558.0f, 559.0f, 560.0f, 561.0f, 562.0f, 563.0f, 564.0f, 565.0f, 566.0f, 567.0f, 568.0f, 569.0f,
      570.0f, 571.0f, 572.0f, 573.0f, 574.0f, 575.0f, 576.0f, 577.0f, 578.0f, 579.0f, 580.0f, 581.0f, 582.0f, 583.0f, 584.0f,
      585.0f, 586.0f, 587.0f, 588.0f, 589.0f, 590.0f, 591.0f, 592.0f, 593.0f, 594.0f, 595.0f, 596.0f, 597.0f, 598.0f, 599.0f,
      600.0f, 601.0f, 602.0f, 603.0f, 604.0f, 605.0f, 606.0f, 607.0f, 608.0f, 609.0f, 610.0f, 611.0f, 612.0f, 613.0f, 614.0f,
      615.0f, 616.0f, 617.0f, 618.0f, 619.0f, 620.0f, 621.0f, 622.0f, 623.0f, 624.0f, 625.0f, 626.0f, 627.0f, 628.0f, 629.0f,
      630.0f, 631.0f, 632.0f, 633.0f, 634.0f, 635.0f, 636.0f, 637.0f, 638.0f, 639.0f, 640.0f, 641.0f, 642.0f, 643.0f, 644.0f,
      645.0f, 646.0f, 647.0f, 648.0f, 649.0f, 650.0f, 651.0f, 652.0f, 653.0f, 654.0f, 655.0f, 656.0f, 657.0f, 658.0f, 659.0f,
      660.0f, 661.0f, 662.0f, 663.0f, 664.0f, 665.0f, 666.0f, 667.0f, 668.0f, 669.0f, 670.0f, 671.0f, 672.0f, 673.0f, 674.0f,
      675.0f, 676.0f, 677.0f, 678.0f, 679.0f, 680.0f, 681.0f, 682.0f, 683.0f, 684.0f, 685.0f, 686.0f, 687.0f, 688.0f, 689.0f,
      690.0f, 691.0f, 692.0f, 693.0f, 694.0f, 695.0f, 696.0f, 697.0f, 698.0f, 699.0f, 700.0f, 701.0f, 702.0f, 703.0f, 704.0f,
      705.0f, 706.0f, 707.0f, 708.0f, 709.0f, 710.0f, 711.0f, 712.0f, 713.0f, 714.0f, 715.0f, 716.0f, 717.0f, 718.0f, 719.0f,
      720.0f, 721.0f, 722.0f, 723.0f, 724.0f, 725.0f, 726.0f, 727.0f, 728.0f, 729.0f, 730.0f, 731.0f, 732.0f, 733.0f, 734.0f,
      735.0f, 736.0f, 737.0f, 738.0f, 739.0f, 740.0f, 741.0f, 742.0f, 743.0f, 744.0f, 745.0f, 746.0f, 747.0f, 748.0f, 749.0f,
      750.0f, 751.0f, 752.0f, 753.0f, 754.0f, 755.0f, 756.0f, 757.0f, 758.0f, 759.0f, 760.0f, 761.0f, 762.0f, 763.0f, 764.0f,
      765.0f, 766.0f, 767.0f, 768.0f, 769.0f, 770.0f, 771.0f, 772.0f, 773.0f, 774.0f, 775.0f, 776.0f, 777.0f, 778.0f, 779.0f,
      780.0f, 781.0f, 782.0f, 783.0f, 784.0f, 785.0f, 786.0f, 787.0f, 788.0f, 789.0f, 790.0f, 791.0f, 792.0f, 793.0f, 794.0f,
      795.0f, 796.0f, 797.0f, 798.0f, 799.0f, 800.0f, 801.0f, 802.0f, 803.0f, 804.0f, 805.0f, 806.0f, 807.0f, 808.0f, 809.0f,
      810.0f, 811.0f, 812.0f, 813.0f, 814.0f, 815.0f, 816.0f, 817.0f, 818.0f, 819.0f, 820.0f, 821.0f, 822.0f, 823.0f, 824.0f,
      825.0f, 826.0f, 827.0f, 828.0f, 829.0f, 830.0f
  };

  static const float CIE_Y_integral = 106.856895f;


  //
  // Vec3 type
  //

  struct Vec3 {
    float x, y, z;
  };

  static inline Vec3 operator - (Vec3 lhs, Vec3 rhs) { return Vec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z }; }

  static inline float dot(Vec3 lhs, Vec3 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }
  static inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
  static inline Vec3 normalize(Vec3 v) { float len = length(v); return Vec3{ v.x / len, v.y / len, v.z / len }; }
  static inline Vec3 cross(Vec3 lhs, Vec3 rhs) { return Vec3{ lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x }; }


  //
  // Mat4 type
  //

  struct Mat4 {
    float rows[4][4];

    void identity();                                  //!< Set this to the identity matrix.
    void translate(Vec3 v);                           //!< Multiply this matrix by a translation matrix
    void scale(Vec3 v);                               //!< Multiply this matrix by a scale matrix.
    void rotate(const float angleRadians, Vec3 axis); //!< Multiply this matrix by a rotation matrix.
    void lookAt(Vec3 pos, Vec3 target, Vec3 up);      //!< Multiply this matrix by a lookAt matrix.
    void concatTransform(const Mat4& m);              //!< Multiply this matrix by the given matrix.

    float det2x2(int r0, int r1, int c0, int c1) const;
  };

  static Mat4 inverse(const Mat4& m);


  //
  // TransformStack type
  //

  struct TransformStack {
    Mat4 matrices[kMaxTransformStackEntry + 1][2];
    bool active[2] = { true, true };
    uint32_t entry = 0;

    std::unordered_map<std::string, Mat4*> coordinateSystems;

    TransformStack();
    ~TransformStack();

    bool push();
    bool pop();

    void clear();                                 //!< Pop all transforms off the stack and reset the remaining transform to identity.

    void identity();                              //!< Set the current transform to the identity matrix.
    void translate(Vec3 t);                       //!< Multiply the current transform by a translation matrix.
    void scale(Vec3 s);                           //!< Multiply the current transform by a scale matrix.
    void rotate(float angleRadians, Vec3 axis);   //!< Multiply the current transform by a rotation matrix.
    void lookAt(Vec3 pos, Vec3 target, Vec3 up);  //!< Multiply the current transform by a lookAt matrix.
    void transform(const Mat4& m);                //!< Set the current transform to an arbitrary matrix.
    void concatTransform(const Mat4& m);          //!< Multiply the current transform by an arbitrary matrix.

    void coordinateSystem(const char* name);      //!< Save the current transforms with the specified name.
    bool coordSysTransform(const char* name);     //!< Restore the transforms with the specified name.
  };


  //
  // Attributes type
  //

  struct Attributes {
    uint32_t activeMaterial = kInvalidIndex;
    uint32_t areaLight      = kInvalidIndex;
    uint32_t insideMedium   = kInvalidIndex;
    uint32_t outsideMedium  = kInvalidIndex;
    bool reverseOrientation = false;

    std::vector<uint32_t> floatTextures;    //!< Float textures defined in the current AttributeBegin/End block. Values are indexes into `Scene::textures`.
    std::vector<uint32_t> spectrumTextures; //!< Spectrum textures defined in the current AttributeBegin/End block. Values are indexes into `Scene::textures`.
    std::vector<uint32_t> materials;        //!< Materials defined in the current AttributeBegin/End block. Values are indexes into `Scene::materials`.

    Attributes();
  };


  //
  // AttributeStack type
  //

  struct AttributeStack {
    Attributes attrs[kMaxAttributeStackEntry + 1];
    uint32_t entry = 0;

    Attributes* top; // Always points to the current top entry.

    AttributeStack();

    bool push();
    bool pop();
    void clear();
  };


  //
  // Tokenizer class declaration
  //

  class Tokenizer {
  public:
    Tokenizer();
    ~Tokenizer();

    /// Buffer capacity is how many chars to store in the read buffer. It
    /// determines how much memory is allocated and the size of the blocks we
    /// attempt to read from the input file.
    ///
    /// If a single token (including string literals) is larger than the buffer
    /// capacity, tokenisation will fail.
    void set_buffer_capacity(size_t n);

    /// 0 means no include files allowed, any attempt to open an additional file will fail
    /// 1 means the original file can include a file, but they can't include any files.
    /// 2 means the original file can include A and A can include B, but B can't include anything.
    /// ...and so on.
    void set_max_include_depth(uint32_t n);

    bool open_file(const char* filename);
    bool eof() const;     //!< Is the cursor at the end of the file yet?
    bool advance();       //!< Skip past whitespace & comments, refilling the buffer as necessary. Returns false at EOF, true otherwise.
    bool refill_buffer(); //!< Load more data from the file, preserving the current token.

    bool push_file(const char* filename, bool reportEOF); //!< Open an included file.
    bool pop_file();                      //!< Close an included file.

    /// Get the line and column number that we're up to. This is SLOW!
    /// We compute the line number on demand by rewinding to the start of the
    /// file and counting line numbers from there up to the current cursor
    /// position. This means there's no line counting overhead during parsing,
    /// you only pay the cost when you need it (usually for error reporting).
    void cursor_location(int64_t* line, int64_t* col);

    bool string_literal(char buf[], size_t bufLen);
    bool int_literal(int* val);
    bool float_literal(float* val);
    bool float_array(float arr[], size_t arrLen);
    bool identifier(char buf[], size_t bufLen);
    bool which_directive(uint32_t* index);
    bool which_type(uint32_t* index);
    bool match_symbol(const char* str);
    bool which_string_literal(const char* values[], int* index);
    bool which_keyword(const char* values[], int* index);

    size_t token_length() const;
    const char* token() const;

    const char* get_filename() const;
    const char* get_original_filename() const;

    void set_error(const char* fmt, ...);
    bool has_error() const;
    const Error* get_error() const;


    char* temp_buffer(); // Allow other objects to use the Tokenizer's temp buffer.

  private:
    struct FileData {
      const char* filename = nullptr;
      FILE* f              = nullptr;
      bool atEOF           = false;
      bool reportEOF       = false; // If true, advance stops at EOF of the current file and you must explicitly use pop_file to continue. If false, then we automatically call pop_file when EOF is reached.
      int64_t bufOffset    = 0; // File offset for the start of the current buffer contents.
    };

  private:
    FileData* m_fileData = nullptr; // Array of length `m_maxIncludeDepth + 1`;
    uint32_t m_includeDepth = 0;
    uint32_t m_maxIncludeDepth = 5;

    char* m_buf = nullptr;
    char* m_bufEnd = nullptr;
    size_t m_bufCapacity = kDefaultBufCapacity;

    const char* m_pos = nullptr;
    const char* m_end = nullptr;

    Error* m_error = nullptr;


    char* m_tmpBuf = nullptr;
  };


  //
  // ParamDecl struct
  //

  typedef Bits<ParamType> ParamTypeSet;
  static const ParamTypeSet kSpectrumTypes = ParamType::RGB | ParamType::XYZ | ParamType::Blackbody | ParamType::Samples;
  static const ParamTypeSet kColorTextureTypes = kSpectrumTypes | ParamType::Texture;
  static const ParamTypeSet kFloatTextureTypes = ParamType::Float | ParamType::Texture;


  struct ParamInfo {
    const char* name;
    ParamType type;
    size_t offset;  //!< Start index of the data for this param in the Parser's m_temp array.
    uint32_t count; //!< Number of items with type `type` in the param.
  };


  //
  // Parser class declaration
  //

  class Parser {
  public:
    Parser();
    ~Parser();

    Tokenizer& tokenizer();

    bool parse(const char* filename);

    bool has_error() const;
    const Error* get_error() const;

    Scene* take_scene();
    Scene* borrow_scene();

  private:
    bool parse_Identity();
    bool parse_Translate();
    bool parse_Scale();
    bool parse_Rotate();
    bool parse_LookAt();
    bool parse_CoordinateSystem();
    bool parse_CoordSysTransform();
    bool parse_Transform();
    bool parse_ConcatTransform();
    bool parse_ActiveTransform();
    bool parse_MakeNamedMedium();
    bool parse_MediumInterface();
    bool parse_Include();
    bool parse_AttributeBegin();
    bool parse_AttributeEnd();
    bool parse_Shape();
    bool parse_AreaLightSource();
    bool parse_LightSource();
    bool parse_Material();
    bool parse_MakeNamedMaterial();
    bool parse_NamedMaterial();
    bool parse_ObjectBegin();
    bool parse_ObjectEnd();
    bool parse_ObjectInstance();
    bool parse_Texture();
    bool parse_TransformBegin();
    bool parse_TransformEnd();
    bool parse_ReverseOrientation();
    bool parse_Accelerator();
    bool parse_Camera();
    bool parse_Film();
    bool parse_Integrator();
    bool parse_PixelFilter();
    bool parse_Sampler();
    bool parse_TransformTimes();
    bool parse_WorldBegin();
    bool parse_WorldEnd();

    bool parse_material_common(MaterialType materialType, const char* materialName, uint32_t* materialOut);
    bool parse_material_overrides(const Material* baseMaterial, uint32_t* materialOut);
    bool has_material_overrides(uint32_t matIdx) const;

    bool parse_statement();
    bool parse_args(const StatementDeclaration& statement);
    bool parse_params();
    bool parse_param();

    bool parse_ints(uint32_t* count);
    bool parse_floats(uint32_t* count);
    bool parse_spectrum(uint32_t* count);
    bool parse_strings(uint32_t* count);
    bool parse_bools(uint32_t* count);

    const char* string_arg(uint32_t index) const;
    int enum_arg(uint32_t index) const;
    float float_arg(uint32_t index) const;

    template <class T>
    T typed_enum_arg(uint32_t index) const {
      return static_cast<T>(enum_arg(index));
    }

    const ParamInfo* find_param(const char* name, ParamTypeSet allowedTypes) const;

    bool string_param(const char* name, char** dest, bool copy=false);
    bool string_param_with_default(const char* name, char** dest, const char* defaultVal, bool copy=false);
    bool filename_param(const char* name, char** dest);
    bool bool_param(const char* name, bool* dest);
    bool bool_param_with_default(const char* name, bool* dest, bool defaultVal);
    bool int_param(const char* name, int* dest);
    bool int_array_param(const char* name, uint32_t len, int* dest);
    bool int_vector_param(const char* name, uint32_t *len, int** dest, bool copy=false);
    bool float_param(const char* name, float* dest);
    bool float_param_with_default(const char* name, float* dest, float defaultVal);
    bool float_array_param(const char* name, ParamType expectedType, uint32_t len, float* dest);
    bool float_vector_param(const char* name, ParamType expectedType, uint32_t *len, float** dest, bool copy);
    bool spectrum_param(const char* name, float dest[3]);
    bool texture_param(const char* name, TextureData dataType, uint32_t* dest);
    bool float_texture_param(const char* name, FloatTex* dest);
    bool color_texture_param(const char* name, ColorTex* dest);
    bool float_texture_param_with_default(const char* name, FloatTex* dest, const FloatTex* defaultVal);
    bool color_texture_param_with_default(const char *name, ColorTex *dest, const ColorTex* defaultVal);
    bool enum_param(const char* name, const char* values[], int* dest);

    template <class T>
    bool typed_enum_param(const char* name, const char* values[], T* dest) {
      return enum_param(name, values, reinterpret_cast<int*>(dest));
    }

    void save_current_transform_matrices(Transform* dest) const;
    void save_inverse_transform_matrices(Transform* dest) const;

    uint32_t find_object(const char* name) const;
    uint32_t find_medium(const char* name) const;
    uint32_t find_material(const char* name) const;
    uint32_t find_texture(const char* name, TextureData dataType) const;

    void push_bytes(void* src, size_t numBytes);
    void push_string(const char* src, size_t len);

  private:
    Tokenizer m_tokenizer;
    bool m_inWorld               = false;
    TransformStack* m_transforms = nullptr;
    AttributeStack* m_attrs      = nullptr;
    uint32_t m_activeObject      = kInvalidIndex;
    uint32_t m_firstShape        = kInvalidIndex; // Index of the first shape in the currently active object.

    Scene* m_scene               = nullptr;

    // Temporary storage for the current directive.
    uint32_t m_statementIndex = uint32_t(-1);
    std::vector<ParamInfo> m_params;
    std::vector<uint8_t> m_temp;

    std::unordered_map<std::string, const char*> m_paramNames;

    // Temporary storage for processing strings.
    char* m_tmpBuf = nullptr;
  };


  //
  // Internal-only functions
  //

  static inline bool is_whitespace(char ch)
  {
    return ch == ' ' || ch == '\t' || ch == '\r';
  }


  static inline bool is_digit(char ch)
  {
    return ch >= '0' && ch <= '9';
  }


  static inline bool is_letter(char ch)
  {
    ch |= 32; // upper and lower case letters differ only at this bit.
    return ch >= 'a' && ch <= 'z';
  }


  static inline bool is_alnum(char ch)
  {
    return is_digit(ch) || is_letter(ch);
  }


  static inline bool is_keyword_part(char ch)
  {
    return is_alnum(ch) || ch == '_';
  }


  static inline bool is_safe_buffer_end(char ch)
  {
    return (ch > 0 && ch <= 32) || (ch >= 127);
//    return is_whitespace(ch) || ch == '\n';
  }


  static char* copy_string(const char* src)
  {
    if (src == nullptr) {
      return nullptr;
    }
    size_t len = std::strlen(src);
    char* dst = new char[len + 1];
    std::memcpy(dst, src, sizeof(char) * len);
    dst[len] = '\0';
    return dst;
  }


  static int find_string_in_array(const char* str, const char* arr[])
  {
    for (int i = 0; arr[i] != nullptr; i++) {
      if (std::strcmp(str, arr[i]) == 0) {
        return i;
      }
    }
    return -1;
  }


  // If `filename` is a relative path, make it relative to the directory
  // containing `current`. If `current` is an absolute path, the resolved
  // filename will be absolute too. If `filename` is already an absolute path,
  // then it's returned as is.
  //
  // The resolved path is stored in `buf`. The boolean return value indicates
  // whether buf was large enough to hold it. If buf was large enough we return
  // true, otherwise return false and set buf to the empty string.
  static bool resolve_file(const char* filename, const char* current, char* buf, size_t bufSize)
  {
    if (filename == nullptr || filename[0] == '\0') {
      return false;
    }

    bool isAbsolute = (filename[0] == '/' || filename[0] == '\\');
    if (!isAbsolute) {
      if (is_letter(filename[0]) && filename[1] == ':') {
        isAbsolute = true;
      }
    }

    size_t currentLen = 0;
    if (!isAbsolute) {
      for (size_t i = 0; current[i] != '\0'; i++) {
        if (current[i] == '/' || current[i] == '\\') {
          currentLen = i + 1;
        }
      }
    }

    size_t filenameLen = 0;
    while (filename[filenameLen] != '\0') {
      ++filenameLen;
    }
    ++filenameLen; // Include the null terminator.

    size_t totalLen = currentLen + filenameLen;
    if (totalLen > bufSize) {
      return false;
    }

    if (filename >= buf && filename < (buf + currentLen)) {
      if (currentLen > 0) {
        memmove(buf + sizeof(char) * currentLen, filename, sizeof(char) * currentLen);
        memcpy(buf, current, sizeof(char) * currentLen);
      }
    }
    else {
      if (currentLen > 0) {
        memcpy(buf, current, sizeof(char) * currentLen);
      }
      memcpy(buf + currentLen, filename, sizeof(char) * filenameLen);
    }
    return true;
  }


  static int file_open(FILE** f, const char* filename, const char* mode)
  {
  #ifdef _WIN32
    return fopen_s(f, filename, mode);
  #else
    *f = fopen(filename, mode);
    return (*f != nullptr) ? 0 : errno;
  #endif
  }

  static inline int64_t file_pos(FILE* file)
  {
  #ifdef _WIN32
    return _ftelli64(file);
  #else
    static_assert(sizeof(off_t) == sizeof(int64_t), "off_t is not 64 bits.");
    return ftello(file);
  #endif
  }


  static inline int file_seek(FILE* file, int64_t offset, int origin)
  {
  #ifdef _WIN32
    return _fseeki64(file, offset, origin);
  #else
    static_assert(sizeof(off_t) == sizeof(int64_t), "off_t is not 64 bits.");
    return fseeko(file, offset, origin);
  #endif
  }


  static bool int_literal(const char* start, char const** end, int* val)
  {
    const char* pos = start;

    bool negative = false;
    if (*pos == '-') {
      negative = true;
      ++pos;
    }
    else if (*pos == '+') {
      ++pos;
    }

    bool hasLeadingZeroes = *pos == '0';
    if (hasLeadingZeroes) {
      do {
        ++pos;
      } while (*pos == '0');
    }

    int numDigits = 0;
    int localVal = 0;
    while (is_digit(*pos)) {
      // FIXME: this will overflow if we get too many digits.
      localVal = localVal * 10 + static_cast<int>(*pos - '0');
      ++numDigits;
      ++pos;
    }

    if (numDigits == 0 && hasLeadingZeroes) {
      numDigits = 1;
    }

    if (numDigits == 0 || is_letter(*pos) || *pos == '_') {
      return false;
    }
    else if (numDigits > 10) {
      // Overflow, literal value is larger than an int can hold.
      // FIXME: this won't catch *all* cases of overflow, make it exact.
      return false;
    }

    if (val != nullptr) {
      *val = negative ? -localVal : localVal;
    }
    if (end != nullptr) {
      *end = pos;
    }
    return true;
  }


  static bool double_literal(const char* start, char const** end, double* val)
  {
    const char* pos = start;

    bool negative = false;
    if (*pos == '-') {
      negative = true;
      ++pos;
    }
    else if (*pos == '+') {
      ++pos;
    }

    double localVal = 0.0;

    bool hasIntDigits = is_digit(*pos);
    if (hasIntDigits) {
      do {
        localVal = localVal * 10.0 + kDoubleDigits[*pos - '0'];
        ++pos;
      } while (is_digit(*pos));
    }
    else if (*pos != '.') {
//      set_error("Not a floating point number");
      return false;
    }

    bool hasFracDigits = false;
    if (*pos == '.') {
      ++pos;
      hasFracDigits = is_digit(*pos);
      if (hasFracDigits) {
        double scale = 0.1;
        do {
          localVal += scale * kDoubleDigits[*pos - '0'];
          scale *= 0.1;
          ++pos;
        } while (is_digit(*pos));
      }
      else if (!hasIntDigits) {
//        set_error("Floating point number has no digits before or after the decimal point");
        return false;
      }
    }

    bool hasExponent = *pos == 'e' || *pos == 'E';
    if (hasExponent) {
      ++pos;
      bool negativeExponent = false;
      if (*pos == '-') {
        negativeExponent = true;
        ++pos;
      }
      else if (*pos == '+') {
        ++pos;
      }

      if (!is_digit(*pos)) {
//        set_error("Floating point exponent has no digits");
        return false; // error: exponent part has no digits.
      }

      double exponent = 0.0;
      do {
        exponent = exponent * 10.0 + kDoubleDigits[*pos - '0'];
        ++pos;
      } while (is_digit(*pos));

      if (val != nullptr) {
        if (negativeExponent) {
          exponent = -exponent;
        }
        localVal *= std::pow(10.0, exponent);
      }
    }

    if (*pos == '.' || *pos == '_' || is_alnum(*pos)) {
//      set_error("Floating point number has trailing chars");
      return false;
    }

    if (negative) {
      localVal = -localVal;
    }

    if (val != nullptr) {
      *val = localVal;
    }
    if (end != nullptr) {
      *end = pos;
    }
    return true;
  }


  static bool float_literal(const char* start, char const** end, float* val)
  {
    double tmp = 0.0;
    bool ok = double_literal(start, end, &tmp);
    if (ok && val != nullptr) {
      *val = static_cast<float>(tmp);
    }
    return ok;
  }


  static bool match_chars(const char* expected, const char* start, char const** end)
  {
    const char* pos = start;
    while (*pos == *expected && *expected != '\0') {
      ++pos;
      ++expected;
    }
    if (*expected != '\0') {
      return false;
    }
    if (end != nullptr) {
      *end = pos;
    }
    return true;
  }


  static bool match_keyword(const char* expected, const char* start, const char** end)
  {
    const char* tmp = nullptr;
    if (!match_chars(expected, start, &tmp) || is_keyword_part(*tmp)) {
      return false;
    }
    if (end != nullptr) {
      *end = tmp;
    }
    return true;
  }


  static inline float degrees_to_radians(float degrees)
  {
    return degrees * kPi / 180.0f;
  }


  static void xyz_to_rgb(const float xyz[3], float rgb[3])
  {
    rgb[0] =  3.240479f * xyz[0] - 1.537150f * xyz[1] - 0.498535f * xyz[2];
    rgb[1] = -0.969256f * xyz[0] + 1.875991f * xyz[1] + 0.041556f * xyz[2];
    rgb[2] =  0.055648f * xyz[0] - 0.204043f * xyz[1] + 1.057311f * xyz[2];
  }


  struct SpectrumEntry {
    float wavelength, value;
  };


  static bool sort_by_wavelength(const SpectrumEntry& lhs, const SpectrumEntry& rhs)
  {
    return lhs.wavelength < rhs.wavelength;
  }


  static float lerp(float t, float v1, float v2)
  {
    return (1.0f - t) * v1 + t * v2;
  }


  static float average_over_curve(const float x[], const float y[], uint32_t n, float x0, float x1)
  {
    // NOTE: we assume the x values are already sorted in increasing order and
    // that there are no duplicate values.

    assert(x0 <= x1);
    if (x1 <= x[0]) {
      return y[0];
    }
    if (x0 >= x[n - 1]) {
      return y[n - 1];
    }
    if (n == 1) {
      return y[0];
    }

    float sum = 0.0f;
    float xRange = x1 - x0;

    if (x1 > x[n-1]) {
      sum += y[n-1] * (x1 - x[n-1]);
      x1 = x[n-1];
    }

    // Find the first sample at or after x0 and account for the sum of the
    // partial section leading up to it.
    int i = 0;
    if (x0 <= x[0]) {
      sum += y[0] * (x[0] - x0);
      x0 = x[0];
    }
    else {
      while (x[i] < x0) {
        i++;
      }
      float t0 = (x0 - x[i-1]) / (x[i] - x[i-1]);
      float y0 = y[i-1] + (y[i] - y[i-1]) * t0;
      sum += (y0 + y[i]) * 0.5f * (x[i] - x0);
    }

    while (x[i] < x1) {
      sum += (y[i] + y[i+1]) * 0.5f * (x[i+1] - x[1]);
      ++i;
    }

    if (x1 < x[i]) {
      float t0 = (x1 - x[i-1]) / (x[i] - x[i-1]);
      float y1 = y[i-1] + (y[i] - y[i-1]) * t0;
      sum -= (y[i] + y1) * 0.5f * (x[i] - x1);
    }

    return sum / xRange;
  }


  static float* X = nullptr;
  static float* Y = nullptr;
  static float* Z = nullptr;

  static void spectrum_init()
  {
    // Compute XYZ matching functions for SampledSpectrum
    X = new float[nSpectralSamples];
    Y = new float[nSpectralSamples];
    Z = new float[nSpectralSamples];
    for (int i = 0; i < nSpectralSamples; ++i) {
      float wl0 = lerp(float(i) / float(nSpectralSamples), sampledLambdaStart, sampledLambdaEnd);
      float wl1 = lerp(float(i + 1) / float(nSpectralSamples), sampledLambdaStart, sampledLambdaEnd);
      X[i] = average_over_curve(CIE_lambda, CIE_X, nCIESamples, wl0, wl1);
      Y[i] = average_over_curve(CIE_lambda, CIE_Y, nCIESamples, wl0, wl1);
      Z[i] = average_over_curve(CIE_lambda, CIE_Z, nCIESamples, wl0, wl1);
    }
  }


  static void spectrum_to_xyz(float spectrum[], uint32_t numEntries, float xyz[3])
  {
    SpectrumEntry* entries = reinterpret_cast<SpectrumEntry*>(spectrum);

    // Check whether the spectrum is sorted.
    bool isSorted = true;
    for (uint32_t i = 1; i < numEntries; i++) {
      if (entries[i].wavelength < entries[i - 1].wavelength) {
        isSorted = false;
        break;
      }
    }

    // If necessary, sort the spectrum.
    if (!isSorted) {
      std::sort(entries, entries + numEntries, sort_by_wavelength);
    }

    // Split the spectrum into separate wavelength & value arrays.
    float* wavelength = new float[numEntries];
    float* value = new float[numEntries];
    for (uint32_t i = 0; i < numEntries; i++) {
      wavelength[i] = entries[i].wavelength;
      value[i] = entries[i].value;
    }

    // Integrate the spectrum against each of the X, Y and Z curves.
    xyz[0] = 0.0f;
    xyz[1] = 0.0f;
    xyz[2] = 0.0f;
    for (int i = 0; i < nSpectralSamples; i++) {
      float wl0 = lerp(float(i) / float(nSpectralSamples), sampledLambdaStart, sampledLambdaEnd);
      float wl1 = lerp(float(i + 1) / float(nSpectralSamples), sampledLambdaStart, sampledLambdaEnd);
      float val = average_over_curve(wavelength, value, numEntries, wl0, wl1);
      xyz[0] += X[i] * val;
      xyz[0] += Y[i] * val;
      xyz[0] += Z[i] * val;
    }
    float scale = float(sampledLambdaEnd - sampledLambdaStart) / float(CIE_Y_integral * nSpectralSamples);
    xyz[0] *= scale;
    xyz[1] *= scale;
    xyz[2] *= scale;
  }


  static void spectrum_to_rgb(float spectrum[], uint32_t numEntries, float rgb[3])
  {
    float xyz[3];
    spectrum_to_xyz(spectrum, numEntries, xyz);
    xyz_to_rgb(xyz, rgb);
  }


  static void blackbody_to_xyz(const float blackbody[2], float xyz[3])
  {
    const float c = 299792458.0f;
    const float h = 6.62606957e-34f;
    const float kb = 1.3806488e-23f;

    float t = blackbody[0]; // temperature in Kelvin

    xyz[0] = 0.0f;
    xyz[1] = 0.0f;
    xyz[2] = 0.0f;
    for (int i = 0; i < nSpectralSamples; i++) {
      float wl = lerp(float(i) / float(nSpectralSamples), sampledLambdaStart, sampledLambdaEnd);

      // Calculate `Le`, the amount of light emitted at wavelength = `wl`.
      // `wl` is deliberately chose to match the wavelengths at which the X, Y
      // and Z curves are sampled.
      float l = wl * 1e-9f;
      float lambda5 = (l * l) * (l * l) * l;
      float Le = (2.0f * h * c * c) / (lambda5 * (std::exp((h * c) / (l * kb  * t)) - 1));

      xyz[0] += X[i] * Le;
      xyz[1] += Y[i] * Le;
      xyz[2] += Z[i] * Le;
    }

    float scale = blackbody[1] * float(sampledLambdaEnd - sampledLambdaStart) / float(CIE_Y_integral * nSpectralSamples);
    xyz[0] *= scale;
    xyz[1] *= scale;
    xyz[2] *= scale;
  }


  static void blackbody_to_rgb(const float blackbody[2], float rgb[3])
  {
    float xyz[3];
    blackbody_to_xyz(blackbody, xyz);
    xyz_to_rgb(xyz, rgb);
  }


  //
  // Mat4 public methods
  //

  void Mat4::identity()
  {
    std::memset(rows, 0, sizeof(rows));
    rows[0][0] = rows[1][1] = rows[2][2] = rows[3][3] = 1.0f;
  }


  void Mat4::translate(Vec3 v)
  {
    // See NOTES.md for the working out.
    rows[0][3] += rows[0][0] * v.x + rows[0][1] * v.y + rows[0][2] * v.z;
    rows[1][3] += rows[1][0] * v.x + rows[1][1] * v.y + rows[1][2] * v.z;
    rows[2][3] += rows[2][0] * v.x + rows[2][1] * v.y + rows[2][2] * v.z;
    rows[3][3] += rows[3][0] * v.x + rows[3][1] * v.y + rows[3][2] * v.z;
  }


  void Mat4::scale(Vec3 v)
  {
    // See NOTES.md for the working out.
    rows[0][0] *= v.x;
    rows[0][1] *= v.y;
    rows[0][2] *= v.z;

    rows[1][0] *= v.x;
    rows[1][1] *= v.y;
    rows[1][2] *= v.z;

    rows[2][0] *= v.x;
    rows[2][1] *= v.y;
    rows[2][2] *= v.z;

    rows[3][0] *= v.x;
    rows[3][1] *= v.y;
    rows[3][2] *= v.z;
  }


  void Mat4::rotate(const float angleRadians, Vec3 axis)
  {
    // See NOTES.md for the working out.
    float c = std::cos(angleRadians);
    float s = std::sin(angleRadians);

    Vec3 u = normalize(axis);

    float a[4][4];
    std::memcpy(a, rows, sizeof(a));

    float b[3][3] = {
      { u.x * u.x * (1.0f - c) + c,        u.x * u.y * (1.0f - c) - u.z * s,  u.x * u.z * (1.0f - c) + u.y * s },
      { u.y * u.x * (1.0f - c) + u.z * s,  u.y * u.y * (1.0f - c) + c,        u.y * u.z * (1.0f - c) - u.x * s },
      { u.z * u.x * (1.0f - c) - u.y * s,  u.z * u.y * (1.0f - c) + u.x * s,  u.z * u.z * (1.0f - c) + c       },
    };

    rows[0][0] = a[0][0] * b[0][0] + a[0][1] * b[1][0] + a[0][2] * b[2][0];
    rows[0][1] = a[0][0] * b[0][1] + a[0][1] * b[1][1] + a[0][2] * b[2][1];
    rows[0][2] = a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2] * b[2][2];

    rows[1][0] = a[1][0] * b[0][0] + a[1][1] * b[1][0] + a[1][2] * b[2][0];
    rows[1][1] = a[1][0] * b[0][1] + a[1][1] * b[1][1] + a[1][2] * b[2][1];
    rows[1][2] = a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2] * b[2][2];

    rows[2][0] = a[2][0] * b[0][0] + a[2][1] * b[1][0] + a[2][2] * b[2][0];
    rows[2][1] = a[2][0] * b[0][1] + a[2][1] * b[1][1] + a[2][2] * b[2][1];
    rows[2][2] = a[2][0] * b[0][2] + a[2][1] * b[1][2] + a[2][2] * b[2][2];

    rows[3][0] = a[3][0] * b[0][0] + a[3][1] * b[1][0] + a[3][2] * b[2][0];
    rows[3][1] = a[3][0] * b[0][1] + a[3][1] * b[1][1] + a[3][2] * b[2][1];
    rows[3][2] = a[3][0] * b[0][2] + a[3][1] * b[1][2] + a[3][2] * b[2][2];
  }


  void Mat4::lookAt(Vec3 pos, Vec3 target, Vec3 up)
  {
    // This implementation could be made faster, but lookAt will usually only
    // be called no more than once per input file so raw speed isn't as
    // important as keeping the code clear in this case.

    Vec3 dir = normalize(target - pos);
    Vec3 xAxis = normalize(cross(normalize(up), dir));
    Vec3 yAxis = normalize(cross(dir, xAxis));

    Mat4 m;
    m.rows[0][0] = xAxis.x;
    m.rows[1][0] = xAxis.y;
    m.rows[2][0] = xAxis.z;
    m.rows[3][0] = 0.0f;

    m.rows[0][1] = yAxis.x;
    m.rows[1][1] = yAxis.y;
    m.rows[2][1] = yAxis.z;
    m.rows[3][1] = 0.0f;

    m.rows[0][2] = dir.x;
    m.rows[1][2] = dir.y;
    m.rows[2][2] = dir.z;
    m.rows[3][2] = 0.0f;

    m.rows[0][3] = pos.x;
    m.rows[1][3] = pos.y;
    m.rows[2][3] = pos.z;
    m.rows[3][3] = 1.0f;

    m = inverse(m);

    concatTransform(m);
  }


  void Mat4::concatTransform(const Mat4& m)
  {
    float a[4][4];
    std::memcpy(a, rows, sizeof(a));

    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        rows[r][c] = a[r][0] * m.rows[0][c] + a[r][1] * m.rows[1][c] + a[r][2] * m.rows[2][c] + a[r][3] * m.rows[3][c];
      }
    }
  }


  // Determinant of the 2x2 matrix formed from rows r0,r1 and columns c0,c1 of
  // a 4x4 matrix. This is a helper method used for calculating the inverse of
  // the 4x4 matrix.
  float Mat4::det2x2(int r0, int r1, int c0, int c1) const
  {
    return rows[r0][c0] * rows[r1][c1] - rows[r0][c1] * rows[r1][c0];
  }


  static Mat4 inverse(const Mat4& m)
  {
    float A = m.det2x2(2, 3, 2, 3);
    float B = m.det2x2(2, 3, 1, 3);
    float C = m.det2x2(2, 3, 1, 2);
    float D = m.det2x2(2, 3, 0, 3);
    float E = m.det2x2(2, 3, 0, 2);
    float F = m.det2x2(2, 3, 0, 1);
    float G = m.det2x2(1, 3, 2, 3);
    float H = m.det2x2(1, 3, 1, 3);
    float I = m.det2x2(1, 3, 1, 2);
    float J = m.det2x2(1, 3, 0, 3);
    float K = m.det2x2(1, 3, 0, 2);
    float L = m.det2x2(1, 3, 0, 1);
    float M = m.det2x2(1, 2, 2, 3);
    float N = m.det2x2(1, 2, 1, 3);
    float O = m.det2x2(1, 2, 1, 2);
    float P = m.det2x2(1, 2, 0, 3);
    float Q = m.det2x2(1, 2, 0, 2);
    float R = m.det2x2(1, 2, 0, 1);

    Mat4 inv;

    inv.rows[0][0] = +(m.rows[1][1] * A - m.rows[1][2] * B + m.rows[1][3] * C);
    inv.rows[0][1] = -(m.rows[0][1] * A - m.rows[0][2] * B + m.rows[0][3] * C);
    inv.rows[0][2] = +(m.rows[0][1] * G - m.rows[0][2] * H + m.rows[0][3] * I);
    inv.rows[0][3] = -(m.rows[0][1] * M - m.rows[0][2] * N + m.rows[0][3] * O);

    inv.rows[1][0] = -(m.rows[1][0] * A - m.rows[1][2] * D + m.rows[1][3] * E);
    inv.rows[1][1] = +(m.rows[0][0] * A - m.rows[0][2] * D + m.rows[0][3] * E);
    inv.rows[1][2] = -(m.rows[0][0] * G - m.rows[0][2] * J + m.rows[0][3] * K);
    inv.rows[1][3] = +(m.rows[0][0] * M - m.rows[0][2] * P + m.rows[0][3] * Q);

    inv.rows[2][0] = +(m.rows[1][0] * B - m.rows[1][1] * D + m.rows[1][3] * F);
    inv.rows[2][1] = -(m.rows[0][0] * B - m.rows[0][1] * D + m.rows[0][3] * F);
    inv.rows[2][2] = +(m.rows[0][0] * H - m.rows[0][1] * J + m.rows[0][3] * L);
    inv.rows[2][3] = -(m.rows[0][0] * N - m.rows[0][1] * P + m.rows[0][3] * R);

    inv.rows[3][0] = -(m.rows[1][0] * C - m.rows[1][1] * E + m.rows[1][2] * F);
    inv.rows[3][1] = +(m.rows[0][0] * C - m.rows[0][1] * E + m.rows[0][2] * F);
    inv.rows[3][2] = -(m.rows[0][0] * I - m.rows[0][1] * K + m.rows[0][2] * L);
    inv.rows[3][3] = +(m.rows[0][0] * O - m.rows[0][1] * Q + m.rows[0][2] * R);

    float det = m.rows[0][0] * inv.rows[0][0] +
                m.rows[0][1] * inv.rows[1][0] +
                m.rows[0][2] * inv.rows[2][0] +
                m.rows[0][3] * inv.rows[3][0];
    float invDet = 1.0f / det;

    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        inv.rows[r][c] *= invDet;
      }
    }
    
    return inv;
  }


  //
  // TransformStack public methods
  //

  TransformStack::TransformStack()
  {
    matrices[0][0].identity();
    matrices[0][1].identity();
  }


  TransformStack::~TransformStack()
  {
    for (auto& entry : coordinateSystems) {
      delete[] entry.second;
    }
  }


  bool TransformStack::push()
  {
    if (entry == kMaxTransformStackEntry) {
      return false;
    }
    matrices[entry + 1][0] = matrices[entry][0];
    matrices[entry + 1][1] = matrices[entry][1];
    ++entry;
    return true;
  }


  bool TransformStack::pop()
  {
    if (entry == 0) {
      return false;
    }
    --entry;
    return true;
  }


  void TransformStack::clear()
  {
    entry = 0;
    matrices[entry][0].identity();
    matrices[entry][1].identity();
  }


  void TransformStack::identity()
  {
    if (active[0]) {
      matrices[entry][0].identity();
    }
    if (active[1]) {
      matrices[entry][1].identity();
    }
  }


  void TransformStack::translate(Vec3 t)
  {
    if (active[0]) {
      matrices[entry][0].translate(t);
    }
    if (active[1]) {
      matrices[entry][1].translate(t);
    }
  }


  void TransformStack::scale(Vec3 s)
  {
    if (active[0]) {
      matrices[entry][0].scale(s);
    }
    if (active[1]) {
      matrices[entry][1].scale(s);
    }
  }


  void TransformStack::rotate(float angleRadians, Vec3 axis)
  {
    if (active[0]) {
      matrices[entry][0].rotate(angleRadians, axis);
    }
    if (active[1]) {
      matrices[entry][1].rotate(angleRadians, axis);
    }
  }


  void TransformStack::lookAt(Vec3 pos, Vec3 target, Vec3 up)
  {
    if (active[0]) {
      matrices[entry][0].lookAt(pos, target, up);
    }
    if (active[1]) {
      matrices[entry][1].lookAt(pos, target, up);
    }
  }


  void TransformStack::transform(const Mat4& m)
  {
    if (active[0]) {
      matrices[entry][0] = m;
    }
    if (active[1]) {
      matrices[entry][1] = m;
    }
  }


  void TransformStack::concatTransform(const Mat4& m)
  {
    if (active[0]) {
      matrices[entry][0].concatTransform(m);
    }
    if (active[1]) {
      matrices[entry][1].concatTransform(m);
    }
  }


  void TransformStack::coordinateSystem(const char *name)
  {
    Mat4* m = new Mat4[2];
    std::memcpy(m, matrices[entry], sizeof(Mat4) * 2);

    auto it = coordinateSystems.find(name);
    if (it != coordinateSystems.end()) {
      delete[] it->second;
      it->second = m;
    }
    else {
      coordinateSystems[name] = m;
    }
  }


  bool TransformStack::coordSysTransform(const char* name)
  {
    auto it = coordinateSystems.find(name);
    if (it == coordinateSystems.end()) {
      return false;
    }
    Mat4* m = it->second;
    std::memcpy(matrices[entry], m, sizeof(Mat4) * 2);
    return true;
  }


  //
  // Attributes methods
  //

  Attributes::Attributes()
  {
    floatTextures.reserve(16);
    spectrumTextures.reserve(16);
    materials.reserve(16);
  }


  //
  // AttributeStack methods
  //

  AttributeStack::AttributeStack()
  {
    top = attrs + entry;
  }


  bool AttributeStack::push()
  {
    if (entry == kMaxAttributeStackEntry) {
      return false;
    }

    attrs[entry + 1].activeMaterial     = attrs[entry].activeMaterial;
    attrs[entry + 1].areaLight          = attrs[entry].areaLight;
    attrs[entry + 1].insideMedium       = attrs[entry].insideMedium;
    attrs[entry + 1].outsideMedium      = attrs[entry].outsideMedium;
    attrs[entry + 1].reverseOrientation = attrs[entry].reverseOrientation;

    ++entry;
    top = attrs + entry;

    return true;
  }


  bool AttributeStack::pop()
  {
    if (entry == 0) {
      return false;
    }

    top->floatTextures.clear();
    top->spectrumTextures.clear();
    top->materials.clear();

    --entry;
    top = attrs + entry;
    return true;
  }


  void AttributeStack::clear()
  {
    entry = 0;
    top = attrs;
  }


  //
  // Camera methods
  //

  static void compute_camera_defaults(const Film* film, float& frameaspectratio, float screenwindow[4])
  {
    if (frameaspectratio <= 0.0f) {
      frameaspectratio = film->get_aspect_ratio();
    }

    if (screenwindow[1] <= screenwindow[0] ||
        screenwindow[3] <= screenwindow[2]) {
      if (frameaspectratio >= 1.0f) {
        screenwindow[0] = -frameaspectratio;
        screenwindow[1] =  frameaspectratio;
        screenwindow[2] = -1.0f;
        screenwindow[3] =  1.0f;
      }
      else {
        screenwindow[0] = -1.0f;
        screenwindow[1] =  1.0f;
        screenwindow[2] = -frameaspectratio;
        screenwindow[3] =  frameaspectratio;
      }
    }
  }


  void PerspectiveCamera::compute_defaults(const Film* film)
  {
    compute_camera_defaults(film, frameaspectratio, screenwindow);
  }


  void OrthographicCamera::compute_defaults(const Film* film)
  {
    compute_camera_defaults(film, frameaspectratio, screenwindow);
  }


  void EnvironmentCamera::compute_defaults(const Film* film)
  {
    compute_camera_defaults(film, frameaspectratio, screenwindow);
  }


  //
  // Integrator methods
  //

  void compute_integrator_pixelbounds(const Film* film, int pixelbounds[4])
  {
    if (pixelbounds[1] > pixelbounds[0] && pixelbounds[3] > pixelbounds[2]) {
      return;
    }
    pixelbounds[0] = 0;
    pixelbounds[2] = 0;
    film->get_resolution(pixelbounds[1], pixelbounds[2]);
  }


  void BDPTIntegrator::compute_defaults(const Film *film)
  {
    compute_integrator_pixelbounds(film, pixelbounds);
  }


  void DirectLightingIntegrator::compute_defaults(const Film *film)
  {
    compute_integrator_pixelbounds(film, pixelbounds);
  }


  void PathIntegrator::compute_defaults(const Film *film)
  {
    compute_integrator_pixelbounds(film, pixelbounds);
  }


  void WhittedIntegrator::compute_defaults(const Film *film)
  {
    compute_integrator_pixelbounds(film, pixelbounds);
  }


  void VolPathIntegrator::compute_defaults(const Film *film)
  {
    compute_integrator_pixelbounds(film, pixelbounds);
  }


  void AOIntegrator::compute_defaults(const Film *film)
  {
    compute_integrator_pixelbounds(film, pixelbounds);
  }


  //
  // Shape methods
  //

  void Shape::copy_common_properties(const Shape* other)
  {
    shapeToWorld       = other->shapeToWorld;
    material           = other->material;
    areaLight          = other->areaLight;
    insideMedium       = other->insideMedium;
    outsideMedium      = other->outsideMedium;
    reverseOrientation = other->reverseOrientation;
    instanced          = other->instanced;
  }


  //
  // HeightField methods
  //

  TriangleMesh* HeightField::triangle_mesh() const
  {
    TriangleMesh* trimesh = new TriangleMesh();

    trimesh->num_vertices = uint32_t(nu * nv);
    trimesh->P = new float[trimesh->num_vertices * 3];
    float* dstP = trimesh->P;
    const float* srcZ = Pz;
    for (int v = 0; v < nv; v++) {
      for (int u = 0; u < nu; u++) {
        dstP[0] = float(u);
        dstP[1] = float(v);
        dstP[2] = *srcZ;
        dstP += 3;
        srcZ++;
      }
    }

    trimesh->num_indices = uint32_t((nu - 1) * (nv - 1) * 6);
    trimesh->indices = new int[trimesh->num_indices];
    int* dst = trimesh->indices;
    for (int v = 0, endV = nv - 1; v < endV; v++) {
      for (int u = 0, endU = nu - 1; u < endU; u++) {
        dst[0] = nu * v + u;
        dst[1] = dst[0] + nu;
        dst[2] = dst[0] + 1;

        dst[3] = nu * (v + 1) + (u + 1);
        dst[4] = dst[3] - nu;
        dst[5] = dst[3] - 1;

        dst += 6;
      }
    }

    trimesh->copy_common_properties(this);

    return trimesh;
  }


  //
  // LoopSubdiv methods
  //

  TriangleMesh* LoopSubdiv::triangle_mesh() const
  {
    // For now we just return the control mesh, we don't subdivide it at all.

    TriangleMesh* trimesh = new TriangleMesh();

    trimesh->num_vertices = num_points;
    trimesh->P = new float[num_points * 3];
    std::memcpy(trimesh->P, P, sizeof(float) * num_points * 3);

    trimesh->num_indices = num_indices;
    trimesh->indices = new int[num_indices];
    std::memcpy(trimesh->indices, indices, sizeof(int) * num_indices);

    trimesh->copy_common_properties(this);

    return trimesh;
  }


  //
  // Nurbs methods
  //

  TriangleMesh* Nurbs::triangle_mesh() const
  {
    // For now we just return the control mesh, we don't subdivide it at all.

    TriangleMesh* trimesh = new TriangleMesh();

    trimesh->num_vertices = uint32_t(nu * nv);
    trimesh->P = new float[trimesh->num_vertices * 3];
    if (P != nullptr) {
      std::memcpy(trimesh->P, P, sizeof(float) * trimesh->num_vertices * 3);
    }
    else {
      float* dst = trimesh->P;
      const float* src = Pw;
      for (uint32_t i = 0; i < trimesh->num_vertices; i++) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst += 3;
        src += 4;
      }
    }

    trimesh->num_indices = uint32_t((nu - 1) * (nv - 1) * 6);
    trimesh->indices = new int[trimesh->num_indices];
    int* dst = trimesh->indices;
    for (int v = 0, endV = nv - 1; v < endV; v++) {
      for (int u = 0, endU = nu - 1; u < endU; u++) {
        dst[0] = nu * v + u;
        dst[1] = dst[0] + nu;
        dst[2] = dst[0] + 1;

        dst[3] = nu * (v + 1) + (u + 1);
        dst[4] = dst[3] - nu;
        dst[5] = dst[3] - 1;

        dst += 6;
      }
    }

    trimesh->copy_common_properties(this);

    return trimesh;
  }


  //
  // PLYMesh public methods
  //

  TriangleMesh* PLYMesh::triangle_mesh() const
  {
    miniply::PLYReader reader(filename);
    if (!reader.valid()) {
      return nullptr;
    }

    TriangleMesh* trimesh = new TriangleMesh();
    bool gotVerts = false;
    bool gotFaces = false;
    while (reader.has_element() && (!gotVerts || !gotFaces)) {
      if (!gotVerts && reader.element_is("vertex")) {
        if (!reader.load_element()) {
          break;
        }
        uint32_t propIdxs[3];
        if (!reader.find_pos(propIdxs)) {
          break;
        }
        trimesh->num_vertices = reader.num_rows();
        trimesh->P = new float[trimesh->num_vertices * 3];
        reader.extract_properties(propIdxs, 3, miniply::PLYPropertyType::Float, trimesh->P);
        if (reader.find_normal(propIdxs)) {
          trimesh->N = new float[trimesh->num_vertices * 3];
          reader.extract_properties(propIdxs, 3, miniply::PLYPropertyType::Float, trimesh->N);
        }
        if (reader.find_texcoord(propIdxs)) {
          trimesh->uv = new float[trimesh->num_vertices * 2];
          reader.extract_properties(propIdxs, 2, miniply::PLYPropertyType::Float, trimesh->uv);
        }
        gotVerts = true;
      }
      else if (!gotFaces && reader.element_is("face")) {
        if (!reader.load_element()) {
          break;
        }
        uint32_t propIdx;
        if (!reader.find_indices(&propIdx)) {
          break;
        }
        bool polys = reader.requires_triangulation(propIdx);
        if (polys && !gotVerts) {
          fprintf(stderr, "Error: face data needing triangulation found before vertex data.\n");
          break;
        }
        if (polys) {
          trimesh->num_indices = reader.num_triangles(propIdx) * 3;
          trimesh->indices = new int[trimesh->num_indices];
          reader.extract_triangles(propIdx, trimesh->P, trimesh->num_vertices, miniply::PLYPropertyType::Int, trimesh->indices);
        }
        else {
          trimesh->num_indices = reader.num_rows() * 3;
          trimesh->indices = new int[trimesh->num_indices];
          reader.extract_list_property(propIdx, miniply::PLYPropertyType::Int, trimesh->indices);
        }
        gotFaces = true;
      }
      reader.next_element();
    }

    if (!gotVerts || !gotFaces) {
      delete trimesh;
      return nullptr;
    }

    trimesh->copy_common_properties(this);
    trimesh->alpha = alpha;
    trimesh->shadowalpha = shadowalpha;

    return trimesh;
  }


  //
  // Scene public methods
  //

  Scene::Scene()
  {
  }


  Scene::~Scene()
  {
    delete accelerator;
    delete camera;
    delete film;
    delete filter;
    delete integrator;
    delete sampler;

    for (Shape* shape : shapes) {
      delete shape;
    }
    for (Object* object : objects) {
      delete object;
    }
    for (Instance* instance : instances) {
      delete instance;
    }
    for (Light* light : lights) {
      delete light;
    }
    for (AreaLight* areaLight : areaLights) {
      delete areaLight;
    }
    for (Material* material : materials) {
      delete material;
    }
    for (Texture* texture : textures) {
      delete texture;
    }
    for (Medium* medium : mediums) {
      delete medium;
    }
  }


  bool Scene::to_triangle_mesh(uint32_t shapeIndex)
  {
    assert(shapeIndex < shapes.size());

    TriangleMesh* trimesh = shapes[shapeIndex]->triangle_mesh();
    if (trimesh != nullptr) {
      delete shapes[shapeIndex];
      shapes[shapeIndex] = trimesh;
      return true;
    }
    else {
      return false;
    }
  }


  bool Scene::shapes_to_triangle_mesh(Bits<ShapeType> typesToConvert)
  {
    for (uint32_t i = 0, endi = static_cast<uint32_t>(shapes.size()); i < endi; i++) {
      if (typesToConvert.contains(shapes[i]->type()) && !to_triangle_mesh(i)) {
        return false;
      }
    }
    return true;
  }


  bool Scene::all_to_triangle_mesh()
  {
    Bits<ShapeType> types;
    types.setAll();
    types.clear(ShapeType::TriangleMesh);
    return shapes_to_triangle_mesh(types);
  }


  bool Scene::load_all_ply_meshes()
  {
    return shapes_to_triangle_mesh(ShapeType::PLYMesh);
  }


  //
  // Error public methods
  //

  Error::Error(const char* theFilename, int64_t theOffset, const char* theMessage)
  {
    m_filename = copy_string(theFilename);
    m_offset = theOffset;
    m_message = copy_string(theMessage);
  }


  Error::~Error()
  {
    delete[] m_filename;
    delete[] m_message;
  }


  const char* Error::filename() const
  {
    return m_filename;
  }


  const char* Error::message() const
  {
    return m_message;
  }


  int64_t Error::offset() const
  {
    return m_offset;
  }


  int64_t Error::line() const
  {
    return m_line;
  }


  int64_t Error::column() const
  {
    return m_column;
  }


  bool Error::has_line_and_column() const
  {
    return m_line > 0 && m_column > 0;
  }


  void Error::set_line_and_column(int64_t theLine, int64_t theColumn)
  {
    m_line = theLine;
    m_column = theColumn;
  }


  int Error::compare(const Error& rhs) const
  {
    int tmp = strcmp(m_filename, rhs.m_filename);
    if (tmp != 0) {
      return tmp;
    }
    if (m_offset != rhs.m_offset) {
      return (m_offset < rhs.m_offset) ? 1 : -1;
    }
    return 0;
  }


  //
  // Tokenizer public methods
  //

  Tokenizer::Tokenizer()
  {
  }


  Tokenizer::~Tokenizer()
  {
    if (m_fileData != nullptr) {
      for (uint32_t i = 0; i <= m_includeDepth; i++) {
        if (m_fileData[i].f != nullptr) {
          fclose(m_fileData[i].f);
        }
        delete[] m_fileData[i].filename;
      }
    }
    delete[] m_fileData;
    delete[] m_buf;
    delete[] m_tmpBuf;
    delete m_error;
  }


  void Tokenizer::set_buffer_capacity(size_t n)
  {
    assert(m_buf == nullptr); // can't change the buffer capacity after it's been created.
    assert(n > 0);
    m_bufCapacity = n;
  }


  void Tokenizer::set_max_include_depth(uint32_t n)
  {
    assert(m_fileData == nullptr); // can't change the max include depth once the m_fileData array has been created.
    m_maxIncludeDepth = n;
  }


  bool Tokenizer::open_file(const char* filename)
  {
    assert(m_fileData == nullptr);
    assert(m_buf == nullptr);

    if (filename == nullptr) {
      set_error("No filename provided");
      return false;
    }

    FILE* f = nullptr;
    if (file_open(&f, filename, "rb") != 0) {
      set_error("Failed to open %s", filename);
      return false;
    }

    // Create the fileData array and the input buffer. This freezes their configuration settings.
    m_fileData = new FileData[m_maxIncludeDepth + 1];
    m_fileData[0].filename = copy_string(filename);
    m_fileData[0].f = f;
    m_fileData[0].atEOF = false;
    m_fileData[0].bufOffset = 0;
    m_includeDepth = 0;

    m_buf = new char[m_bufCapacity + 1];
    m_buf[m_bufCapacity] = '\0';
    m_bufEnd = m_buf + m_bufCapacity;
    m_pos = m_bufEnd;
    m_end = m_bufEnd;

    m_tmpBuf = new char[kTempBufferCapacity + 1];
    m_tmpBuf[kTempBufferCapacity] = '\0';

    return refill_buffer();
  }


  bool Tokenizer::eof() const
  {
    return m_fileData[m_includeDepth].atEOF && m_pos == m_bufEnd;
  }


  bool Tokenizer::advance()
  {
    // Preconditions
    assert(m_end >= m_buf && m_end <= m_bufEnd); // Cursor end position is somewhere in the current buffer.

    m_pos = m_end;

    bool skipLine = false;
    while (true) {
      if (skipLine) {
        // Skip to end of current line, e.g. if we're in a line comment.
        while (*m_pos != '\n' && *m_pos != '\0') {
          ++m_pos;
        }
        if (*m_pos == '\n') {
          skipLine = false;
          continue;
        }
      }
      else {
        // Skip whitespace.
        while (is_whitespace(*m_pos) || *m_pos == '\n') {
          ++m_pos;
        }
        if (*m_pos == '#') {
          ++m_pos;
          skipLine = true;
          continue;
        }
      }

      // If we're at the end of the current buffer, try to load some more. If
      // that's successful, then continue advancing. Otherwise we're at the
      // end of the file so our work here is done.
      if (m_pos == m_bufEnd) {
        m_end = m_pos;
        if (refill_buffer()) {
          continue;
        }
        else {
          break;
        }
      }

      // If we got here, we've reached a non-whitespace char that isn't the end
      // of the file and isn't part of a comment.
      break;
    }
    m_end = m_pos;

    // Postconditions:
    // - m_pos == m_end
    // - If *m_pos == '\0' it means we're at the end of the file.
    return *m_pos != '\0';
  }


  bool Tokenizer::refill_buffer()
  {
    FileData* fdata = &m_fileData[m_includeDepth];
    if (fdata->atEOF) {
      if (m_includeDepth == 0 || fdata->reportEOF) {
        return false;
      }
      else {
        return pop_file();
      }
    }

    // Move everything from the start of the current token onwards, to the
    // start of the read buffer.
    size_t bufSize = static_cast<size_t>(m_bufEnd - m_buf);
    if (bufSize < m_bufCapacity) {
      m_buf[bufSize] = m_buf[m_bufCapacity];
      m_buf[m_bufCapacity] = '\0';
      m_bufEnd = m_buf + m_bufCapacity;
    }
    size_t keep = static_cast<size_t>(m_bufEnd - m_pos);
    if (keep > 0 && m_pos > m_buf) {
      std::memmove(m_buf, m_pos, sizeof(char) * keep);
      fdata->bufOffset += static_cast<int64_t>(m_pos - m_buf);
    }
    m_end = m_buf + (m_end - m_pos);
    m_pos = m_buf;

    size_t fetched = fread(m_buf + keep, sizeof(char), m_bufCapacity - keep, fdata->f) + keep;
    fdata->atEOF = fetched < m_bufCapacity;
    m_bufEnd = m_buf + fetched;

    // Trim the buffer back to the last non-token char, so that we don't have
    // to worry about tokens being split between two buffers. This means we
    // only need to check for the end-of-buffer condition in `advance()`, so
    // our token parsing methods stay nice and efficient.
    if (!fdata->atEOF) {
      while (m_bufEnd > m_buf && !is_safe_buffer_end(m_bufEnd[-1])) {
        --m_bufEnd;
      }
      if (m_bufEnd == m_buf) {
        // Failed to backtrack to last safe char - there's no whitespace or
        // newlines anywhere in the buffer!?!
        return false;
      }
      m_buf[m_bufCapacity] = *m_bufEnd;
    }
    *m_bufEnd = '\0';

    return true;
  }


  bool Tokenizer::push_file(const char *filename, bool reportEOF)
  {
    assert(m_fileData != nullptr);
    assert(m_buf != nullptr);

    if (filename == nullptr) {
      set_error("No filename provided");
      return false;
    }
    else if (m_includeDepth == m_maxIncludeDepth) {
      set_error("Maximum include depth exceeded");
      return false;
    }

    resolve_file(filename, m_fileData[0].filename, m_tmpBuf, kTempBufferCapacity);

    FILE* f = nullptr;
    if (file_open(&f, m_tmpBuf, "rb") != 0) {
      set_error("Failed to include %s, full path = %s", filename, m_tmpBuf);
      return false;
    }

    // Adjust the current file pointer so it will be correct after the file we're about to push is popped.
    m_fileData[m_includeDepth].bufOffset += static_cast<int64_t>(m_end - m_buf);
    file_seek(m_fileData[m_includeDepth].f, m_fileData[m_includeDepth].bufOffset, SEEK_SET);

    m_includeDepth++;

    FileData* fdata = &m_fileData[m_includeDepth];
    fdata->filename = copy_string(m_tmpBuf);
    fdata->f = f;
    fdata->atEOF = false;
    fdata->reportEOF = reportEOF;
    fdata->bufOffset = 0;

    m_bufEnd = m_buf + m_bufCapacity;
    m_pos = m_bufEnd;
    m_end = m_bufEnd;
    return refill_buffer();
  }


  bool Tokenizer::pop_file()
  {
    if (m_includeDepth == 0) {
      set_error("Attempted to pop the original input file");
      return false;
    }

    FileData* fdata = &m_fileData[m_includeDepth];
    fclose(fdata->f);
    delete[] fdata->filename;
    fdata->f = nullptr;
    fdata->filename = nullptr;
    fdata->atEOF = false;
    fdata->reportEOF = false;
    fdata->bufOffset = 0;

    --m_includeDepth;

    m_fileData[m_includeDepth].atEOF = false;
    m_bufEnd = m_buf + m_bufCapacity;
    m_pos = m_bufEnd;
    m_end = m_bufEnd;
    return refill_buffer();
  }


  void Tokenizer::cursor_location(int64_t* line, int64_t* col)
  {
    const FileData* fdata = &m_fileData[m_includeDepth];

    const int64_t posOffset = static_cast<int64_t>(m_pos - m_buf) + fdata->bufOffset;

    int64_t localLine = 1;
    int64_t newline = -1;

    if (fdata->bufOffset == 0) {
      for (int64_t j = 0; j < posOffset; j++) {
        if (m_buf[j] == '\n') {
          ++localLine;
          newline = j;
        }
      }
      *line = localLine;
      *col = posOffset - newline;
      return;
    }

    int64_t oldFileOffset = file_pos(fdata->f); // So we can restore it once we're done.

    char* tmpBuf = new char[m_bufCapacity];
    int64_t tmpBufOffset = 0;
    int64_t n;

    file_seek(fdata->f, 0, SEEK_SET);
    for (int64_t i = 0, endI = posOffset / int64_t(m_bufCapacity); i < endI; i++) {
      n = int64_t(fread(tmpBuf, sizeof(char), m_bufCapacity, fdata->f));
      for (int64_t j = 0, endJ = int64_t(n); j < endJ; j++) {
        if (tmpBuf[j] == '\n') {
          ++localLine;
          newline = j + i * int64_t(m_bufCapacity);
        }
      }
      tmpBufOffset += n;
    }
    n = int64_t(fread(tmpBuf, sizeof(char), m_bufCapacity, fdata->f));
    for (int64_t j = 0, endJ = std::min(posOffset - tmpBufOffset, n); j < endJ; j++) {
      if (tmpBuf[j] == '\n') {
        ++localLine;
        newline = j + tmpBufOffset;
      }
    }

    delete[] tmpBuf;

    *line = localLine;
    *col = posOffset - newline;

    file_seek(fdata->f, oldFileOffset, SEEK_SET); // Restore the old file position.
  }


  bool Tokenizer::string_literal(char buf[], size_t bufLen)
  {
    m_end = m_pos;

    if (*m_end != '"') {
      return false;
    }

    do {
      ++m_end;
      if (m_end == m_bufEnd) {
        if (m_pos == m_buf) {
          // Can't refill the buffer, the string is already occupying all of it!
          set_error("String literal exceeds input buffer size (maximum length = %u)", m_bufCapacity);
          return false;
        }
        else if (!refill_buffer()) {
          // At EOF and the string literal hasn't been terminated.
          set_error("String literal is not terminated");
          return false;
        }
      }
      // XXX: Should we disallow multiline strings here?
    } while (*m_end != '"');

    // `m_end` must be the closing double-quote char if we got here.
    ++m_end;

    // We've found the whole of the string. If the caller has provided an
    // output buffer, copy the string into it *without* the surrounding double
    // quotes.
    if (buf != nullptr && bufLen > 0) {
      size_t tokenLen = static_cast<size_t>(m_end - m_pos) - 2;
      if (tokenLen + 1 > bufLen) {
        set_error("String literal is too large for the provided output buffer (maximum length = %llu)", uint64_t(bufLen));
        return false; // string too large for the output buffer.
      }
      std::memcpy(buf, m_pos + 1, tokenLen);
      buf[tokenLen] = '\0';
    }
    return true;
  }


  bool Tokenizer::int_literal(int* val)
  {
    m_end = m_pos;
    return minipbrt::int_literal(m_pos, &m_end, val);
  }


  bool Tokenizer::float_literal(float* val)
  {
    m_end = m_pos;
    return minipbrt::float_literal(m_pos, &m_end, val);
  }


  bool Tokenizer::float_array(float* arr, size_t arrLen)
  {
    for (size_t i = 0; i < arrLen; i++) {
      bool ok = advance() && float_literal((arr != nullptr) ? (arr + i) : nullptr);
      if (!ok) {
        set_error("expected %llu float values but only got %llu", uint64_t(arrLen), uint64_t(i));
        return false;
      }
    }
    return true;
  }


  bool Tokenizer::identifier(char buf[], size_t bufLen)
  {
    m_end = m_pos;

    if (!is_letter(*m_end) && *m_end != '_') {
      set_error("Not an identifier");
      return false;
    }

    do {
      ++m_end;
    } while (is_keyword_part(*m_end));

    if (buf != nullptr && bufLen > 0) {
      size_t len = static_cast<size_t>(m_end - m_pos);
      if (len >= bufLen) {
        set_error("Identifier is too large to store in the output buffer (maximum length = %llu)", static_cast<uint64_t>(bufLen));
        return false;
      }
      std::memcpy(buf, m_pos, sizeof(char) * len);
      buf[len] = '\0';
    }
    return true;
  }


  bool Tokenizer::which_directive(uint32_t *index)
  {
    for (uint32_t i = 0; i < kNumStatements; i++) {
      if (match_keyword(kStatements[i].name, m_pos, &m_end)) {
        if (index != nullptr) {
          *index = i;
        }
        return true;
      }
    }
    return false;
  }


  bool Tokenizer::which_type(uint32_t *index)
  {
    for (uint32_t i = 0; i < kNumParamTypes; i++) {
      if (match_keyword(kParamTypes[i].name, m_pos, &m_end) ||
          (kParamTypes[i].alias != nullptr && match_keyword(kParamTypes[i].alias, m_pos, &m_end))) {
        if (index != nullptr) {
          *index = i;
          return true;
        }
      }
    }
    return false;
  }


  bool Tokenizer::match_symbol(const char *str)
  {
    assert(str != nullptr);
    return match_chars(str, m_pos, &m_end);
  }


  bool Tokenizer::which_string_literal(const char* values[], int* index)
  {
    if (*m_pos != '"') {
      return false;
    }
    const char* pos = m_pos + 1;
    for (int i = 0; values[i] != nullptr; i++) {
      m_end = pos;
      const char* str = values[i];
      while (*m_end == *str && *str != '\0') {
        ++m_end;
        ++str;
      }
      if (*str == '\0' && *m_end == '"') {
        if (index != nullptr) {
          *index = i;
        }
        ++m_end;
        return true;
      }
    }

    m_end = pos;
    while (*m_end != '"' && *m_end != '\0') {
      ++m_end;
    }
    if (*m_end == '\"') {
      ++m_end;
    }
    return false;
  }


  bool Tokenizer::which_keyword(const char* values[], int* index)
  {
    for (int i = 0; values[i] != nullptr; i++) {
      m_end = m_pos;
      const char* str = values[i];
      while (*m_end == *str && *str != '\0') {
        ++m_end;
        ++str;
      }
      if (*str == '\0' && !is_keyword_part(*m_end)) {
        if (index != nullptr) {
          *index = i;
          return true;
        }
      }
    }
    return false;
  }


  size_t Tokenizer::token_length() const
  {
    return static_cast<size_t>(m_end - m_pos);
  }


  const char* Tokenizer::token() const
  {
    return m_pos;
  }


  const char* Tokenizer::get_filename() const
  {
    return m_fileData[m_includeDepth].filename;
  }


  const char* Tokenizer::get_original_filename() const
  {
    return m_fileData[0].filename;
  }


  void Tokenizer::set_error(const char* fmt, ...)
  {
    if (has_error()) {
      return;
    }

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);

    if (len <= 0) {
      return;
    }

    if (m_error != nullptr) {
      delete m_error;
      m_error = nullptr;
    }

    FileData* fdata = &m_fileData[m_includeDepth];
    int64_t errorOffset = fdata->bufOffset + static_cast<int64_t>(m_pos - m_buf);

    char* errorMessage = new char[size_t(len + 1)];
    va_start(args, fmt);
    vsnprintf(errorMessage, size_t(len + 1), fmt, args);
    va_end(args);

    m_error = new Error(fdata->filename, errorOffset, errorMessage);

    int64_t errorLine, errorCol;
    cursor_location(&errorLine, &errorCol);
    m_error->set_line_and_column(errorLine, errorCol);
  }


  bool Tokenizer::has_error() const
  {
    return m_error != nullptr;
  }


  const Error* Tokenizer::get_error() const
  {
    return m_error;
  }



  char* Tokenizer::temp_buffer()
  {
    return m_tmpBuf;
  }


  //
  // Parser public methods
  //

  Parser::Parser()
  {
    m_transforms = new TransformStack();
    m_attrs = new AttributeStack();

    m_scene = new Scene();
    m_scene->accelerator = new BVHAccelerator();
    m_scene->film = new ImageFilm();
    m_scene->filter = new BoxFilter();
    m_scene->integrator = new PathIntegrator();
    m_scene->sampler = new HaltonSampler();
  }


  Parser::~Parser()
  {
    delete m_transforms;
    delete m_attrs;
    delete m_scene;

    for (auto it : m_paramNames) {
      delete[] it.second;
    }
  }


  Tokenizer& Parser::tokenizer()
  {
    return m_tokenizer;
  }


  bool Parser::parse(const char* filename)
  {
    bool ok = m_tokenizer.open_file(filename);
    if (!ok) {
      return false;
    }

    if (X == nullptr || Y == nullptr || Z == nullptr) {
      spectrum_init();
    }

    m_inWorld = false;
    while (ok && m_tokenizer.advance()) {
      ok = parse_statement();
    }

    return ok;
  }


  bool Parser::has_error() const
  {
    return m_tokenizer.has_error();
  }


  const Error* Parser::get_error() const
  {
    return m_tokenizer.get_error();
  }


  Scene* Parser::take_scene()
  {
    Scene* scene = m_scene;
    m_scene = nullptr;
    return scene;
  }


  Scene* Parser::borrow_scene()
  {
    return m_scene;
  }


  //
  // Parser private methods
  //

  bool Parser::parse_statement()
  {
    m_statementIndex = uint32_t(-1);
    bool ok = m_tokenizer.which_directive(&m_statementIndex);
    if (!ok) {
      m_tokenizer.set_error("Unknown statement");
      return false;
    }

    if (m_temp.capacity() > kMaxReservedTempSpace) {
      m_temp.resize(kMaxReservedTempSpace);
      m_temp.shrink_to_fit();
    }
    m_params.clear();
    m_temp.clear();

    const StatementDeclaration& statement = kStatements[m_statementIndex];

    bool allowed = m_inWorld ? statement.inWorld : statement.inPreamble;
    if (!allowed) {
      m_tokenizer.set_error("%s is not allowed in the %s section", statement.name, m_inWorld ? "world" : "preamble");
      return false;
    }

    ok = parse_args(statement) && parse_params();
    if (!ok) {
      m_tokenizer.set_error("Failed to parse parameters for %s", statement.name);
      return false;
    }

    switch (statement.id) {
      case StatementID::Identity:           ok = parse_Identity(); break;
      case StatementID::Translate:          ok = parse_Translate(); break;
      case StatementID::Scale:              ok = parse_Scale(); break;
      case StatementID::Rotate:             ok = parse_Rotate(); break;
      case StatementID::LookAt:             ok = parse_LookAt(); break;
      case StatementID::CoordinateSystem:   ok = parse_CoordinateSystem(); break;
      case StatementID::CoordSysTransform:  ok = parse_CoordSysTransform(); break;
      case StatementID::Transform:          ok = parse_Transform(); break;
      case StatementID::ConcatTransform:    ok = parse_ConcatTransform(); break;
      case StatementID::ActiveTransform:    ok = parse_ActiveTransform(); break;
      case StatementID::MakeNamedMedium:    ok = parse_MakeNamedMedium(); break;
      case StatementID::MediumInterface:    ok = parse_MediumInterface(); break;
      case StatementID::Include:            ok = parse_Include(); break;
      case StatementID::AttributeBegin:     ok = parse_AttributeBegin(); break;
      case StatementID::AttributeEnd:       ok = parse_AttributeEnd(); break;
      case StatementID::Shape:              ok = parse_Shape(); break;
      case StatementID::AreaLightSource:    ok = parse_AreaLightSource(); break;
      case StatementID::LightSource:        ok = parse_LightSource(); break;
      case StatementID::Material:           ok = parse_Material(); break;
      case StatementID::MakeNamedMaterial:  ok = parse_MakeNamedMaterial(); break;
      case StatementID::NamedMaterial:      ok = parse_NamedMaterial(); break;
      case StatementID::ObjectBegin:        ok = parse_ObjectBegin(); break;
      case StatementID::ObjectEnd:          ok = parse_ObjectEnd(); break;
      case StatementID::ObjectInstance:     ok = parse_ObjectInstance(); break;
      case StatementID::Texture:            ok = parse_Texture(); break;
      case StatementID::TransformBegin:     ok = parse_TransformBegin(); break;
      case StatementID::TransformEnd:       ok = parse_TransformEnd(); break;
      case StatementID::ReverseOrientation: ok = parse_ReverseOrientation(); break;
      case StatementID::WorldEnd:           ok = parse_WorldEnd(); break;
      case StatementID::Accelerator:        ok = parse_Accelerator(); break;
      case StatementID::Camera:             ok = parse_Camera(); break;
      case StatementID::Film:               ok = parse_Film(); break;
      case StatementID::Integrator:         ok = parse_Integrator(); break;
      case StatementID::PixelFilter:        ok = parse_PixelFilter(); break;
      case StatementID::Sampler:            ok = parse_Sampler(); break;
      case StatementID::TransformTimes:     ok = parse_TransformTimes(); break;
      case StatementID::WorldBegin:         ok = parse_WorldBegin(); break;
    }

    if (!ok) {
      m_tokenizer.set_error("Failed to parse %s", statement.name);
    }

    return ok;
  }


  bool Parser::parse_args(const StatementDeclaration& statement)
  {
    const char* fmt = statement.argPattern;
    bool parsedEnum = false;
    int tmpInt;
    float tmpFloat;
    bool ok = true;

    bool bracketed = m_tokenizer.advance() && m_tokenizer.match_symbol("[");

    while (*fmt && ok) {
      if (!m_tokenizer.advance()) {
        ok = false;
        break;
      }

      switch (*fmt) { // arg is an enum string
      case 'e':
        ok = m_tokenizer.which_string_literal(parsedEnum ? statement.enum1 : statement.enum0, &tmpInt);
        if (!ok) {
          tmpInt = parsedEnum ? statement.enum1default : statement.enum0default;
          ok = true;
        }
        parsedEnum = true;
        if (ok) {
          m_params.push_back(ParamInfo{ "", ParamType::Int, m_temp.size(), 1 });
          push_bytes(&tmpInt, sizeof(tmpInt));
        }
        break;

      case 'k':
        ok = m_tokenizer.which_keyword(parsedEnum ? statement.enum1 : statement.enum0, &tmpInt);
        if (!ok) {
          tmpInt = parsedEnum ? statement.enum1default : statement.enum0default;
          ok = true;
        }
        parsedEnum = true;
        if (ok) {
          m_params.push_back(ParamInfo{ "", ParamType::Int, m_temp.size(), 1 });
          push_bytes(&tmpInt, sizeof(tmpInt));
        }
        break;

      case 'f':
        ok = m_tokenizer.float_literal(&tmpFloat);
        if (ok) {
          m_params.push_back(ParamInfo{ "", ParamType::Float, m_temp.size(), 1 });
          push_bytes(&tmpFloat, sizeof(tmpFloat));
        }
        break;

      case 's':
        ok = m_tokenizer.string_literal(nullptr, 0);
        if (ok) {
          m_params.push_back(ParamInfo{ "", ParamType::String, m_temp.size(), 1 });
          push_string(m_tokenizer.token() + 1, m_tokenizer.token_length() - 2); // The +1 and -2 are trimming off the surrounding double quote chars.
        }
        break;

      default:
        assert(false); // invalid type code.
        break;
      }

      ++fmt;
    }

    if (ok && bracketed) {
      ok = m_tokenizer.advance() && m_tokenizer.match_symbol("]");
    }

    if (!ok) {
      m_tokenizer.set_error("Failed to parse required args for %s", statement.name);
      return false;
    }
    return true;
  }


  bool Parser::parse_Identity()
  {
    m_transforms->identity();
    return true;
  }


  bool Parser::parse_Translate()
  {
    Vec3 v{ float_arg(0), float_arg(1), float_arg(2) };
    m_transforms->translate(v);
    return true;
  }


  bool Parser::parse_Scale()
  {
    Vec3 v{ float_arg(0), float_arg(1), float_arg(2) };
    m_transforms->scale(v);
    return true;
  }


  bool Parser::parse_Rotate()
  {
    float angleDegrees = float_arg(0);
    Vec3 axis{ float_arg(1), float_arg(2), float_arg(3) };
    m_transforms->rotate(degrees_to_radians(angleDegrees), axis);
    return true;
  }


  bool Parser::parse_LookAt()
  {
    Vec3 pos{ float_arg(0), float_arg(1), float_arg(2) };
    Vec3 target{ float_arg(3), float_arg(4), float_arg(5) };
    Vec3 up{ float_arg(6), float_arg(7), float_arg(8) };
    m_transforms->lookAt(pos, target, up);
    return true;
  }


  bool Parser::parse_CoordinateSystem()
  {
    const char* name = string_arg(0);
    m_transforms->coordinateSystem(name);
    return true;
  }


  bool Parser::parse_CoordSysTransform()
  {
    const char* name = string_arg(0);
    if (!m_transforms->coordSysTransform(name)) {
      m_tokenizer.set_error("Coordinate system '%s' has not been defined", name);
      return false;
    }
    return true;
  }


  bool Parser::parse_Transform()
  {
    // Somewhat bizarrely, the Transform parameters are in column-major
    // order even though PBRT's Matrix4x4 class uses row-major order.
    Mat4 m;
    m.rows[0][0] = float_arg(0);
    m.rows[1][0] = float_arg(1);
    m.rows[2][0] = float_arg(2);
    m.rows[3][0] = float_arg(3);
    m.rows[0][1] = float_arg(4);
    m.rows[1][1] = float_arg(5);
    m.rows[2][1] = float_arg(6);
    m.rows[3][1] = float_arg(7);
    m.rows[0][2] = float_arg(8);
    m.rows[1][2] = float_arg(9);
    m.rows[2][2] = float_arg(10);
    m.rows[3][2] = float_arg(11);
    m.rows[0][3] = float_arg(12);
    m.rows[1][3] = float_arg(13);
    m.rows[2][3] = float_arg(14);
    m.rows[3][3] = float_arg(15);
    m_transforms->transform(m);
    return true;
  }


  bool Parser::parse_ConcatTransform()
  {
    // Somewhat bizarrely, the ConcatTransform parameters are in column-major
    // order even though PBRT's Matrix4x4 class uses row-major order.
    Mat4 m;
    m.rows[0][0] = float_arg(0);
    m.rows[1][0] = float_arg(1);
    m.rows[2][0] = float_arg(2);
    m.rows[3][0] = float_arg(3);
    m.rows[0][1] = float_arg(4);
    m.rows[1][1] = float_arg(5);
    m.rows[2][1] = float_arg(6);
    m.rows[3][1] = float_arg(7);
    m.rows[0][2] = float_arg(8);
    m.rows[1][2] = float_arg(9);
    m.rows[2][2] = float_arg(10);
    m.rows[3][2] = float_arg(11);
    m.rows[0][3] = float_arg(12);
    m.rows[1][3] = float_arg(13);
    m.rows[2][3] = float_arg(14);
    m.rows[3][3] = float_arg(15);
    m_transforms->concatTransform(m);
    return true;
  }


  bool Parser::parse_ActiveTransform()
  {
    int transformIndex = enum_arg(0);
    if (transformIndex == 0) { // StartTime
      m_transforms->active[0] = true;
      m_transforms->active[1] = false;
    }
    else if (transformIndex == 1) { // EndTime
      m_transforms->active[0] = false;
      m_transforms->active[1] = true;
    }
    else { // All
      m_transforms->active[0] = true;
      m_transforms->active[1] = true;
    }
    return true;
  }


  bool Parser::parse_MakeNamedMedium()
  {
    const char* mediumName = string_arg(0);

    MediumType mediumType;
    if (!typed_enum_param("type", kMediumTypes, &mediumType)) {
      m_tokenizer.set_error("Unknown medium type");
      return false;
    }

    Medium* medium = nullptr;
    if (mediumType == MediumType::Homogeneous) { // homogeneous
      medium = new HomogeneousMedium();
    }
    else if (mediumType == MediumType::Heterogeneous) { // heterogeneous
      HeterogeneousMedium* heterogeneous = new HeterogeneousMedium();
      medium = heterogeneous;
      float_array_param("p0", ParamType::Point3, 3, heterogeneous->p0);
      float_array_param("p1", ParamType::Point3, 3, heterogeneous->p1);
      int_param("nx", &heterogeneous->nx);
      int_param("ny", &heterogeneous->ny);
      int_param("nz", &heterogeneous->nz);
      if (heterogeneous->nx < 1 || heterogeneous->ny < 1 || heterogeneous->nz < 1) {
        m_tokenizer.set_error("Invalid density grid dimensions for heterogeneous medium '%s'", mediumName);
        delete medium;
        return false;
      }

      const ParamInfo* densityDesc = find_param("density", ParamType::Float);
      if (densityDesc != nullptr) {
        uint32_t densityLen = static_cast<uint32_t>(heterogeneous->nx) *
                              static_cast<uint32_t>(heterogeneous->ny) *
                              static_cast<uint32_t>(heterogeneous->nz);
        if (densityDesc->count != densityLen) {
          m_tokenizer.set_error("Invalid density data for heterogeneous medium '%s'", mediumName);
          delete medium;
          return false;
        }
        heterogeneous->density = new float[densityLen];
        float_array_param("density", ParamType::Float, densityLen, heterogeneous->density);
      }
    }

    // Common params for all types.
    spectrum_param("sigma_a", medium->sigma_a);
    spectrum_param("sigma_s", medium->sigma_s);
    string_param("preset", &medium->preset, true);
    float_param("g", &medium->g);
    float_param("scale", &medium->scale);

    medium->mediumName = copy_string(string_arg(0));

    // Add the medium to the scene.
    m_scene->mediums.push_back(medium);

    return true;
  }


  bool Parser::parse_MediumInterface()
  {
    const char* inside = string_arg(0);
    const char* outside = string_arg(1);

    uint32_t insideMedium = kInvalidIndex;
    if (m_inWorld && inside[0] != '\0') {
      insideMedium = find_medium(inside);
      if (insideMedium == kInvalidIndex) {
        // TODO: warn/error about invalid medium name
      }
    }

    uint32_t outsideMedium = kInvalidIndex;
    if (outside[0] != '\0') {
      outsideMedium = find_medium(outside);
      if (outsideMedium == kInvalidIndex) {
        // TODO: warn/error about invalid medium name.
      }
    }

    m_attrs->top->insideMedium = insideMedium;
    m_attrs->top->outsideMedium = outsideMedium;
    return true;
  }


  bool Parser::parse_Include()
  {
    const char* path = string_arg(0);
    return m_tokenizer.push_file(path, false);
  }


  bool Parser::parse_AttributeBegin()
  {
    if (!m_transforms->push()) {
      m_tokenizer.set_error("Exceeded maximum transform stack size");
      return false;
    }
    if (!m_attrs->push()) {
      m_tokenizer.set_error("Exceeded maximum attribute stack size");
      return false;
    }
    return true;
  }


  bool Parser::parse_AttributeEnd()
  {
    if (!m_attrs->pop()) {
      m_tokenizer.set_error("Cannot pop last attribute set off the stack");
      return false;
    }
    if (!m_transforms->pop()) {
      m_tokenizer.set_error("Cannot pop last transform set off the stack");
      return false;
    }
    return true;
  }


  bool Parser::parse_Shape()
  {
    ShapeType shapeType = typed_enum_arg<ShapeType>(0);
    Shape* shape = nullptr;
    bool ok = true;

    switch (shapeType) {
    case ShapeType::Cone: // cone
      {
        Cone* cone = new Cone();
        float_param("radius", &cone->radius);
        float_param("height", &cone->height);
        float_param("phimax", &cone->phimax);
        shape = cone;
      }
      break;

    case ShapeType::Curve: // curve
      {
        const char* basisValues[] = { "bezier", "bspline", nullptr };
        const char* curvetypeValues[] = { "flat", "ribbon", "cylinder", nullptr };
        Curve* curve = new Curve();
        typed_enum_param("basis", basisValues, &curve->basis);
        if (int_param("degree", reinterpret_cast<int*>(&curve->degree))) {
          if (curve->degree < 2 || curve->degree > 3) {
            m_tokenizer.set_error("Invalid value for \"degree\" parameter, must be either 2 or 3");
            delete curve;
            return false;
          }
        }
        ok = float_vector_param("P", ParamType::Point3, &curve->num_P, &curve->P, true);
        if (ok) {
          curve->num_P /= 3; // there are 3 floats per control point...
          if (curve->basis == CurveBasis::Bezier) {
            ok = ((curve->num_P - 1 - curve->degree) % curve->degree) == 0;
            if (!ok) {
              m_tokenizer.set_error("Invalid number of control points for a bezier curve with degree %d", curve->degree);
            }
            curve->num_segments = (curve->num_P - 1) / curve->degree;
          }
          else {
            ok = curve->num_P > curve->degree;
            if (!ok) {
              m_tokenizer.set_error("Invalid number of control points for a bspline curve with degree %d", curve->degree);
            }
          }
        }
        if (!ok) {
          m_tokenizer.set_error("Required param \"P\" is missing or has invalid data.");
          delete curve;
          return false;
        }
        typed_enum_param("type", curvetypeValues, &curve->curvetype);
        if (curve->curvetype == CurveType::Ribbon) {
          uint32_t numNormals = 0;
          ok = float_vector_param("N", ParamType::Normal3, &numNormals, &curve->N, true);
          if (ok) {
            numNormals /= 3; // There are 3 floats per normal.
            if (numNormals != (curve->num_segments + 1)) {
              m_tokenizer.set_error("Invalid number of normals, expected %u but got %u", curve->num_segments + 1, numNormals);
              ok = false;
            }
          }
          if (!ok) {
            m_tokenizer.set_error("Required param \"N\" is missing or has invalid data.");
            delete curve;
            return false;
          }
        }
        float width;
        bool hasWidth = float_param("width", &width);
        if (!float_param("width0", &curve->width0) && hasWidth) {
          curve->width0 = width;
        }
        if (!float_param("width1", &curve->width1) && hasWidth) {
          curve->width1 = width;
        }
        int_param("splitdepth", &curve->splitdepth);
        shape = curve;
      }
      break;

    case ShapeType::Cylinder: // cylinder
      {
        Cylinder* cylinder = new Cylinder();
        float_param("radius", &cylinder->radius);
        float_param("zmin", &cylinder->zmin);
        float_param("zmax", &cylinder->zmax);
        float_param("phimax", &cylinder->phimax);
        shape = cylinder;
      }
      break;

    case ShapeType::Disk: // disk
      {
        Disk* disk = new Disk();
        float_param("height", &disk->height);
        float_param("radius", &disk->radius);
        float_param("innerradius", &disk->innerradius);
        float_param("phimax", &disk->phimax);
        shape = disk;
      }
      break;

    case ShapeType::Hyperboloid: // hyperboloid
      {
        Hyperboloid* hyperboloid = new Hyperboloid();
        float_array_param("p1", ParamType::Point3, 3, hyperboloid->p1);
        float_array_param("p2", ParamType::Point3, 3, hyperboloid->p2);
        float_param("phimax", &hyperboloid->phimax);
        shape = hyperboloid;
      }
      break;

    case ShapeType::Paraboloid: // paraboloid
      {
        Paraboloid* paraboloid = new Paraboloid();
        float_param("radius", &paraboloid->radius);
        float_param("zmin", &paraboloid->zmin);
        float_param("zmax", &paraboloid->zmax);
        float_param("phimax", &paraboloid->phimax);
        shape = paraboloid;
      }
      break;

    case ShapeType::Sphere: // sphere
      {
        Sphere* sphere = new Sphere();
        float_param("radius", &sphere->radius);
        float_param("zmin", &sphere->zmin);
        float_param("zmax", &sphere->zmax);
        float_param("phimax", &sphere->phimax);
        shape = sphere;
      }
      break;

    case ShapeType::TriangleMesh: // trianglemesh
      {
        TriangleMesh* trianglemesh = new TriangleMesh();
        ok = int_vector_param("indices", &trianglemesh->num_indices, &trianglemesh->indices, true) &&
             float_vector_param("P", ParamType::Point3, &trianglemesh->num_vertices, &trianglemesh->P, true);
        if (ok) {
          ok = (trianglemesh->num_indices % 3 == 0) && (trianglemesh->num_vertices % 3 == 0);
          if (ok) {
            trianglemesh->num_vertices /= 3;
          }
        }
        if (!ok) {
          m_tokenizer.set_error("One or more required params are missing or have invalid data.");
          delete trianglemesh;
          return false;
        }
        uint32_t count = 0;
        if (ok && float_vector_param("N", ParamType::Normal3, &count, &trianglemesh->N, true)) {
          ok = count == trianglemesh->num_vertices * 3;
        }
        if (ok && float_vector_param("S", ParamType::Vector3, &count, &trianglemesh->S, true)) {
          ok = count == trianglemesh->num_vertices * 3;
        }
        if (ok && float_vector_param("uv", ParamType::Float, &count, &trianglemesh->uv, true)) {
          ok = count == trianglemesh->num_vertices * 2;
        }
        texture_param("alpha", TextureData::Float, &trianglemesh->alpha);
        texture_param("shadowalpha", TextureData::Float, &trianglemesh->shadowalpha);
        shape = trianglemesh;
      }
      break;

    case ShapeType::HeightField: // heightfield
      {
        HeightField* heightfield = new HeightField();
        ok = int_param("nu", &heightfield->nu) &&
             int_param("nv", &heightfield->nv);
        if (!ok) {
          m_tokenizer.set_error("Missing required parameter(s) \"nu\" and/or \"nv\"");
          delete heightfield;
          return false;
        }
        heightfield->Pz = new float[heightfield->nu * heightfield->nv];
        if (!float_array_param("Pz", ParamType::Float, uint32_t(heightfield->nu * heightfield->nv), heightfield->Pz)) {
          m_tokenizer.set_error("Required parameter \"Pz\" was missing or invalid");
          delete heightfield;
          return false;
        }
        shape = heightfield;
      }
      break;

    case ShapeType::LoopSubdiv: // loopsubdiv
      {
        LoopSubdiv* loopsubdiv = new LoopSubdiv();
        int_param("levels", &loopsubdiv->levels);
        if (!int_vector_param("indices", &loopsubdiv->num_indices, &loopsubdiv->indices, true)) {
          m_tokenizer.set_error("Required parameter \"indices\" is missing");
          delete loopsubdiv;
          return false;
        }
        if (!float_vector_param("P", ParamType::Point3, &loopsubdiv->num_points, &loopsubdiv->P, true)) {
          m_tokenizer.set_error("Required parameter \"P\" is missing");
          delete loopsubdiv;
          return false;
        }
        shape = loopsubdiv;
      }
      break;

    case ShapeType::Nurbs: // nurbs
      {
        Nurbs* nurbs = new Nurbs;
        ok = int_param("nu", &nurbs->nu)         && int_param("nv", &nurbs->nv) &&
             int_param("uorder", &nurbs->uorder) && int_param("vorder", &nurbs->vorder) &&
             float_param("u0", &nurbs->u0)       && float_param("v0", &nurbs->v0) &&
             float_param("u1", &nurbs->u1)       && float_param("v1", &nurbs->v1);
        if (!ok) {
          m_tokenizer.set_error("One or more required parameters are missing.");
          delete nurbs;
          return false;
        }
        nurbs->uknots = new float[nurbs->nu + nurbs->uorder];
        nurbs->vknots = new float[nurbs->nv + nurbs->vorder];
        ok = float_array_param("uknots", ParamType::Float, uint32_t(nurbs->nu + nurbs->uorder), nurbs->uknots) &&
             float_array_param("vknots", ParamType::Float, uint32_t(nurbs->nv + nurbs->vorder), nurbs->vknots);
        if (!ok) {
          m_tokenizer.set_error("Missing or invalid data for knot arrays");
          delete nurbs;
          return false;
        }
        uint32_t count = 1;
        uint32_t divisor = 3;
        if (float_vector_param("P", ParamType::Point3, &count, &nurbs->P, true)) {
          divisor = 3;
        }
        else if (float_vector_param("Pw", ParamType::Float, &count, &nurbs->Pw, true)) {
          divisor = 4;
        }
        else {
          m_tokenizer.set_error("Both \"P\" and \"Pw\" are missing.");
          delete nurbs;
          return false;
        }
        if (count % divisor != 0 || (count / divisor) != uint32_t(nurbs->nu * nurbs->nv)) {
          m_tokenizer.set_error("Invalid NURBS control point data");
          delete nurbs;
          return false;
        }
        shape = nurbs;
      }
      break;

    case ShapeType::PLYMesh: // plymesh
      {
        PLYMesh* plymesh = new PLYMesh();
        if (!filename_param("filename", &plymesh->filename)) {
          m_tokenizer.set_error("Required parameter \"filename\" is missing.");
          delete plymesh;
          return false;
        }
        texture_param("alpha", TextureData::Float, &plymesh->alpha);
        texture_param("shadowalpha", TextureData::Float, &plymesh->shadowalpha);
        shape = plymesh;
      }
      break;
    }

    if (shape == nullptr) {
      m_tokenizer.set_error("Failed to create %s shape", kShapeTypes[uint32_t(shapeType)]);
      return false;
    }

    save_current_transform_matrices(&shape->shapeToWorld);
    shape->material = m_attrs->top->activeMaterial;
    shape->areaLight = m_attrs->top->areaLight;
    shape->insideMedium = m_attrs->top->insideMedium;
    shape->outsideMedium = m_attrs->top->outsideMedium;
    shape->reverseOrientation = m_attrs->top->reverseOrientation;
    shape->instanced = m_activeObject != kInvalidIndex;

    // Check whether the shape is overriding any properties of the active material
    if (shape->material != kInvalidIndex) {
      if (has_material_overrides(shape->material)) {
        const Material* baseMaterial = m_scene->materials[shape->material];
        parse_material_overrides(baseMaterial, &shape->material);
      }
    }

    if (m_activeObject != kInvalidIndex && m_firstShape == kInvalidIndex) {
      m_firstShape = static_cast<uint32_t>(m_scene->shapes.size());
    }

    m_scene->shapes.push_back(shape);
    return true;
  }


  bool Parser::parse_AreaLightSource()
  {
    AreaLightType areaLightType = typed_enum_arg<AreaLightType>(0);
    AreaLight* areaLight = nullptr;
    if (areaLightType == AreaLightType::Diffuse) {
      DiffuseAreaLight* diffuse = new DiffuseAreaLight();
      spectrum_param("L", diffuse->L);
      bool_param("twosided", &diffuse->twosided);
      int_param("samples", &diffuse->samples);
      areaLight = diffuse;
    }

    if (areaLight == nullptr) {
      m_tokenizer.set_error("Failed to create %s area light source", kAreaLightTypes[uint32_t(areaLightType)]);
      return false;
    }

    // Params common to all area lights.
    spectrum_param("scale", areaLight->scale);

    m_attrs->top->areaLight = static_cast<uint32_t>(m_scene->areaLights.size());
    m_scene->areaLights.push_back(areaLight);

    return true;
  }


  bool Parser::parse_LightSource()
  {
    LightType lightType = typed_enum_arg<LightType>(0);
    Light* light = nullptr;

    switch (lightType) {
    case LightType::Distant: // distant
      {
        DistantLight* distant = new DistantLight();
        spectrum_param("L", distant->L);
        float_array_param("from", ParamType::Point3, 3, distant->from);
        float_array_param("to", ParamType::Point3, 3, distant->to);
        light = distant;
      }
      break;

    case LightType::Goniometric: // goniometric
      {
        GoniometricLight* goniometric = new GoniometricLight();
        spectrum_param("I", goniometric->I);
        if (!string_param("mapname", &goniometric->mapname, true)) {
          m_tokenizer.set_error("Required parameter \"mapname\" is missing");
          delete goniometric;
          return false;
        }
        light = goniometric;
      }
      break;

    case LightType::Infinite: // infinite
      {
        InfiniteLight* infinite = new InfiniteLight();
        spectrum_param("L", infinite->L);
        int_param("samples", &infinite->samples);
        string_param("mapname", &infinite->mapname, true);
        light = infinite;
      }
      break;

    case LightType::Point: // point
      {
        PointLight* point = new PointLight();
        spectrum_param("I", point->I);
        float_array_param("from", ParamType::Point3, 3, point->from);
        light = point;
      }
      break;

    case LightType::Projection: // projection
      {
        ProjectionLight* projection = new ProjectionLight();
        spectrum_param("I", projection->I);
        float_param("fov", &projection->fov);
        if (!string_param("mapname", &projection->mapname, true)) {
          m_tokenizer.set_error("Required parameter \"mapname\" is missing");
          delete projection;
          return false;
        }
        light = projection;
      }
      break;

    case LightType::Spot: // spot
      {
        SpotLight* spot = new SpotLight();
        spectrum_param("I", spot->I);
        float_array_param("from", ParamType::Point3, 3, spot->from);
        float_array_param("to", ParamType::Point3, 3, spot->to);
        float_param("coneangle", &spot->coneangle);
        float_param("conedeltaangle", &spot->conedeltaangle);
        light = spot;
      }
      break;
    }

    if (light == nullptr) {
      m_tokenizer.set_error("Failed to create %s light source", kLightTypes[uint32_t(lightType)]);
      return false;
    }

    save_current_transform_matrices(&light->lightToWorld);
    spectrum_param("scale", light->scale);
    m_scene->lights.push_back(light);
    return true;
  }


  bool Parser::parse_Material()
  {
    MaterialType materialType;
    int materialTypeIndex = enum_arg(0);
    if (materialTypeIndex == int(MaterialType::Uber) + 1) {
      materialType = MaterialType::None;
    }
    else {
      materialType = static_cast<MaterialType>(materialTypeIndex);
    }

    uint32_t material = kInvalidIndex;
    if (parse_material_common(materialType, nullptr, &material)) {
      m_attrs->top->activeMaterial = material;
      return true;
    }
    return false;
  }


  bool Parser::parse_MakeNamedMaterial()
  {
    MaterialType materialType;
    int materialTypeIndex;
    bool ok = enum_param("type", kMaterialTypes, &materialTypeIndex);
    if (!ok) {
      m_tokenizer.set_error("Unknown or invalid material type");
      return false;
    }
    if (materialTypeIndex == int(MaterialType::Uber) + 1) {
      materialType = MaterialType::None;
    }
    else {
      materialType = static_cast<MaterialType>(materialTypeIndex);
    }

    uint32_t material = kInvalidIndex;
    if (parse_material_common(materialType, string_arg(0), &material)) {
      m_scene->materials[material]->name = copy_string(string_arg(0));
      return true;
    }
    return false;
  }


  bool Parser::parse_material_common(MaterialType materialType, const char* materialName, uint32_t* materialOut)
  {
    Material* material = nullptr;
    switch (materialType) {
    case MaterialType::Disney:
      {
        DisneyMaterial* disney = new DisneyMaterial();
        color_texture_param("color",           &disney->color);
        float_texture_param("anisotropic",     &disney->anisotropic);
        float_texture_param("clearcoat",       &disney->clearcoat);
        float_texture_param("clearcoatgloss",  &disney->clearcoatgloss);
        float_texture_param("eta",             &disney->eta);
        float_texture_param("metallic",        &disney->metallic);
        float_texture_param("roughness",       &disney->roughness);
        color_texture_param("scatterdistance", &disney->scatterdistance);
        float_texture_param("sheen",           &disney->sheen);
        float_texture_param("sheentint",       &disney->sheentint);
        float_texture_param("spectrans",       &disney->spectrans);
        float_texture_param("speculartint",    &disney->speculartint);
        bool_param("thin", &disney->thin);
        color_texture_param("difftrans",       &disney->difftrans);
        color_texture_param("flatness",        &disney->flatness);
        material = disney;
      }
      break;

    case MaterialType::Fourier:
      {
        FourierMaterial* fourier = new FourierMaterial();
        if (!string_param("bsdffile", &fourier->bsdffile, true)) {
          m_tokenizer.set_error("Required parameter \"bsdffile\" is missing or invalid");
          return false;
        }
        material = fourier;
      }
      break;

    case MaterialType::Glass:
      {
        GlassMaterial* glass = new GlassMaterial();
        color_texture_param("Kr",         &glass->Kr);
        color_texture_param("Kt",         &glass->Kt);
        float_texture_param("eta",        &glass->eta);
        float_texture_param("uroughness", &glass->uroughness);
        float_texture_param("vroughness", &glass->vroughness);
        bool_param("remaproughness",      &glass->remaproughness);
        material = glass;
      }
      break;

    case MaterialType::Hair:
      {
        HairMaterial* hair = new HairMaterial();
        hair->has_sigma_a = color_texture_param("sigma_a", &hair->sigma_a);
        hair->has_color   = color_texture_param("color",   &hair->color);
        float_texture_param("eumelanin",   &hair->eumelanin);
        float_texture_param("pheomelanin", &hair->pheomelanin);
        float_texture_param("eta",         &hair->eta);
        float_texture_param("beta_m",      &hair->beta_m);
        float_texture_param("beta_n",      &hair->beta_n);
        float_texture_param("alpha",       &hair->alpha);
        material = hair;
      }
      break;

    case MaterialType::KdSubsurface:
      {
        KdSubsurfaceMaterial* kdsubsurface = new KdSubsurfaceMaterial();
        color_texture_param("Kd",         &kdsubsurface->Kd);
        color_texture_param("mfp",        &kdsubsurface->mfp);
        float_texture_param("eta",        &kdsubsurface->eta);
        color_texture_param("Kr",         &kdsubsurface->Kr);
        color_texture_param("Kt",         &kdsubsurface->Kt);
        float_texture_param("uroughness", &kdsubsurface->uroughness);
        float_texture_param("vroughness", &kdsubsurface->vroughness);
        bool_param("remaproughness",      &kdsubsurface->remaproughness);
        material = kdsubsurface;
      }
      break;

    case MaterialType::Matte:
      {
        MatteMaterial* matte = new MatteMaterial();
        color_texture_param("Kd",    &matte->Kd);
        float_texture_param("sigma", &matte->sigma);
        material = matte;
      }
      break;

    case MaterialType::Metal:
      {
        MetalMaterial* metal = new MetalMaterial();
        color_texture_param("eta",        &metal->eta);
        color_texture_param("k",          &metal->k);
        float_texture_param("uroughness", &metal->uroughness);
        float_texture_param("vroughness", &metal->vroughness);
        bool_param("remaproughness",      &metal->remaproughness);
        material = metal;
      }
      break;

    case MaterialType::Mirror:
      {
        MirrorMaterial* mirror = new MirrorMaterial();
        color_texture_param("Kr", &mirror->Kr);
        material = mirror;
      }
      break;

    case MaterialType::Mix:
      {
        char* tmp = nullptr;
        MixMaterial* mix = new MixMaterial();
        color_texture_param("amount", &mix->amount);
        if (string_param("namedmaterial1", &tmp)) {
          mix->namedmaterial1 = find_material(tmp);
        }
        if (string_param("namedmaterial2", &tmp)) {
          mix->namedmaterial2 = find_material(tmp);
        }
        material = mix;
      }
      break;

    case MaterialType::None:
      material = new NoneMaterial();
      break;

    case MaterialType::Plastic:
      {
        PlasticMaterial* plastic = new PlasticMaterial();
        color_texture_param("Kd",        &plastic->Kd);
        color_texture_param("Ks",        &plastic->Ks);
        float_texture_param("roughness", &plastic->roughness);
        bool_param("remaproughness",     &plastic->remaproughness);
        material = plastic;
      }
      break;

    case MaterialType::Substrate:
      {
        SubstrateMaterial* substrate = new SubstrateMaterial();
        color_texture_param("Kd",         &substrate->Kd);
        color_texture_param("Ks",         &substrate->Ks);
        float_texture_param("uroughness", &substrate->uroughness);
        float_texture_param("vroughness", &substrate->vroughness);
        bool_param("remaproughness",      &substrate->remaproughness);
        material = substrate;
      }
      break;

    case MaterialType::Subsurface:
      {
        SubsurfaceMaterial* subsurface = new SubsurfaceMaterial();
        string_param("name",                 &subsurface->coefficients, true);
        color_texture_param("sigma_a",       &subsurface->sigma_a);
        color_texture_param("sigma_prime_s", &subsurface->sigma_prime_s);
        float_param("scale",                 &subsurface->scale);
        float_texture_param("eta",           &subsurface->eta);
        color_texture_param("Kr",            &subsurface->Kr);
        color_texture_param("Kt",            &subsurface->Kt);
        float_texture_param("uroughness",    &subsurface->uroughness);
        float_texture_param("vroughness",    &subsurface->vroughness);
        bool_param("remaproughness",         &subsurface->remaproughness);
        material = subsurface;
      }
      break;

    case MaterialType::Translucent:
      {
        TranslucentMaterial* translucent = new TranslucentMaterial();
        color_texture_param("Kd",        &translucent->Kd);
        color_texture_param("Ks",        &translucent->Ks);
        color_texture_param("reflect",   &translucent->reflect);
        color_texture_param("transmit",  &translucent->transmit);
        float_texture_param("roughness", &translucent->roughness);
        bool_param("remaproughness",     &translucent->remaproughness);
        material = translucent;
      }
      break;

    case MaterialType::Uber:
      {
        UberMaterial* uber = new UberMaterial();
        color_texture_param("Kd",         &uber->Kd);
        color_texture_param("Ks",         &uber->Ks);
        color_texture_param("reflect",    &uber->Kr);
        color_texture_param("transmit",   &uber->Kt);
        float_texture_param("eta",        &uber->eta);
        color_texture_param("opacity",    &uber->opacity);
        float_texture_param("uroughness", &uber->uroughness);
        float_texture_param("vroughness", &uber->vroughness);
        bool_param("remaproughness",      &uber->remaproughness);
        material = uber;
      }
      break;
    }

    if (material == nullptr) {
      m_tokenizer.set_error("Failed to create material");
      return false;
    }

    texture_param("bumpmap", TextureData::Float, &material->bumpmap);

    if (materialName != nullptr) {
      material->name = copy_string(materialName);
    }

    if (materialOut != nullptr) {
      *materialOut = static_cast<uint32_t>(m_scene->materials.size());
    }
    m_scene->materials.push_back(material);
    return true;
  }


  bool Parser::parse_material_overrides(const Material* baseMaterial, uint32_t* materialOut)
  {
    Material* material = nullptr;

    switch (baseMaterial->type()) {
    case MaterialType::Disney:
      {
        const DisneyMaterial* src = dynamic_cast<const DisneyMaterial*>(baseMaterial);
        DisneyMaterial* dst = new DisneyMaterial();
        color_texture_param_with_default("color",           &dst->color,           &src->color);
        float_texture_param_with_default("anisotropic",     &dst->anisotropic,     &src->anisotropic);
        float_texture_param_with_default("clearcoat",       &dst->clearcoat,       &src->clearcoat);
        float_texture_param_with_default("clearcoatgloss",  &dst->clearcoatgloss,  &src->clearcoatgloss);
        float_texture_param_with_default("eta",             &dst->eta,             &src->eta);
        float_texture_param_with_default("metallic",        &dst->metallic,        &src->metallic);
        float_texture_param_with_default("roughness",       &dst->roughness,       &src->roughness);
        color_texture_param_with_default("scatterdistance", &dst->scatterdistance, &src->scatterdistance);
        float_texture_param_with_default("sheen",           &dst->sheen,           &src->sheen);
        float_texture_param_with_default("sheentint",       &dst->sheentint,       &src->sheentint);
        float_texture_param_with_default("spectrans",       &dst->spectrans,       &src->spectrans);
        float_texture_param_with_default("speculartint",    &dst->speculartint,    &src->speculartint);
        bool_param_with_default         ("thin",            &dst->thin,            src->thin);
        color_texture_param_with_default("difftrans",       &dst->difftrans,       &src->difftrans);
        color_texture_param_with_default("flatness",        &dst->flatness,        &src->flatness);
        material = dst;
      }
      break;

    case MaterialType::Fourier:
      {
        const FourierMaterial* src = dynamic_cast<const FourierMaterial*>(baseMaterial);
        FourierMaterial* dst = new FourierMaterial();
        string_param_with_default("bsdffile", &dst->bsdffile, src->bsdffile, true);
        material = dst;
      }
      break;

    case MaterialType::Glass:
      {
        const GlassMaterial* src = dynamic_cast<const GlassMaterial*>(baseMaterial);
        GlassMaterial* dst = new GlassMaterial();
        color_texture_param_with_default("Kr",             &dst->Kr,             &src->Kr);
        color_texture_param_with_default("Kt",             &dst->Kt,             &src->Kt);
        float_texture_param_with_default("eta",            &dst->eta,            &src->eta);
        float_texture_param_with_default("uroughness",     &dst->uroughness,     &src->uroughness);
        float_texture_param_with_default("vroughness",     &dst->vroughness,     &src->vroughness);
        bool_param_with_default         ("remaproughness", &dst->remaproughness, src->remaproughness);
        material = dst;
      }
      break;

    case MaterialType::Hair:
      {
        const HairMaterial* src = dynamic_cast<const HairMaterial*>(baseMaterial);
        HairMaterial* dst = new HairMaterial();
        dst->has_sigma_a = color_texture_param("sigma_a", &dst->sigma_a);
        if (!dst->has_sigma_a && src->has_sigma_a) {
          dst->sigma_a = src->sigma_a;
          dst->has_sigma_a = true;
        }
        dst->has_color = color_texture_param("color", &dst->color);
        if (!dst->has_color && src->has_color) {
          dst->color = src->color;
          dst->has_color = true;
        }
        float_texture_param_with_default("eumelanin",   &dst->eumelanin,   &src->eumelanin);
        float_texture_param_with_default("pheomelanin", &dst->pheomelanin, &src->pheomelanin);
        float_texture_param_with_default("eta",         &dst->eta,         &src->eta);
        float_texture_param_with_default("beta_m",      &dst->beta_m,      &src->beta_m);
        float_texture_param_with_default("beta_n",      &dst->beta_n,      &src->beta_n);
        float_texture_param_with_default("alpha",       &dst->alpha,       &src->alpha);
        material = dst;
      }
      break;

    case MaterialType::KdSubsurface:
      {
        const KdSubsurfaceMaterial* src = dynamic_cast<const KdSubsurfaceMaterial*>(baseMaterial);
        KdSubsurfaceMaterial* dst = new KdSubsurfaceMaterial();
        color_texture_param_with_default("Kd",             &dst->Kd,             &src->Kd);
        color_texture_param_with_default("mfp",            &dst->mfp,            &src->mfp);
        float_texture_param_with_default("eta",            &dst->eta,            &src->eta);
        color_texture_param_with_default("Kr",             &dst->Kr,             &src->Kr);
        color_texture_param_with_default("Kt",             &dst->Kt,             &src->Kt);
        float_texture_param_with_default("uroughness",     &dst->uroughness,     &src->uroughness);
        float_texture_param_with_default("vroughness",     &dst->vroughness,     &src->vroughness);
        bool_param_with_default         ("remaproughness", &dst->remaproughness, src->remaproughness);
        material = dst;
      }
      break;

    case MaterialType::Matte:
      {
        const MatteMaterial* src = dynamic_cast<const MatteMaterial*>(baseMaterial);
        MatteMaterial* dst = new MatteMaterial();
        color_texture_param_with_default("Kd",    &dst->Kd,    &src->Kd);
        float_texture_param_with_default("sigma", &dst->sigma, &src->sigma);
        material = dst;
      }
      break;

    case MaterialType::Metal:
      {
        const MetalMaterial* src = dynamic_cast<const MetalMaterial*>(baseMaterial);
        MetalMaterial* dst = new MetalMaterial();
        color_texture_param_with_default("eta",        &dst->eta,        &src->eta);
        color_texture_param_with_default("k",          &dst->k,          &src->k);
        float_texture_param_with_default("uroughness", &dst->uroughness, &src->uroughness);
        float_texture_param_with_default("vroughness", &dst->vroughness, &src->vroughness);
        bool_param("remaproughness",      &dst->remaproughness);
        material = dst;
      }
      break;

    case MaterialType::Mirror:
      {
        const MirrorMaterial* src= dynamic_cast<const MirrorMaterial*>(baseMaterial);
        MirrorMaterial* dst = new MirrorMaterial();
        color_texture_param_with_default("Kr", &dst->Kr, &src->Kr);
        material = dst;
      }
      break;

    case MaterialType::Mix:
      {
        const MixMaterial* src = dynamic_cast<const MixMaterial*>(baseMaterial);
        MixMaterial* dst = new MixMaterial();
        color_texture_param_with_default("amount", &dst->amount, &src->amount);

        char* tmp = nullptr;
        if (string_param("namedmaterial1", &tmp)) {
          dst->namedmaterial1 = find_material(tmp);
        }
        else {
          dst->namedmaterial1 = src->namedmaterial1;
        }

        if (string_param("namedmaterial2", &tmp)) {
          dst->namedmaterial2 = find_material(tmp);
        }
        else {
          dst->namedmaterial2 = src->namedmaterial2;
        }
        material = dst;
      }
      break;

    case MaterialType::None:
      material = new NoneMaterial();
      break;

    case MaterialType::Plastic:
      {
        const PlasticMaterial* src = dynamic_cast<const PlasticMaterial*>(baseMaterial);
        PlasticMaterial* dst = new PlasticMaterial();
        color_texture_param_with_default("Kd",            &dst->Kd,             &src->Kd);
        color_texture_param_with_default("Ks",            &dst->Ks,             &src->Ks);
        float_texture_param_with_default("roughness",     &dst->roughness,      &src->roughness);
        bool_param_with_default        ("remaproughness", &dst->remaproughness, src->remaproughness);
        material = dst;
      }
      break;

    case MaterialType::Substrate:
      {
        const SubstrateMaterial* src = dynamic_cast<const SubstrateMaterial*>(baseMaterial);
        SubstrateMaterial* dst = new SubstrateMaterial();
        color_texture_param_with_default("Kd",         &dst->Kd,             &src->Kd);
        color_texture_param_with_default("Ks",         &dst->Ks,             &src->Ks);
        float_texture_param_with_default("uroughness", &dst->uroughness,     &src->uroughness);
        float_texture_param_with_default("vroughness", &dst->vroughness,     &src->vroughness);
        bool_param_with_default("remaproughness",      &dst->remaproughness, src->remaproughness);
        material = dst;
      }
      break;

    case MaterialType::Subsurface:
      {
        const SubsurfaceMaterial* src = dynamic_cast<const SubsurfaceMaterial*>(baseMaterial);
        SubsurfaceMaterial* dst = new SubsurfaceMaterial();
        string_param_with_default("name",                 &dst->coefficients,   src->coefficients, true);
        color_texture_param_with_default("sigma_a",       &dst->sigma_a,        &src->sigma_a);
        color_texture_param_with_default("sigma_prime_s", &dst->sigma_prime_s,  &src->sigma_prime_s);
        float_param_with_default("scale",                 &dst->scale,          src->scale);
        float_texture_param_with_default("eta",           &dst->eta,            &src->eta);
        color_texture_param_with_default("Kr",            &dst->Kr,             &src->Kr);
        color_texture_param_with_default("Kt",            &dst->Kt,             &src->Kt);
        float_texture_param_with_default("uroughness",    &dst->uroughness,     &src->uroughness);
        float_texture_param_with_default("vroughness",    &dst->vroughness,     &src->vroughness);
        bool_param_with_default("remaproughness",         &dst->remaproughness, src->remaproughness);
        material = dst;
      }
      break;

    case MaterialType::Translucent:
      {
        const TranslucentMaterial* src = dynamic_cast<const TranslucentMaterial*>(baseMaterial);
        TranslucentMaterial* dst = new TranslucentMaterial();
        color_texture_param_with_default("Kd",        &dst->Kd,             &src->Kd);
        color_texture_param_with_default("Ks",        &dst->Ks,             &src->Ks);
        color_texture_param_with_default("reflect",   &dst->reflect,        &src->reflect);
        color_texture_param_with_default("transmit",  &dst->transmit,       &src->transmit);
        float_texture_param_with_default("roughness", &dst->roughness,      &src->roughness);
        bool_param_with_default("remaproughness",     &dst->remaproughness, src->remaproughness);
        material = dst;
      }
      break;

    case MaterialType::Uber:
      {
        const UberMaterial* src = dynamic_cast<const UberMaterial*>(baseMaterial);
        UberMaterial* dst = new UberMaterial();
        color_texture_param_with_default("Kd",         &dst->Kd,             &src->Kd);
        color_texture_param_with_default("Ks",         &dst->Ks,             &src->Ks);
        color_texture_param_with_default("reflect",    &dst->Kr,             &src->Kr);
        color_texture_param_with_default("transmit",   &dst->Kt,             &src->Kt);
        float_texture_param_with_default("eta",        &dst->eta,            &src->eta);
        color_texture_param_with_default("opacity",    &dst->opacity,        &src->opacity);
        float_texture_param_with_default("uroughness", &dst->uroughness,     &src->uroughness);
        float_texture_param_with_default("vroughness", &dst->vroughness,     &src->vroughness);
        bool_param_with_default("remaproughness",      &dst->remaproughness, src->remaproughness);
        material = dst;
      }
      break;
    }

    if (!texture_param("bumpmap", TextureData::Float, &material->bumpmap)) {
      material->bumpmap = baseMaterial->bumpmap;
    }

    if (materialOut != nullptr) {
      *materialOut = uint32_t(m_scene->materials.size());
    }
    m_scene->materials.push_back(material);

    return true;
  }


  bool Parser::has_material_overrides(uint32_t matIdx) const
  {
    if (matIdx == kInvalidIndex) {
      return false;
    }

    const Material* mat = m_scene->materials[matIdx];
    if (mat == nullptr) {
      return false;
    }

    // If there are any spectrum-type params, it's almost certainly a material
    // override. Likewise if there are any texture params other alpha or
    // shadowalpha.
    for (const ParamInfo& param : m_params) {
      if (kSpectrumTypes.contains(param.type)) {
        return true;
      }

      if (param.type == ParamType::Texture && strcmp(param.name, "alpha") != 0 && strcmp(param.name, "shadowalpha") != 0) {
        return true;
      }

      if (param.type == ParamType::Float && find_string_in_array(param.name, kFloatParamsByMaterial[uint32_t(mat->type())]) != -1) {
        return true;
      }

      if (param.type == ParamType::Bool && strcmp(param.name, "remaproughness") == 0) {
        return true;
      }
    }

    // We've already checked for most material parameters above, so here we
    // only need to check for the few possible remaining params.
    switch (mat->type()) {
    case MaterialType::Disney:
      return find_param("thin", ParamType::Bool);

    case MaterialType::Fourier:
      return find_param("bsdffile", ParamType::String);

    case MaterialType::Mix:
      return find_param("namedmaterial1", ParamType::String) ||
             find_param("namedmaterial2", ParamType::String);

    case MaterialType::Subsurface:
      return find_param("name", ParamType::String);

    default:
      return false;
    }
  }


  bool Parser::parse_NamedMaterial()
  {
    const char* name = string_arg(0);
    uint32_t material = find_material(name);
    if (material == kInvalidIndex) {
      // TODO: warn/error about invalid material name.
    }
    else {
      m_attrs->top->activeMaterial = material;
    }
    return true;
  }


  bool Parser::parse_ObjectBegin()
  {
    if (m_activeObject != kInvalidIndex) {
      m_tokenizer.set_error("Previous ObjectBegin has not been closed yet");
      return false;
    }

    if (!m_transforms->push()) {
      m_tokenizer.set_error("Exceeded maximum transform stack size");
      return false;
    }
    if (!m_attrs->push()) {
      m_tokenizer.set_error("Exceeded maximum attribute stack size");
      return false;
    }

    m_firstShape = kInvalidIndex;

    Object* object = new Object();
    save_current_transform_matrices(&object->objectToInstance);
    object->name = copy_string(string_arg(0));

    m_activeObject = static_cast<uint32_t>(m_scene->objects.size());
    m_scene->objects.push_back(object);
    return true;
  }


  bool Parser::parse_ObjectEnd()
  {
    if (m_activeObject == kInvalidIndex) {
      m_tokenizer.set_error("ObjectEnd without an ObjectBegin");
      return false;
    }

    if (!m_attrs->pop()) {
      m_tokenizer.set_error("Cannot pop last attribute set off the stack");
      return false;
    }

    if (!m_transforms->pop()) {
      m_tokenizer.set_error("Cannot pop last transform set off the stack");
      return false;
    }

    Object* object = m_scene->objects[m_activeObject];
    object->firstShape = m_firstShape;
    object->numShapes = m_firstShape == kInvalidIndex ? 0 : (static_cast<uint32_t>(m_scene->shapes.size()) - m_firstShape);

    m_activeObject = kInvalidIndex;
    return true;
  }


  bool Parser::parse_ObjectInstance()
  {
    if (m_activeObject != kInvalidIndex) {
      m_tokenizer.set_error("Nested instances are not allowed");
      return false;
    }

    const char* name = string_arg(0);

    uint32_t object = find_object(name);
    if (object == kInvalidIndex) {
      // TODO: warn that the object isn't defined.
      return true;
    }

    Instance* instance = new Instance();
    save_current_transform_matrices(&instance->instanceToWorld);
    instance->object = object;
    instance->areaLight = m_attrs->top->areaLight;
    instance->insideMedium = m_attrs->top->insideMedium;
    instance->outsideMedium = m_attrs->top->outsideMedium;
    instance->reverseOrientation = m_attrs->top->reverseOrientation;

    m_scene->instances.push_back(instance);
    return true;
  }


  bool Parser::parse_Texture()
  {
    TextureType textureType = typed_enum_arg<TextureType>(2);

    Texture* texture = nullptr;
    switch (textureType) {
    case TextureType::Bilerp:
      {
        BilerpTexture* bilerp = new BilerpTexture();
        color_texture_param("v00", &bilerp->v00);
        color_texture_param("v01", &bilerp->v01);
        color_texture_param("v10", &bilerp->v10);
        color_texture_param("v11", &bilerp->v11);
        texture = bilerp;
      }
      break;

    case TextureType::Checkerboard3D:
    case TextureType::Checkerboard2D:
      {
        int dimension = 2;
        int_param("dimension", &dimension);
        if (dimension == 3) {
          Checkerboard3DTexture* checkerboard3d = new Checkerboard3DTexture();
          color_texture_param("tex1", &checkerboard3d->tex1);
          color_texture_param("tex2", &checkerboard3d->tex2);
          texture = checkerboard3d;
        }
        else {
          Checkerboard2DTexture* checkerboard2d = new Checkerboard2DTexture();
          color_texture_param("tex1", &checkerboard2d->tex1);
          color_texture_param("tex2", &checkerboard2d->tex2);
          typed_enum_param("aamode", kCheckerboardAAModes, &checkerboard2d->aamode);
          texture = checkerboard2d;
        }
      }
      break;

    case TextureType::Constant:
      {
        ConstantTexture* constant = new ConstantTexture();
        spectrum_param("value", constant->value);
        texture = constant;
      }
      break;

    case TextureType::Dots:
      {
        DotsTexture* dots = new DotsTexture();
        color_texture_param("inside",  &dots->inside);
        color_texture_param("outside", &dots->outside);
        texture = dots;
      }
      break;

    case TextureType::FBM:
      {
        FBMTexture* fbm = new FBMTexture();
        int_param("octaves",     &fbm->octaves);
        float_param("roughness", &fbm->roughness);
        texture = fbm;
      }
      break;

    case TextureType::ImageMap:
      {
        ImageMapTexture* imagemap = new ImageMapTexture();
        if (!string_param("filename", &imagemap->filename, true)) {
          m_tokenizer.set_error("Required parameter \"filename\" is missing");
          delete imagemap;
          return false;
        }
        typed_enum_param("wrap", kWrapModes, &imagemap->wrap);
        float_param("maxanisotropy", &imagemap->maxanisotropy);
        bool_param("trilinear",      &imagemap->trilinear);
        float_param("scale",         &imagemap->scale);
        bool_param("gamma",          &imagemap->gamma);
        texture = imagemap;
      }
      break;

    case TextureType::Marble:
      {
        MarbleTexture* marble = new MarbleTexture();
        int_param("octaves",     &marble->octaves);
        float_param("roughness", &marble->roughness);
        float_param("scale",     &marble->scale);
        float_param("variation", &marble->variation);
        texture = marble;
      }
      break;

    case TextureType::Mix:
      {
        MixTexture* mix = new MixTexture();
        color_texture_param("tex1", &mix->tex1);
        color_texture_param("tex2", &mix->tex2);
        float_texture_param("amount", &mix->amount);
        texture = mix;
      }
      break;

    case TextureType::Scale:
      {
        ScaleTexture* scale = new ScaleTexture();
        color_texture_param("tex1", &scale->tex1);
        color_texture_param("tex2", &scale->tex2);
        texture = scale;
      }
      break;

    case TextureType::UV:
      {
        UVTexture* uv = new UVTexture();
        // Not sure...?
        texture = uv;
      }
      break;

    case TextureType::Windy:
      {
        WindyTexture* windy = new WindyTexture();
        // Not sure...?
        texture = windy;
      }
      break;

    case TextureType::Wrinkled:
      {
        WrinkledTexture* wrinkled = new WrinkledTexture();
        int_param("octaves",     &wrinkled->octaves);
        float_param("roughness", &wrinkled->roughness);
        texture = wrinkled;
      }
      break;

    case TextureType::PTex:
      {
        PTexTexture* ptex = new PTexTexture();
        if (!string_param("filename", &ptex->filename, true)) {
          m_tokenizer.set_error("Required param \"filename\" is missing");
          delete ptex;
          return false;
        }
        float_param("gamma", &ptex->gamma);
        texture = ptex;
      }
      break;
    }

    if (texture == nullptr) {
      m_tokenizer.set_error("Failed to create %s texture", kTextureTypes[uint32_t(textureType)]);
      return false;
    }

    Texture2D* tex2d = dynamic_cast<Texture2D*>(texture);
    if (tex2d != nullptr) {
      typed_enum_param("mapping", kTexCoordMappings, &tex2d->mapping);
      float_param("uscale", &tex2d->uscale);
      float_param("vscale", &tex2d->vscale);
      float_param("udelta", &tex2d->udelta);
      float_param("vdelta", &tex2d->vdelta);
      float_array_param("v1", ParamType::Vector3, 3, tex2d->v1);
      float_array_param("v2", ParamType::Vector3, 3, tex2d->v2);
    }

    Texture3D* tex3d = dynamic_cast<Texture3D*>(texture);
    if (tex3d != nullptr) {
      save_current_transform_matrices(&tex3d->objectToTexture);
    }

    texture->name = copy_string(string_arg(0));
    texture->dataType = typed_enum_arg<TextureData>(1);

    // TODO: add the texture to the name map for the current AttributeBegin/End block.
    m_scene->textures.push_back(texture);
    return true;
  }


  bool Parser::parse_TransformBegin()
  {
    if (!m_transforms->push()) {
      m_tokenizer.set_error("Exceeded maximum attribute stack size");
      return false;
    }
    return true;
  }


  bool Parser::parse_TransformEnd()
  {
    if (!m_transforms->pop()) {
      m_tokenizer.set_error("Cannot pop last transform set off the stack");
      return false;
    }
    return true;
  }


  bool Parser::parse_ReverseOrientation()
  {
    m_attrs->top->reverseOrientation = !m_attrs->top->reverseOrientation;
    return true;
  }


  bool Parser::parse_Accelerator()
  {
    AcceleratorType accelType = typed_enum_arg<AcceleratorType>(0);
    Accelerator* accel = nullptr;

    if (accelType == AcceleratorType::BVH) { // bvh
      const char* bvhSplitMethods[] = { "sah", "middle", "equal", "hlbvh", nullptr };

      BVHAccelerator* bvh = new BVHAccelerator();
      int_param("maxnodeprims", &bvh->maxnodeprims);
      typed_enum_param("splitmethod", bvhSplitMethods, &bvh->splitmethod);
      accel = bvh;
    }
    else if (accelType == AcceleratorType::KdTree) { // kdtree
      KdTreeAccelerator* kdtree = new KdTreeAccelerator();
      int_param("intersectcost", &kdtree->intersectcost);
      int_param("traversalcost", &kdtree->traversalcost);
      float_param("emptybonus", &kdtree->emptybonus);
      int_param("maxprims", &kdtree->maxprims);
      int_param("maxdepth", &kdtree->maxdepth);
      accel = kdtree;
    }

    if (accel == nullptr) {
      m_tokenizer.set_error("Failed to create %s accelerator", kAccelTypes[uint32_t(accelType)]);
      return false;
    }

    if (m_scene->accelerator != nullptr) {
      delete m_scene->accelerator; // Should we warn about multiple accelerator definitions?
    }
    m_scene->accelerator = accel;
    return true;
  }


  bool Parser::parse_Camera()
  {
    CameraType cameraType = typed_enum_arg<CameraType>(0);
    Camera* camera = nullptr;

    switch (cameraType) {
    case CameraType::Perspective: // perspective
      {
        PerspectiveCamera* persp = new PerspectiveCamera();
        float_param("frameaspectratio", &persp->frameaspectratio);
        float_array_param("screenwindow", ParamType::Float, 4, persp->screenwindow); // TODO: calculate default screen window if none is specified
        float_param("lensradius", &persp->lensradius);
        float_param("focaldistance", &persp->focaldistance);
        float_param("fov", &persp->fov);
        float_param("halffov", &persp->halffov);
        camera = persp;
      }
      break;

    case CameraType::Orthographic: // orthographic
      {
        OrthographicCamera* ortho = new OrthographicCamera();
        float_param("frameaspectratio", &ortho->frameaspectratio);
        float_array_param("screenwindow", ParamType::Float, 4, ortho->screenwindow); // TODO: calculate default screen window if none is specified
        float_param("lensradius", &ortho->lensradius);
        float_param("focaldistance", &ortho->focaldistance);
        camera = ortho;
      }
      break;

    case CameraType::Environment: // environment
      {
        EnvironmentCamera* env = new EnvironmentCamera();
        float_param("frameaspectratio", &env->frameaspectratio);
        float_array_param("screenwindow", ParamType::Float, 4, env->screenwindow); // TODO: calculate default screen window if none is specified
        camera = env;
      }
      break;

    case CameraType::Realistic: // realistic
      {
        RealisticCamera* realistic = new RealisticCamera();
        string_param("lensfile", &realistic->lensfile, true);
        float_param("aperturediameter", &realistic->aperturediameter);
        float_param("focusdistance", &realistic->focusdistance);
        bool_param("simpleweighting", &realistic->simpleweighting);
        camera = realistic;
      }
      break;
    }

    if (camera == nullptr) {
      m_tokenizer.set_error("Failed to create %s camera", kCameraTypes[uint32_t(cameraType)]);
      return false;
    }

    save_inverse_transform_matrices(&camera->cameraToWorld);

    float_param("shutteropen", &camera->shutteropen);
    float_param("shutterclose", &camera->shutterclose);

    // Creating a camera automatically defines the "camera" coordinate system.
    m_transforms->coordinateSystem("camera");

    if (m_scene->camera != nullptr) {
      delete m_scene->camera; // Should we warn about multiple camera definitions here?
    }
    m_scene->camera = camera;
    return true;
  }


  bool Parser::parse_Film()
  {
    FilmType filmType = typed_enum_arg<FilmType>(0);
    Film* film = nullptr;

    if (filmType == FilmType::Image) { // image
      ImageFilm* img = new ImageFilm();
      int_param("xresolution", &img->xresolution);
      int_param("yresolution", &img->yresolution);
      float_array_param("cropwindow", ParamType::Float, 4, img->cropwwindow);
      float_param("scale", &img->scale);
      float_param("maxsampleluminance", &img->maxsampleluminance);
      float_param("diagonal", &img->diagonal);
      string_param("filename", &img->filename, true);
      film = img;
    }

    if (film == nullptr) {
      m_tokenizer.set_error("Failed to create %s film", kFilmTypes[uint32_t(filmType)]);
      return false;
    }

    if (m_scene->film != nullptr) {
      delete m_scene->film; // Should we warn about multiple film definitions in this case?
    }
    m_scene->film = film;
    return true;
  }


  bool Parser::parse_Integrator()
  {
    IntegratorType integratorType = typed_enum_arg<IntegratorType>(0);
    Integrator* integrator = nullptr;

    switch (integratorType) {
    case IntegratorType::BDPT: // bdpt
      {
        BDPTIntegrator* bdpt = new BDPTIntegrator();
        int_param("maxdepth", &bdpt->maxdepth);
        int_array_param("pixelbounds", 4, bdpt->pixelbounds);
        typed_enum_param("lightsamplestrategy", kLightSampleStrategies, &bdpt->lightsamplestrategy);
        bool_param("visualizeweights", &bdpt->visualizeweights);
        bool_param("visualizestrategies", &bdpt->visualizestrategies);
        integrator = bdpt;
      }
      break;

    case IntegratorType::DirectLighting: // directlighting
      {
        DirectLightingIntegrator* direct = new DirectLightingIntegrator();
        int_param("maxdepth", &direct->maxdepth);
        int_array_param("pixelbounds", 4, direct->pixelbounds);
        typed_enum_param("strategy", kDirectLightSampleStrategies, &direct->strategy);
        integrator = direct;
      }
      break;

    case IntegratorType::MLT: // mlt
      {
        MLTIntegrator* mlt = new MLTIntegrator();
        int_param("maxdepth", &mlt->maxdepth);
        int_param("bootstrapsamples", &mlt->bootstrapsamples);
        int_param("chains", &mlt->chains);
        int_param("mutationsperpixel", &mlt->mutationsperpixel);
        float_param("largestprobability", &mlt->largestprobability);
        float_param("sigma", &mlt->sigma);
        integrator = mlt;
      }
      break;

    case IntegratorType::Path: // path
      {
        PathIntegrator* path = new PathIntegrator();
        int_param("maxdepth", &path->maxdepth);
        int_array_param("pixelbounds", 4, path->pixelbounds);
        float_param("rrthreshold", &path->rrthreshold);
        typed_enum_param("lightsamplestrategy", kLightSampleStrategies, &path->lightsamplestrategy);
        integrator = path;
      }
      break;

    case IntegratorType::SPPM: // sppm
      {
        SPPMIntegrator* sppm = new SPPMIntegrator();
        int_param("maxdepth", &sppm->maxdepth);
        int_param("maxiterations", &sppm->maxiterations);
        int_param("photonsperiteration", &sppm->photonsperiteration);
        int_param("imagewritefrequency", &sppm->imagewritefrequency);
        float_param("radius", &sppm->radius);
        integrator = sppm;
      }
      break;

    case IntegratorType::Whitted: // whitted
      {
        WhittedIntegrator* whitted = new WhittedIntegrator();
        int_param("maxdepth", &whitted->maxdepth);
        int_array_param("pixelbounds", 4, whitted->pixelbounds);
        integrator = whitted;
      }
      break;

    case IntegratorType::VolPath: // volpath
      {
        VolPathIntegrator* volpath = new VolPathIntegrator();
        int_param("maxdepth", &volpath->maxdepth);
        int_array_param("pixelbounds", 4, volpath->pixelbounds);
        float_param("rrthreshold", &volpath->rrthreshold);
        integrator = volpath;
      }
      break;

    case IntegratorType::AO: // ao
      {
        AOIntegrator* ao = new AOIntegrator();
        int_array_param("pixelbounds", 4, ao->pixelbounds);
        bool_param("cossample", &ao->cossample);
        int_param("nsamples", &ao->nsamples);
        integrator = ao;
      }
      break;
    }

    if (integrator == nullptr) {
      m_tokenizer.set_error("Failed to create %s integrator", kIntegratorTypes[uint32_t(integratorType)]);
      return false;
    }

    if (m_scene->integrator != nullptr) {
      delete m_scene->integrator; // Should we warn about multiple integrator definitions?
    }
    m_scene->integrator = integrator;
    return true;
  }


  bool Parser::parse_PixelFilter()
  {
    FilterType filterType = typed_enum_arg<FilterType>(0);
    Filter* filter = nullptr;

    switch (filterType) {
    case FilterType::Box: // box
      filter = new BoxFilter();
      break;

    case FilterType::Gaussian: // gaussian
      {
        GaussianFilter* gaussian = new GaussianFilter();
        float_param("alpha", &gaussian->alpha);
        filter = gaussian;
      }
      break;

    case FilterType::Mitchell: // mitchell
      {
        MitchellFilter* mitchell = new MitchellFilter();
        float_param("B", &mitchell->B);
        float_param("C", &mitchell->C);
        filter = mitchell;
      }
      break;

    case FilterType::Sinc: // sinc
      {
        SincFilter* sinc = new SincFilter();
        float_param("tau", &sinc->tau);
        filter = sinc;
      }
      break;

    case FilterType::Triangle: // triangle
      filter = new TriangleFilter();
      break;
    }

    if (filter == nullptr) {
      m_tokenizer.set_error("Failed to create %s filter", kPixelFilterTypes[uint32_t(filterType)]);
      return false;
    }

    float_param("xwidth", &filter->xwidth);
    float_param("ywidth", &filter->ywidth);

    if (m_scene->filter != nullptr) {
      delete m_scene->filter; // Should we warn about multiple pixel filter definitions?
    }
    m_scene->filter = filter;
    return true;
  }


  bool Parser::parse_Sampler()
  {
    SamplerType samplerType = typed_enum_arg<SamplerType>(0);
    Sampler* sampler = nullptr;
    switch (samplerType) {
    case SamplerType::ZeroTwoSequence:
    case SamplerType::LowDiscrepancy:
      {
        ZeroTwoSequenceSampler* zerotwo = new ZeroTwoSequenceSampler();
        int_param("pixelsamples", &zerotwo->pixelsamples);
        sampler = zerotwo;
      }
      break;

    case SamplerType::Halton: // halton
      {
        HaltonSampler* halton = new HaltonSampler();
        int_param("pixelsamples", &halton->pixelsamples);
        sampler = halton;
      }
      break;

    case SamplerType::MaxMinDist: // maxmindist
      {
        MaxMinDistSampler* maxmindist = new MaxMinDistSampler();
        int_param("pixelsamples", &maxmindist->pixelsamples);
        sampler = maxmindist;
      }
      break;

    case SamplerType::Random: // random
      {
        RandomSampler* random = new RandomSampler();
        int_param("pixelsamples", &random->pixelsamples);
        sampler = random;
      }
      break;

    case SamplerType::Sobol: // sobol
      {
        SobolSampler* sobol = new SobolSampler();
        int_param("pixelsamples", &sobol->pixelsamples);
        sampler = sobol;
      }
      break;

    case SamplerType::Stratified: // stratified
      {
        StratifiedSampler* stratified = new StratifiedSampler();
        bool_param("jitter", &stratified->jitter);
        int_param("xsamples", &stratified->xsamples);
        int_param("ysamples", &stratified->ysamples);
        sampler = stratified;
      }
      break;
    }

    if (sampler == nullptr) {
      m_tokenizer.set_error("Failed to create %s sampler", kSamplerTypes[uint32_t(samplerType)]);
      return false;
    }

    if (m_scene->sampler != nullptr) {
      delete m_scene->sampler;
    }
    m_scene->sampler = sampler;
    return true;
  }


  bool Parser::parse_TransformTimes()
  {
    m_scene->startTime = float_arg(0);
    m_scene->endTime = float_arg(1);
    return true;
  }


  bool Parser::parse_WorldBegin()
  {
//    if (m_attrs->entry > 0) {
//      // Warn about unclosed AttributeBegin
//    }
//    else if (m_transforms->entry > 0) {
//      // Warn about unclosed TransformBegin
//    }

    m_inWorld = true;
    m_transforms->clear();
    m_attrs->clear();

    // Setup defaults for any scene-wide items that haven't been specified.
    if (m_scene->camera == nullptr) {
      m_scene->camera = new PerspectiveCamera();
      save_inverse_transform_matrices(&m_scene->camera->cameraToWorld); // Initialise the camera transform to identity.
      m_transforms->coordinateSystem("camera");
    }
    if (m_scene->sampler == nullptr) {
      m_scene->sampler = new HaltonSampler();
    }
    if (m_scene->film == nullptr) {
      ImageFilm* film = new ImageFilm();
      film->filename = copy_string("pbrt.exr");
      m_scene->film = film;
    }
    if (m_scene->filter == nullptr) {
      m_scene->filter = new BoxFilter();
    }
    if (m_scene->integrator == nullptr) {
      m_scene->integrator = new PathIntegrator();
    }
    if (m_scene->accelerator == nullptr) {
      m_scene->accelerator = new BVHAccelerator();
    }

    m_scene->camera->compute_defaults(m_scene->film);
    m_scene->integrator->compute_defaults(m_scene->film);
    return true;
  }


  bool Parser::parse_WorldEnd()
  {
//    if (m_activeObject != kInvalidIndex) {
//      // Warn about unclosed ObjectBegin
//    }

//    if (m_attrs->entry > 0) {
//      // Warn about unclosed AttributeBegin
//    }
//    else if (m_transforms->entry > 0) {
//      // Warn about unclosed TransformBegin
//    }

    m_inWorld = false;
    return true;
  }


  bool Parser::parse_params()
  {
    while (m_tokenizer.advance()) {
      if (!m_tokenizer.match_symbol("\"")) {
        break;
      }
      if (!parse_param()) {
        m_tokenizer.set_error("Failed to parse parameter");
        return false;
      }
    }
    return true;
  }


  bool Parser::parse_param()
  {
    uint32_t typeIndex = uint32_t(-1);
    char paramNameBuf[512];

    bool ok = true;
    ok = m_tokenizer.match_symbol("\"") &&
         m_tokenizer.advance() &&
         m_tokenizer.which_type(&typeIndex) &&
         m_tokenizer.advance() &&
         m_tokenizer.identifier(paramNameBuf, sizeof(paramNameBuf)) &&
         m_tokenizer.advance() &&
         m_tokenizer.match_symbol("\"");
    if (!ok) {
      m_tokenizer.set_error("Failed to match a param declaration");
      return false;
    }

    if (!m_tokenizer.advance()) {
      m_tokenizer.set_error("Missing value for parameter %s", paramNameBuf);
      return false;
    }

    const ParamTypeDeclaration& paramTypeDecl = kParamTypes[typeIndex];

    ParamInfo param;
    param.type = paramTypeDecl.type;
    param.offset = m_temp.size();

    switch (param.type) {
    case ParamType::Int:
      ok = parse_ints(&param.count);
      break;
    case ParamType::Float:
    case ParamType::Point2:
    case ParamType::Point3:
    case ParamType::Vector2:
    case ParamType::Vector3:
    case ParamType::Normal3:
    case ParamType::RGB:
    case ParamType::XYZ:
    case ParamType::Blackbody:
      ok = parse_floats(&param.count);
      break;
    case ParamType::Samples:
      ok = parse_spectrum(&param.count);
      break;
    case ParamType::String:
    case ParamType::Texture:
      ok = parse_strings(&param.count);
      break;
    case ParamType::Bool:
      ok = parse_bools(&param.count);
      break;
    }

    if (ok && paramTypeDecl.numComponents > 1 && (param.count % paramTypeDecl.numComponents) != 0) {
      m_tokenizer.set_error("Wrong number of values for %s with type %s, expected a multiple of %u",
                            paramNameBuf, paramTypeDecl.name, paramTypeDecl.numComponents);
      return false;
    }

    if (ok) {
      auto it = m_paramNames.find(paramNameBuf);
      if (it == m_paramNames.end()) {
        param.name = copy_string(paramNameBuf);
        m_paramNames[param.name] = param.name;
      }
      else {
        param.name = it->second;
      }
      m_params.push_back(param);
    }

    return ok;
  }


  bool Parser::parse_ints(uint32_t* count)
  {
    int tmp;

    *count = 0;
    if (m_tokenizer.match_symbol("[")) {
      m_tokenizer.advance();
      while (!m_tokenizer.match_symbol("]")) {
        if (!m_tokenizer.int_literal(&tmp)) {
          m_tokenizer.set_error("Expected int or ']'");
          return false;
        }
        push_bytes(&tmp, sizeof(tmp));
        (*count)++;
        m_tokenizer.advance();
      }
    }
    else {
      if (!m_tokenizer.int_literal(&tmp)) {
        m_tokenizer.set_error("Expected int or ']'");
        return false;
      }
      push_bytes(&tmp, sizeof(tmp));
      (*count)++;
    }

    return true;
  }


  bool Parser::parse_floats(uint32_t* count)
  {
    float tmp;

    *count = 0;
    if (m_tokenizer.match_symbol("[")) {
      m_tokenizer.advance();
      while (!m_tokenizer.match_symbol("]")) {
        if (!m_tokenizer.float_literal(&tmp)) {
          m_tokenizer.set_error("Expected int or ']'");
          return false;
        }
        push_bytes(&tmp, sizeof(tmp));
        (*count)++;
        m_tokenizer.advance();
      }
    }
    else {
      if (!m_tokenizer.float_literal(&tmp)) {
        m_tokenizer.set_error("Expected int or ']'");
        return false;
      }
      push_bytes(&tmp, sizeof(tmp));
      (*count)++;
    }

    return true;
  }


  bool Parser::parse_spectrum(uint32_t* count)
  {
    *count = 0;

    bool bracketed = false;
    if (m_tokenizer.match_symbol("[")) {
      bracketed = true;
      m_tokenizer.advance();
    }

    bool ok;
    char* filename = m_tokenizer.temp_buffer();
    if (m_tokenizer.string_literal(filename, kTempBufferCapacity)) {
      if (bracketed) {
        ok = m_tokenizer.advance() && m_tokenizer.match_symbol("]");
        if (!ok) {
          m_tokenizer.set_error("Unmatched '['");
          return false;
        }
      }
      m_tokenizer.advance();

      filename = copy_string(filename);
      ok = m_tokenizer.push_file(filename, true);
      delete[] filename;

      if (!ok) {
        m_tokenizer.set_error("Failed to open SPD file %s", filename);
        return false;
      }

      while (m_tokenizer.advance()) {
        float pair[2];
        ok = m_tokenizer.float_literal(pair) &&
             m_tokenizer.advance() &&
             m_tokenizer.float_literal(pair + 1);
        if (!ok) {
          m_tokenizer.set_error("Failed to parse sampled spectrum data");
          return false;
        }
        push_bytes(pair, sizeof(pair));
        (*count) += 2;
      }

      m_tokenizer.pop_file();
      return true;
    }

    // If we got here, we should be dealing with an inline list of
    // (wavelength, value) pairs, so it's an error if we're not bracketed.
    if (!bracketed) {
      m_tokenizer.set_error("Expected a '[' or a filename");
      return false;
    }

    while (m_tokenizer.advance()) {
      if (m_tokenizer.match_symbol("]")) {
        break;
      }

      float pair[2];
      ok = m_tokenizer.float_literal(pair) &&
           m_tokenizer.advance() &&
           m_tokenizer.float_literal(pair + 1);
      if (!ok) {
        m_tokenizer.set_error("Failed to parse sampled spectrum data");
        return false;
      }
      push_bytes(pair, sizeof(pair));
      (*count) += 2;
    }
    return true;
  }


  bool Parser::parse_strings(uint32_t* count)
  {
    *count = 0;

    bool ok;
    if (m_tokenizer.match_symbol("[")) {
      while (m_tokenizer.advance()) {
        if (m_tokenizer.match_symbol("]")) {
          return true;
        }
        ok = m_tokenizer.string_literal(nullptr, 0);
        if (!ok) {
          m_tokenizer.set_error("Failed to parse string literal");
          return false;
        }
        push_string(m_tokenizer.token() + 1, m_tokenizer.token_length() - 2);
        (*count)++;
      }

      m_tokenizer.set_error("Unclosed '['");
      return false;
    }
    else {
      ok = m_tokenizer.string_literal(nullptr, 0);
      if (!ok) {
        m_tokenizer.set_error("Failed to parse string literal");
        return false;
      }
      push_string(m_tokenizer.token() + 1, m_tokenizer.token_length() - 2);
      (*count)++;
      return true;
    }
  }


  static const char* boolValues[] = { "false", "true", nullptr };

  bool Parser::parse_bools(uint32_t* count)
  {
    *count = 0;

    bool ok;
    int boolIndex = -1;
    if (m_tokenizer.match_symbol("[")) {
      while (m_tokenizer.advance()) {
        if (m_tokenizer.match_symbol("]")) {
          return true;
        }
        ok = m_tokenizer.which_string_literal(boolValues, &boolIndex);
        if (!ok) {
          m_tokenizer.set_error("Invalid value for boolean parameter");
          return false;
        }
        bool val = static_cast<bool>(boolIndex);
        push_bytes(&val, sizeof(val));
        (*count)++;
      }

      m_tokenizer.set_error("Unclosed '['");
      return false;
    }
    else {
      ok = m_tokenizer.which_string_literal(boolValues, &boolIndex);
      if (!ok) {
        m_tokenizer.set_error("Invalid value for boolean parameter");
        return false;
      }
      bool val = static_cast<bool>(boolIndex);
      push_bytes(&val, sizeof(val));
      (*count)++;
      return true;
    }
  }


  //
  // Parser private methods
  //

  const char* Parser::string_arg(uint32_t index) const
  {
    assert(kStatements[m_statementIndex].argPattern[index] == 's');
    return reinterpret_cast<const char*>(m_temp.data() + m_params[index].offset);
  }


  int Parser::enum_arg(uint32_t index) const
  {
    assert(kStatements[m_statementIndex].argPattern[index] == 'e' || kStatements[m_statementIndex].argPattern[index] == 'k');
    return *reinterpret_cast<const int*>(m_temp.data() + m_params[index].offset);
  }


  float Parser::float_arg(uint32_t index) const
  {
    assert(kStatements[m_statementIndex].argPattern[index] == 'f');
    return *reinterpret_cast<const float*>(m_temp.data() + m_params[index].offset);
  }


  const ParamInfo* Parser::find_param(const char* name, ParamTypeSet allowedTypes) const
  {
    assert(name != nullptr);

    for (const ParamInfo& paramDesc : m_params) {
      if (std::strcmp(name, paramDesc.name) != 0) {
        continue;
      }
      if (!allowedTypes.contains(paramDesc.type)) {
        // error, invalid type for param
        return nullptr;
      }
      return &paramDesc;
    }
    return nullptr;
  }


  bool Parser::string_param(const char* name, char** dest, bool copy)
  {
    assert(name != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, ParamType::String);
    if (paramDesc == nullptr || paramDesc->count != 1) {
      return false;
    }

    char* src = reinterpret_cast<char*>(m_temp.data() + paramDesc->offset);
    if (copy && *dest != nullptr) {
      delete[] *dest;
    }
    *dest = copy ? copy_string(src) : src;
    return true;
  }


  bool Parser::string_param_with_default(const char* name, char** dest, const char* defaultVal, bool copy)
  {
    if (string_param(name, dest, copy)) {
      return true;
    }
    if (copy && *dest != nullptr) {
      delete[] *dest;
    }
    *dest = copy ? copy_string(defaultVal) : const_cast<char*>(defaultVal);
    return true;
  }


  bool Parser::filename_param(const char *name, char **dest)
  {
    char* tmp = nullptr;
    if (!string_param(name, &tmp)) {
      return false;
    }
    if (dest == nullptr) {
      return true;
    }

    char* realname = m_tokenizer.temp_buffer();
    if (!resolve_file(tmp, m_tokenizer.get_original_filename(), realname, kTempBufferCapacity)) {
      return false;
    }
    *dest = copy_string(realname);
    return true;
  }


  bool Parser::bool_param(const char *name, bool *dest)
  {
    assert(name != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, ParamType::Bool);
    if (paramDesc == nullptr || paramDesc->count != 1) {
      return false;
    }

    *dest = *reinterpret_cast<bool*>(m_temp.data() + paramDesc->offset);
    return true;
  }


  bool Parser::bool_param_with_default(const char *name, bool *dest, bool defaultVal)
  {
    if (bool_param(name, dest)) {
      return true;
    }
    *dest = defaultVal;
    return true;
  }


  bool Parser::int_param(const char* name, int* dest)
  {
    assert(name != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, ParamType::Int);
    if (paramDesc == nullptr || paramDesc->count != 1) {
      return false;
    }

    *dest = *reinterpret_cast<int*>(m_temp.data() + paramDesc->offset);
    return true;
  }


  bool Parser::int_array_param(const char* name, uint32_t len, int* dest)
  {
    assert(name != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, ParamType::Int);
    if (paramDesc == nullptr || paramDesc->count != len) {
      return false;
    }

    const int* src = reinterpret_cast<const int*>(m_temp.data() + paramDesc->offset);
    std::memcpy(dest, src, sizeof(int) * len);
    return true;
  }


  bool Parser::int_vector_param(const char* name, uint32_t *len, int** dest, bool copy)
  {
    assert(name != nullptr);
    assert(len != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, ParamType::Int);
    if (paramDesc == nullptr) {
      return false;
    }

    *len = paramDesc->count;
    if (copy) {
      const int* src = reinterpret_cast<const int*>(m_temp.data() + paramDesc->offset);
      *dest = (paramDesc->count > 0) ? new int[paramDesc->count] : nullptr;
      std::memcpy(*dest, src, sizeof(int) * paramDesc->count);
    }
    else {
      *dest = reinterpret_cast<int*>(m_temp.data() + paramDesc->offset);
    }
    return true;
  }


  bool Parser::float_param(const char* name, float* dest)
  {
    assert(name != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, ParamType::Float);
    if (paramDesc == nullptr || paramDesc->count != 1) {
      return false;
    }

    *dest = *reinterpret_cast<float*>(m_temp.data() + paramDesc->offset);
    return true;
  }


  bool Parser::float_param_with_default(const char* name, float* dest, float defaultVal)
  {
    if (float_param(name, dest)) {
      return true;
    }
    *dest = defaultVal;
    return true;
  }


  bool Parser::float_array_param(const char* name, ParamType expectedType, uint32_t len, float* dest)
  {
    assert(name != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, expectedType);
    if (paramDesc == nullptr || paramDesc->count != len) {
      return false;
    }

    const float* src = reinterpret_cast<const float*>(m_temp.data() + paramDesc->offset);
    std::memcpy(dest, src, sizeof(float) * len);
    return true;
  }


  bool Parser::float_vector_param(const char* name, ParamType expectedType, uint32_t *len, float** dest, bool copy)
  {
    assert(name != nullptr);
    assert(len != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, expectedType);
    if (paramDesc == nullptr) {
      return false;
    }

    *len = paramDesc->count;
    if (copy) {
      const float* src = reinterpret_cast<const float*>(m_temp.data() + paramDesc->offset);
      *dest = (paramDesc->count > 0) ? new float[paramDesc->count] : nullptr;
      std::memcpy(*dest, src, sizeof(float) * paramDesc->count);
    }
    else {
      *dest = reinterpret_cast<float*>(m_temp.data() + paramDesc->offset);
    }
    return true;
  }


  bool Parser::spectrum_param(const char* name, float dest[3])
  {
    assert(name != nullptr);
    assert(dest != nullptr);

    const ParamInfo* paramDesc = find_param(name, kSpectrumTypes);
    if (paramDesc == nullptr) {
      return false;
    }

    float* src = reinterpret_cast<float*>(m_temp.data() + paramDesc->offset);
    switch (paramDesc->type) {
    case ParamType::RGB:
      if (paramDesc->count != 3) {
        return false;
      }
      std::memcpy(dest, src, sizeof(float) * 3);
      break;

    case ParamType::XYZ:
      if (paramDesc->count != 3) {
        return false;
      }
      xyz_to_rgb(src, dest);
      break;

    case ParamType::Blackbody:
      if (paramDesc->count != 2) {
        return false;
      }
      blackbody_to_rgb(src, dest);
      break;

    case ParamType::Samples:
      if (paramDesc->count == 0 || paramDesc->count % 2 != 0) {
        return false;
      }
      spectrum_to_rgb(src, paramDesc->count / 2, dest);
      break;

    default:
      break;
    }

    return true;
  }


  bool Parser::texture_param(const char* name, TextureData dataType, uint32_t* dest)
  {
    char* textureName = nullptr;
    if (!string_param(name, &textureName)) {
      return false;
    }

    uint32_t tex = find_texture(textureName, dataType);
    if (tex == kInvalidIndex) {
      return false;
    }
    *dest = tex;
    return true;
  }


  bool Parser::float_texture_param(const char* name, FloatTex* dest)
  {
    bool hasTex = texture_param(name, TextureData::Float, &dest->texture);
    bool hasValue = float_param(name, &dest->value);
    return hasTex | hasValue;
  }


  bool Parser::color_texture_param(const char *name, ColorTex *dest)
  {
    bool hasTex = texture_param(name, TextureData::Spectrum, &dest->texture);
    bool hasValue = spectrum_param(name, dest->value);
    return hasTex | hasValue;
  }


  bool Parser::float_texture_param_with_default(const char* name, FloatTex* dest, const FloatTex* defaultVal)
  {
    bool hasTex = texture_param(name, TextureData::Float, &dest->texture);
    if (!hasTex && defaultVal->texture != kInvalidIndex) {
      dest->texture = defaultVal->texture;
      hasTex = true;
    }
    bool hasValue = float_param(name, &dest->value);
    if (!hasValue) {
      dest->value = defaultVal->value;
      hasValue = true;
    }
    return hasTex | hasValue;
  }


  bool Parser::color_texture_param_with_default(const char *name, ColorTex *dest, const ColorTex* defaultVal)
  {
    bool hasTex = texture_param(name, TextureData::Spectrum, &dest->texture);
    if (!hasTex && defaultVal->texture != kInvalidIndex) {
      dest->texture = defaultVal->texture;
      hasTex = true;
    }
    bool hasValue = spectrum_param(name, dest->value);
    if (!hasValue) {
      memcpy(dest->value, defaultVal->value, sizeof(dest->value));
      hasValue = true;
    }
    return hasTex | hasValue;
  }


  bool Parser::enum_param(const char* name, const char* values[], int* dest)
  {
    char* value = nullptr;
    if (!string_param(name, &value)) {
      return false;
    }
    int index = find_string_in_array(value, values);
    if (index == -1) {
      return false;
    }
    *dest = index;
    return true;
  }


  void Parser::save_current_transform_matrices(Transform* dest) const
  {
    assert(dest != nullptr);
    std::memcpy(dest->start, m_transforms->matrices[m_transforms->entry][0].rows, sizeof(float) * 16);
    std::memcpy(dest->end, m_transforms->matrices[m_transforms->entry][1].rows, sizeof(float) * 16);
  }


  void Parser::save_inverse_transform_matrices(Transform* dest) const
  {
    assert(dest != nullptr);
    Mat4 inv[2] = { inverse(m_transforms->matrices[m_transforms->entry][0]),
                    inverse(m_transforms->matrices[m_transforms->entry][1]) };
    std::memcpy(dest->start, inv[0].rows, sizeof(float) * 16);
    std::memcpy(dest->end,   inv[1].rows, sizeof(float) * 16);
  }


  uint32_t Parser::find_object(const char* name) const
  {
    if (name == nullptr || name[0] == '\0' || m_scene->objects.empty()) {
      return kInvalidIndex;
    }
    // Note that we're searching in reverse because we want to find the newest
    // object first, in the case where there's more than one object with the
    // same name (i.e. where the name has been redefined).
    uint32_t i = uint32_t(m_scene->objects.size());
    do {
      --i;
      const Object* object = m_scene->objects[i];
      if (object != nullptr && object->name != nullptr && std::strcmp(name, object->name) == 0) {
        return i;
      }
    } while (i > 0);
    return kInvalidIndex;
  }


  uint32_t Parser::find_medium(const char *name) const
  {
    if (name == nullptr || name[0] == '\0' || m_scene->mediums.empty()) {
      return kInvalidIndex;
    }
    // Note that we're searching in reverse because we want to find the newest
    // medium first, in the case where there's more than one medium with the
    // same name (i.e. where the name has been redefined).
    uint32_t i = uint32_t(m_scene->mediums.size());
    do {
      --i;
      const Medium* medium = m_scene->mediums[i];
      if (medium != nullptr && medium->mediumName != nullptr && std::strcmp(name, medium->mediumName) == 0) {
        return i;
      }
    } while (i > 0);
    return kInvalidIndex;
  }


  uint32_t Parser::find_material(const char* name) const
  {
    if (name == nullptr || name[0] == '\0') {
      return kInvalidIndex;
    }
    for (int i = int(m_attrs->entry); i >= 0; --i) {
      for (uint32_t matIdx : m_attrs->attrs[i].materials) {
        const Material* mat = m_scene->materials[matIdx];
        if (mat != nullptr && mat->name != nullptr && std::strcmp(name, mat->name) == 0) {
          return matIdx;
        }
      }
    }
    return kInvalidIndex;
  }


  uint32_t Parser::find_texture(const char* name, TextureData dataType) const
  {
    if (name == nullptr || name[0] == '\0') {
      return kInvalidIndex;
    }

    switch (dataType) {
    case TextureData::Float:
      for (int i = int(m_attrs->entry); i >= 0; --i) {
        for (uint32_t texIdx : m_attrs->attrs[i].floatTextures) {
          const Texture* tex = m_scene->textures[texIdx];
          if (tex != nullptr && tex->name != nullptr && std::strcmp(name, tex->name) == 0) {
            return texIdx;
          }
        }
      }
      break;
    case TextureData::Spectrum:
      for (int i = int(m_attrs->entry); i >= 0; --i) {
        for (uint32_t texIdx : m_attrs->attrs[i].spectrumTextures) {
          const Texture* tex = m_scene->textures[texIdx];
          if (tex != nullptr && tex->name != nullptr && std::strcmp(name, tex->name) == 0) {
            return texIdx;
          }
        }
      }
      break;
    }

    return kInvalidIndex;
  }


  void Parser::push_bytes(void* src, size_t numBytes)
  {
    size_t start = m_temp.size();
    m_temp.resize(start + numBytes);
    std::memcpy(m_temp.data() + start, src, numBytes);
  }


  void Parser::push_string(const char* src, size_t len)
  {
    size_t start = m_temp.size();
    m_temp.resize(start + len + 1);
    std::memcpy(m_temp.data() + start, src, len);
    m_temp[start + len] = '\0';
  }


  //
  // Loader public methods
  //

  Loader::Loader() :
    m_scene(nullptr),
    m_parser(new Parser())
  {
  }


  Loader::~Loader()
  {
    delete m_scene;
    delete m_parser;
  }


  void Loader::set_buffer_capacity(size_t n)
  {
    m_parser->tokenizer().set_buffer_capacity(n);
  }


  void Loader::set_max_include_depth(uint32_t n)
  {
    m_parser->tokenizer().set_max_include_depth(n);
  }


  bool Loader::load(const char* filename)
  {
    if (!m_parser->parse(filename)) {
      return false;
    }
    m_scene = m_parser->take_scene();
    return true;
  }


  Scene* Loader::take_scene()
  {
    Scene* scene = m_scene;
    m_scene = nullptr;
    return scene;
  }


  Scene* Loader::borrow_scene()
  {
    return m_scene;
  }


  const Error* Loader::error() const
  {
    return m_parser->get_error();
  }

} // namespace minipbrt
