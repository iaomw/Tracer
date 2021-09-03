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

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>


/// minipbrt - A simple and fast parser for PBRT v3 files
/// =====================================================
///
/// For info about the PBRT file format, see:
/// https://www.pbrt.org/fileformat-v3.html
///
/// Getting started
/// ---------------
///
/// - Add minipbrt.h and minipbrt.cpp into your project.
/// - Include minipbrt.h wherever you need it.
///
/// Loading a file
/// --------------
/// ```
/// minipbrt::Loader loader;
/// if (loader.load(filename)) {
///   minipbrt::Scene* scene = loader.take_scene();
///   // ... process the scene, then delete it ...
///   delete scene;
/// }
/// else {
///   // If parsing failed, the parser will have an error object.
///   const minipbrt::Error* err = loader.error();
///   fprintf(stderr, "[%s, line %lld, column %lld] %s\n",
///       err->filename(), err->line(), err->column(), err->message());
///   // Don't delete err, it's still owned by the loader.
/// }
/// ```
///
/// Implementation notes
/// --------------------
///
/// * The code is C++11. 
/// 
/// * Spectra are always converted to RGB at load time. (This may change in
///   future; for now it simplifies things to convert them straight away).
/// 
/// * PLY files are not automatically loaded. Call `load_ply_mesh` to load one
///   of them. You can call this for each plymesh shape in parallel, or you can
///   call `load_ply_meshes` to load them all on a single thread.
///   (This is not implemented yet)
/// 
/// * Likewise, we provide helper functions for triangulating shapes but it's
///   up to you to call them.
/// 
/// * Most material properties can be either a texture or a value. These
///   properties are represented as structs with type `ColorTex` or `FloatTex`. In
///   both structs, if the `texture` member is anything other than
///   `kInvalidTexture`, the texture should be used *instead* of the `value`.

namespace minipbrt {

  //
  // Constants
  //

  static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;


  //
  // Forward declarations
  //

  // Interfaces
  struct Accelerator;
  struct Camera;
  struct Film;
  struct Filter;
  struct Integrator;
  struct Material;
  struct Medium;
  struct Sampler;
  struct Shape;
  struct Texture;

  // Instancing structs
  struct Object;
  struct Instance;

  class Parser;


  //
  // Helper types
  //

  enum class ParamType : uint32_t {
    Bool,       //!< A boolean value.
    Int,        //!< 1 int: a single integer value.
    Float,      //!< 1 float: a single floating point value.
    Point2,     //!< 2 floats: a 2D point.
    Point3,     //!< 3 floats: a 3D point.
    Vector2,    //!< 2 floats: a 2D direction vector.
    Vector3,    //!< 3 floats: a 3D direction vector.
    Normal3,    //!< 3 floats: a 3D normal vector.
    RGB,        //!< 3 floats: an RGB color.
    XYZ,        //!< 3 floats: a CIE XYZ color.
    Blackbody,  //!< 2 floats: temperature (in Kelvin) and scale.
    Samples,    //!< 2n floats: n pairs of (wavelength, value) samples, sorted by wavelength.
    String,     //!< A char* pointer and a length.
    Texture,    //!< A texture reference, stored as a (name, index) pair. The index will be 0xFFFFFFFF if not resolved yet.
  };


  struct FloatTex {
    uint32_t texture;
    float value;
  };


  struct ColorTex {
    uint32_t texture;
    float value[3];
  };


  /// A bit set for elements of an enum.
  template <class T>
  struct Bits {
    uint32_t val;

    Bits() : val(0) {}
    Bits(T ival) : val(1u << static_cast<uint32_t>(ival)) {}
    Bits(uint32_t ival) : val(ival) {}
    Bits(const Bits<T>& other) : val(other.val) {}

    Bits<T>& operator = (const Bits<T>& other) { val = other.val; return *this; }

    void set(T ival)    { val |= (1u << static_cast<uint32_t>(ival)); }
    void clear(T ival)  { val &= ~(1u << static_cast<uint32_t>(ival)); }
    void toggle(T ival) { val ^= (1u << static_cast<uint32_t>(ival)); }

    void setAll()    { val = 0xFFFFFFFFu; }
    void clearAll()  { val = 0u;}
    void toggleAll() { val = ~val; }

    bool contains(T ival) const { return (val & (1u << static_cast<uint32_t>(ival))) != 0u; }
  };

  template <class T> Bits<T> operator | (Bits<T> lhs, Bits<T> rhs) { return Bits<T>(lhs.val | rhs.val); }
  template <class T> Bits<T> operator & (Bits<T> lhs, Bits<T> rhs) { return Bits<T>(lhs.val & rhs.val); }
  template <class T> Bits<T> operator ^ (Bits<T> lhs, Bits<T> rhs) { return Bits<T>(lhs.val ^ rhs.val); }
  template <class T> Bits<T> operator ~ (Bits<T> rhs) { return Bits<T>(~rhs.val); }

  template <class T> Bits<T> operator | (Bits<T> lhs, T rhs) { return lhs | Bits<T>(rhs); }
  template <class T> Bits<T> operator & (Bits<T> lhs, T rhs) { return lhs & Bits<T>(rhs); }
  template <class T> Bits<T> operator ^ (Bits<T> lhs, T rhs) { return lhs ^ Bits<T>(rhs); }

  template <class T> Bits<T> operator | (T lhs, T rhs) { return Bits<T>(lhs) | Bits<T>(rhs); }


  struct Transform {
    float start[4][4]; // row major matrix for when time = start time.
    float end[4][4]; // row major for when time = end time.
  };


  //
  // Accelerator types
  //

  enum class AcceleratorType {
    BVH,
    KdTree,
  };


  enum class BVHSplit {
    SAH,
    Middle,
    Equal,
    HLBVH,
  };


  struct Accelerator {
    virtual ~Accelerator() {}
    virtual AcceleratorType type() const = 0;
  };


  struct BVHAccelerator : public Accelerator {
    int maxnodeprims     = 4;
    BVHSplit splitmethod = BVHSplit::SAH;

    virtual ~BVHAccelerator() override {}
    virtual AcceleratorType type() const override { return AcceleratorType::BVH; }
  };


  struct KdTreeAccelerator : public Accelerator {
    int intersectcost = 80;
    int traversalcost = 1;
    float emptybonus  = 0.2f;
    int maxprims      = 1;
    int maxdepth      = -1;

    virtual ~KdTreeAccelerator() override {}
    virtual AcceleratorType type() const override { return AcceleratorType::KdTree; }
  };


  //
  // Area Light types
  //

  enum class AreaLightType {
    Diffuse,
  };


  struct AreaLight {
    float scale[3] = { 1.0f, 1.0f, 1.0f };

    virtual ~AreaLight() {}
    virtual AreaLightType type() const = 0;
  };


  struct DiffuseAreaLight : public AreaLight {
    float L[3]    = { 1.0f, 1.0f, 1.0f };
    bool twosided = false;
    int samples   = 1;

    virtual ~DiffuseAreaLight() override {}
    virtual AreaLightType type() const override { return AreaLightType::Diffuse; }
  };


  //
  // Camera types
  //

  enum class CameraType {
    Perspective,
    Orthographic,
    Environment,
    Realistic,
  };


  struct Camera {
    Transform cameraToWorld;
    float shutteropen  = 0.0f;
    float shutterclose = 1.0f;

    virtual ~Camera() {}
    virtual CameraType type() const = 0;
    virtual void compute_defaults(const Film* film) {}
  };


  struct PerspectiveCamera : public Camera {
    float frameaspectratio  = 0.0f; // 0 or less means "compute this from the film resolution"
    float screenwindow[4]   = { 0.0f, 0.0f, 0.0f, 0.0f }; // endx <= startx or endy <= starty means "compute this from the film resolution"
    float lensradius        = 0.0f;
    float focaldistance     = 1e30f;
    float fov               = 90.0f;
    float halffov           = 45.0f;

    virtual ~PerspectiveCamera() override {}
    virtual CameraType type() const override { return CameraType::Perspective; }
    virtual void compute_defaults(const Film* film) override;
  };


  struct OrthographicCamera : public Camera {
    float frameaspectratio  = 1.0f;
    float screenwindow[4]   = { -1.0f, 1.0f, -1.0f, 1.0f };
    float lensradius        = 0.0f;
    float focaldistance     = 1e30f;

    virtual ~OrthographicCamera() override {}
    virtual CameraType type() const override { return CameraType::Orthographic; }
    virtual void compute_defaults(const Film* film) override;
  };


  struct EnvironmentCamera : public Camera {
    float frameaspectratio  = 1.0f;
    float screenwindow[4]   = { -1.0f, 1.0f, -1.0f, 1.0f };

    virtual ~EnvironmentCamera() override {}
    virtual CameraType type() const override { return CameraType::Environment; }
    virtual void compute_defaults(const Film* film) override;
  };


  struct RealisticCamera : public Camera {
    char* lensfile          = nullptr;
    float aperturediameter = 1.0f;
    float focusdistance     = 10.0f;
    bool simpleweighting    = true;

    virtual ~RealisticCamera() override {}
    virtual CameraType type() const override { return CameraType::Realistic; }
  };


  //
  // Film types
  //

  enum class FilmType {
    Image,
  };


  struct Film {
    virtual ~Film() {}
    virtual FilmType type() const = 0;
    virtual float get_aspect_ratio() const = 0;
    virtual void get_resolution(int& w, int& h) const = 0;
  };


  struct ImageFilm : public Film {
    int xresolution           = 640;
    int yresolution           = 480;
    float cropwwindow[4]      = { 0.0f, 1.0f, 0.0f, 1.0f };
    float scale               = 1.0f;
    float maxsampleluminance  = std::numeric_limits<float>::infinity();
    float diagonal            = 35.0f; // in millimetres
    char* filename            = nullptr; // name of the output image.

    virtual ~ImageFilm() override {}
    virtual FilmType type() const override { return FilmType::Image; }
    virtual float get_aspect_ratio() const override { return float(xresolution) / float(yresolution); }
    virtual void get_resolution(int& w, int& h) const override { w = xresolution; h = yresolution; }
  };


  //
  // Filter types
  //

  enum class FilterType {
    Box,
    Gaussian,
    Mitchell,
    Sinc,
    Triangle,
  };


  struct Filter {
    float xwidth = 2.0f;
    float ywidth = 2.0f;

    virtual ~Filter() {}
    virtual FilterType type() const = 0;
  };


  struct BoxFilter : public Filter {
    BoxFilter() { xwidth = 0.5f; ywidth = 0.5f; }
    virtual ~BoxFilter() override {}
    virtual FilterType type() const override { return FilterType::Box; }
  };


  struct GaussianFilter : public Filter {
    float alpha = 2.0f;

    virtual ~GaussianFilter() override {}
    virtual FilterType type() const override { return FilterType::Gaussian; }
  };


  struct MitchellFilter : public Filter {
    float B = 1.0f / 3.0f;
    float C = 1.0f / 3.0f;

    virtual ~MitchellFilter() override {}
    virtual FilterType type() const override { return FilterType::Mitchell; }
  };


  struct SincFilter : public Filter {
    float tau = 3.0f;

    SincFilter() { xwidth = 4.0f; ywidth = 4.0f; }
    virtual ~SincFilter() override {}
    virtual FilterType type() const override { return FilterType::Sinc; }
  };


  struct TriangleFilter : public Filter {
    virtual ~TriangleFilter() override {}
    virtual FilterType type() const override { return FilterType::Triangle; }
  };


  //
  // Integrator types
  //

  enum class IntegratorType {
    BDPT,
    DirectLighting,
    MLT,
    Path,
    SPPM,
    Whitted,
    VolPath,
    AO,
  };


  enum class LightSampleStrategy {
    Uniform,
    Power,
    Spatial,
  };


  enum class DirectLightSampleStrategy {
    All,
    One,
  };


  struct Integrator {
    virtual ~Integrator() {}
    virtual IntegratorType type() const = 0;
    virtual void compute_defaults(const Film* /*film*/) {}
  };


  struct BDPTIntegrator : public Integrator {
    int maxdepth                            = 5;
    int pixelbounds[4]                      = { 0, -1, 0, -1 }; // endx <= startx or endy <= starty means "whole image".
    LightSampleStrategy lightsamplestrategy = LightSampleStrategy::Power;
    bool visualizestrategies                = false;
    bool visualizeweights                   = false;

    virtual ~BDPTIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::BDPT; }
    virtual void compute_defaults(const Film* film) override;
  };
  

  struct DirectLightingIntegrator : public Integrator {
    DirectLightSampleStrategy strategy  = DirectLightSampleStrategy::All;
    int maxdepth                        = 5;
    int pixelbounds[4]                  = { 0, -1, 0, -1 };

    virtual ~DirectLightingIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::DirectLighting; }
    virtual void compute_defaults(const Film* film) override;
  };

  
  struct MLTIntegrator : public Integrator {
    int maxdepth              = 5;
    int bootstrapsamples      = 100000;
    int chains                = 1000;
    int mutationsperpixel     = 100;
    float largestprobability  = 0.3f;
    float sigma               = 0.01f;

    virtual ~MLTIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::MLT; }
  };
  

  struct PathIntegrator : public Integrator {
    int maxdepth                            = 5;
    int pixelbounds[4]                      = { 0, -1, 0, -1 }; // endx <= startx or endy <= starty means "whole image".
    float rrthreshold                       = 1.0f;
    LightSampleStrategy lightsamplestrategy = LightSampleStrategy::Spatial;

    virtual ~PathIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::Path; }
    virtual void compute_defaults(const Film* film) override;
  };
  

  struct SPPMIntegrator : public Integrator {
    int maxdepth            = 5;
    int maxiterations       = 64;
    int photonsperiteration = -1;
    int imagewritefrequency = 1 << 30;
    float radius            = 1.0f;

    virtual ~SPPMIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::SPPM; }
  };
  

  struct WhittedIntegrator : public Integrator {
    int maxdepth        = 5;
    int pixelbounds[4]  = { 0, -1, 0, -1 };

    virtual ~WhittedIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::Whitted; }
    virtual void compute_defaults(const Film* film) override;
  };
  

  struct VolPathIntegrator : public Integrator {
    int maxdepth                            = 5;
    int pixelbounds[4]                      = { 0, -1, 0, -1 }; // endx < startx or endy < starty means "whole image".
    float rrthreshold                       = 1.0f;
    LightSampleStrategy lightsamplestrategy = LightSampleStrategy::Spatial;

    virtual ~VolPathIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::VolPath; }
    virtual void compute_defaults(const Film* film) override;
  };
  

  struct AOIntegrator : public Integrator {
    int pixelbounds[4]                      = { 0, -1, 0, -1 }; // endx < startx or endy < starty means "whole image".
    bool cossample                          = true;
    int nsamples                            = 64;

    virtual ~AOIntegrator() override {}
    virtual IntegratorType type() const override { return IntegratorType::AO; }
    virtual void compute_defaults(const Film* film) override;
  };
  

  //
  // Light types
  //

  enum class LightType {
    Distant,
    Goniometric,
    Infinite,
    Point,
    Projection,
    Spot,
  };


  struct Light {
    Transform lightToWorld; // row major.
    float scale[3] = { 1.0f, 1.0f, 1.0f };

    virtual ~Light() {}
    virtual LightType type() const = 0;
  };


  struct DistantLight : public Light {
    float L[3]    = { 1.0f, 1.0f, 1.0f };
    float from[3] = { 0.0f, 0.0f, 0.0f };
    float to[3]   = { 0.0f, 0.0f, 1.0f };

    virtual ~DistantLight() override {}
    virtual LightType type() const override { return LightType::Distant; }
  };


  struct GoniometricLight : public Light {
    float I[3]    = { 1.0f, 1.0f, 1.0f };
    char* mapname = nullptr;

    virtual ~GoniometricLight() override {}
    virtual LightType type() const override { return LightType::Goniometric; }
  };


  struct InfiniteLight : public Light {
    float L[3]    = { 1.0f, 1.0f, 1.0f };
    int samples   = 1;
    char* mapname = nullptr;

    virtual ~InfiniteLight() override {}
    virtual LightType type() const override { return LightType::Infinite; }
  };


  struct PointLight : public Light {
    float I[3]    = { 1.0f, 1.0f, 1.0f };
    float from[3] = { 0.0f, 0.0f, 0.0f };

    virtual ~PointLight() override {}
    virtual LightType type() const override { return LightType::Point; }
  };


  struct ProjectionLight : public Light {
    float I[3]    = { 1.0f, 1.0f, 1.0f };
    float fov     = 45.0f;
    char* mapname = nullptr;

    virtual ~ProjectionLight() override {}
    virtual LightType type() const override { return LightType::Projection; }
  };


  struct SpotLight : public Light {
    float I[3]            = { 1.0f, 1.0f, 1.0f };
    float from[3]         = { 0.0f, 0.0f, 0.0f };
    float to[3]           = { 0.0f, 0.0f, 1.0f };
    float coneangle       = 30.0f;
    float conedeltaangle  = 5.0f;

    virtual ~SpotLight() override {}
    virtual LightType type() const override { return LightType::Spot; }
  };


  //
  // Material types
  //

  enum class MaterialType {
    Disney,
    Fourier,
    Glass,
    Hair,
    KdSubsurface,
    Matte,
    Metal,
    Mirror,
    Mix,
    None,
    Plastic,
    Substrate,
    Subsurface,
    Translucent,
    Uber,
  };


  struct Material {
    const char* name = nullptr;
    uint32_t bumpmap = kInvalidIndex;

    virtual ~Material() {}
    virtual MaterialType type() const = 0;
  };


  struct DisneyMaterial : public Material {
    ColorTex color            = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    FloatTex anisotropic      = { kInvalidIndex, 0.0f               };
    FloatTex clearcoat        = { kInvalidIndex, 0.0f               };
    FloatTex clearcoatgloss   = { kInvalidIndex, 1.0f               };
    FloatTex eta              = { kInvalidIndex, 1.5f               };
    FloatTex metallic         = { kInvalidIndex, 0.0f               };
    FloatTex roughness        = { kInvalidIndex, 0.5f               };
    ColorTex scatterdistance  = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    FloatTex sheen            = { kInvalidIndex, 0.0f               };
    FloatTex sheentint        = { kInvalidIndex, 0.5f               };
    FloatTex spectrans        = { kInvalidIndex, 0.0f               };
    FloatTex speculartint     = { kInvalidIndex, 0.0f               };
    bool thin                 = false;
    ColorTex difftrans        = { kInvalidIndex, {1.0f, 1.0f, 1.0f} }; // only used if `thin == true`
    ColorTex flatness         = { kInvalidIndex, {0.0f, 0.0f, 0.0f} }; // only used if `thin == true`

    virtual ~DisneyMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Disney; }
  };


  struct FourierMaterial : public Material {
    char* bsdffile = nullptr;

    virtual ~FourierMaterial() override { delete[] bsdffile; }
    virtual MaterialType type() const override { return MaterialType::Fourier; }
  };


  struct GlassMaterial : public Material {
    ColorTex Kr         = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex Kt         = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    FloatTex eta        = { kInvalidIndex, 1.5f };
    FloatTex uroughness = { kInvalidIndex, 0.0f };
    FloatTex vroughness = { kInvalidIndex, 0.0f };
    bool remaproughness = true;

    virtual ~GlassMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Glass; }
  };


  struct HairMaterial : public Material {
    ColorTex sigma_a     = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    ColorTex color       = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    FloatTex eumelanin   = { kInvalidIndex, 1.3f               };
    FloatTex pheomelanin = { kInvalidIndex, 0.0f               };
    FloatTex eta         = { kInvalidIndex, 1.55f              };
    FloatTex beta_m      = { kInvalidIndex, 0.3f               };
    FloatTex beta_n      = { kInvalidIndex, 0.3f               };
    FloatTex alpha       = { kInvalidIndex, 2.0f               };
    bool has_sigma_a     = false;
    bool has_color       = false;

    virtual ~HairMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Hair; }
  };


  struct KdSubsurfaceMaterial : public Material {
    ColorTex Kd         = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    ColorTex mfp        = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    FloatTex eta        = { kInvalidIndex, 1.3f               };
    ColorTex Kr         = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex Kt         = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    FloatTex uroughness = { kInvalidIndex, 0.0f               };
    FloatTex vroughness = { kInvalidIndex, 0.0f               };
    bool remaproughness = true;

    virtual ~KdSubsurfaceMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::KdSubsurface; }
  };


  struct MatteMaterial : public Material {
    ColorTex Kd         = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    FloatTex sigma      = { kInvalidIndex, 0.0f };

    virtual ~MatteMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Matte; }
  };


  struct MetalMaterial : public Material {
    ColorTex eta        = { kInvalidIndex, {0.5f, 0.5f, 0.5f} }; // TODO: indices of refraction for copper
    ColorTex k          = { kInvalidIndex, {0.5f, 0.5f, 0.5f} }; // TODO: absorption coefficients for copper
    FloatTex uroughness = { kInvalidIndex, 0.01f              };
    FloatTex vroughness = { kInvalidIndex, 0.01f              };
    bool remaproughness = true;

    virtual ~MetalMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Metal; }
  };


  struct MirrorMaterial : public Material {
    ColorTex Kr         = { kInvalidIndex, {0.9f, 0.9f, 0.9f} };

    virtual ~MirrorMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Mirror; }
  };


  struct MixMaterial : public Material {
    ColorTex amount         = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    uint32_t namedmaterial1 = kInvalidIndex;
    uint32_t namedmaterial2 = kInvalidIndex;

    virtual ~MixMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Mix; }
  };


  struct NoneMaterial : public Material {
    virtual ~NoneMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::None; }
  };


  struct PlasticMaterial : public Material {
    ColorTex Kd         = { kInvalidIndex, {0.25f, 0.25f, 0.25f} };
    ColorTex Ks         = { kInvalidIndex, {0.25f, 0.25f, 0.25f} };
    FloatTex roughness  = { kInvalidIndex, 0.1f                  };
    bool remaproughness = true;

    virtual ~PlasticMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Plastic; }
  };


  struct SubstrateMaterial : public Material {
    ColorTex Kd         = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    ColorTex Ks         = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    FloatTex uroughness = { kInvalidIndex, 0.1f };
    FloatTex vroughness = { kInvalidIndex, 0.1f };
    bool remaproughness = true;

    virtual ~SubstrateMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Substrate; }
  };


  struct SubsurfaceMaterial : public Material {
    char* coefficients      = nullptr; // name of the measured subsurface scattering coefficients
    ColorTex sigma_a        = { kInvalidIndex, {0.0011f, 0.0024f, 0.014f} };
    ColorTex sigma_prime_s  = { kInvalidIndex, {2.55f, 3.12f, 3.77f} };
    float scale             = 1.0f;
    FloatTex eta            = { kInvalidIndex, 1.33f              };
    ColorTex Kr             = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex Kt             = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    FloatTex uroughness     = { kInvalidIndex, 0.0f               };
    FloatTex vroughness     = { kInvalidIndex, 0.0f               };
    bool remaproughness     = true;

    virtual ~SubsurfaceMaterial() override { delete[] coefficients; }
    virtual MaterialType type() const override { return MaterialType::Subsurface; }
  };


  struct TranslucentMaterial : public Material {
    ColorTex Kd         = { kInvalidIndex, {0.25f, 0.25f, 0.25f} };
    ColorTex Ks         = { kInvalidIndex, {0.25f, 0.25f, 0.25f} };
    ColorTex reflect    = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    ColorTex transmit   = { kInvalidIndex, {0.5f, 0.5f, 0.5f} };
    FloatTex roughness  = { kInvalidIndex, 0.1f };
    bool remaproughness = true;

    virtual ~TranslucentMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Translucent; }
  };


  struct UberMaterial : public Material {
    ColorTex Kd         = { kInvalidIndex, {0.25f, 0.25f, 0.25f} };
    ColorTex Ks         = { kInvalidIndex, {0.25f, 0.25f, 0.25f} };
    ColorTex Kr         = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    ColorTex Kt         = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    FloatTex eta        = { kInvalidIndex, 1.5f };
    ColorTex opacity    = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    FloatTex uroughness = { kInvalidIndex, 0.1f };
    FloatTex vroughness = { kInvalidIndex, 0.1f };
    bool remaproughness = true;

    virtual ~UberMaterial() override {}
    virtual MaterialType type() const override { return MaterialType::Uber; }
  };


  //
  // Medium types
  //

  enum class MediumType {
    Homogeneous,
    Heterogeneous,
  };


  struct Medium {
    const char* mediumName  = nullptr;

    float sigma_a[3]  = { 0.0011f, 0.0024f, 0.0014f };
    float sigma_s[3]  = { 2.55f, 3.21f, 3.77f};
    char* preset      = nullptr;
    float g           = 0.0f;
    float scale       = 1.0f;

    virtual ~Medium() {}
    virtual MediumType type() const = 0;
  };


  struct HomogeneousMedium : public Medium {
    virtual ~HomogeneousMedium() override {}
    virtual MediumType type() const override { return MediumType::Homogeneous; }
  };


  struct HeterogeneousMedium : public Medium {
    float p0[3]    = { 0.0f, 0.0f, 0.0f };
    float p1[3]    = { 1.0f, 1.0f, 1.0f };
    int nx         = 1;
    int ny         = 1;
    int nz         = 1;
    float* density = nullptr;

    virtual ~HeterogeneousMedium() override {}
    virtual MediumType type() const override { return MediumType::Heterogeneous; }
  };


  //
  // Sampler types
  //

  enum class SamplerType {
    ZeroTwoSequence,
    LowDiscrepancy, // An alias for ZeroTwoSequence, kept for backwards compatibility
    Halton,
    MaxMinDist,
    Random,
    Sobol,
    Stratified,
  };


  struct Sampler {
    virtual ~Sampler() {}
    virtual SamplerType type() const = 0;
  };


  struct ZeroTwoSequenceSampler : public Sampler {
    int pixelsamples = 16;

    virtual ~ZeroTwoSequenceSampler() override {}
    virtual SamplerType type() const override { return SamplerType::ZeroTwoSequence; }
  };


  struct HaltonSampler : public Sampler {
    int pixelsamples = 16;

    virtual ~HaltonSampler() override {}
    virtual SamplerType type() const override { return SamplerType::Halton; }
  };


  struct MaxMinDistSampler : public Sampler {
    int pixelsamples = 16;

    virtual ~MaxMinDistSampler() override {}
    virtual SamplerType type() const override { return SamplerType::MaxMinDist; }
  };


  struct RandomSampler : public Sampler {
    int pixelsamples = 16;

    virtual ~RandomSampler() override {}
    virtual SamplerType type() const override { return SamplerType::Random; }
  };


  struct SobolSampler : public Sampler {
    int pixelsamples = 16;

    virtual ~SobolSampler() override {}
    virtual SamplerType type() const override { return SamplerType::Sobol; }
  };


  struct StratifiedSampler : public Sampler {
    bool jitter  = true;
    int xsamples = 2;
    int ysamples = 2;

    virtual ~StratifiedSampler() override {}
    virtual SamplerType type() const override { return SamplerType::Stratified; }
  };


  //
  // Shape types
  //

  enum class ShapeType {
    Cone,
    Curve,
    Cylinder,
    Disk,
    Hyperboloid,
    Paraboloid,
    Sphere,
    TriangleMesh,
    HeightField,
    LoopSubdiv,
    Nurbs,
    PLYMesh,
  };


  enum class CurveBasis {
    Bezier,
    BSpline,
  };


  enum class CurveType {
    Flat,
    Ribbon,
    Cylinder,
  };

  struct TriangleMesh;

  struct Shape {
    Transform shapeToWorld; // If shape is part of an object, this is the shapeToObject transform.
    uint32_t material       = kInvalidIndex;
    uint32_t areaLight      = kInvalidIndex;
    uint32_t insideMedium   = kInvalidIndex;
    uint32_t outsideMedium  = kInvalidIndex;
    bool reverseOrientation = false;
    bool instanced          = false;

    virtual ~Shape() {}
    virtual ShapeType type() const = 0;
    virtual TriangleMesh* triangle_mesh() const { return nullptr; }

    void copy_common_properties(const Shape* other);
  };


  struct Cone : public Shape {
    float radius = 1.0f;
    float height = 1.0f;
    float phimax = 360.0f;

    virtual ~Cone() override {}
    virtual ShapeType type() const override { return ShapeType::Cone; }
  };


  struct Curve : public Shape {
    CurveBasis basis          = CurveBasis::Bezier;
    unsigned int degree       = 3;
    CurveType curvetype       = CurveType::Flat; // This is the `type` param in the file, but that name would clash with our `type()` method.
    float* P                  = nullptr; // Elements 3i, 3i+1 and 3i+2 are the xyz coords for control point i.
    unsigned int num_P        = 0; // Number of control points in P.
    unsigned int num_segments = 0; // Number of curve segments.
    float* N                  = nullptr; // Normals at each segment start and end point. Only used when curve type = "ribbon". When used, there must be exactly `num_segments + 1` values.
    float width0              = 1.0f;
    float width1              = 1.0f;
    int splitdepth            = 3;

    virtual ~Curve() override {
      delete[] P;
      delete[] N;
    }
    virtual ShapeType type() const override { return ShapeType::Curve; }
  };


  struct Cylinder : public Shape {
    float radius  = 1.0f;
    float zmin    = -1.0f;
    float zmax    = 1.0f;
    float phimax  = 360.0f;

    virtual ~Cylinder() override {}
    virtual ShapeType type() const override { return ShapeType::Cylinder; }
  };


  struct Disk : public Shape {
    float height      = 0.0f;
    float radius      = 1.0f;
    float innerradius = 0.0f;
    float phimax      = 360.0f;

    virtual ~Disk() override {}
    virtual ShapeType type() const override { return ShapeType::Disk; }
  };


  struct HeightField : public Shape {
    int nu    = 0;
    int nv    = 0;
    float* Pz = nullptr;

    virtual ~HeightField() override { delete Pz; }
    virtual ShapeType type() const override { return ShapeType::HeightField; }
    virtual TriangleMesh* triangle_mesh() const override;
  };


  struct Hyperboloid : public Shape {
    float p1[3]   = { 0.0f, 0.0f, 0.0f };
    float p2[3]   = { 1.0f, 1.0f, 1.0f };
    float phimax  = 360.0f;

    virtual ~Hyperboloid() override {}
    virtual ShapeType type() const override { return ShapeType::Hyperboloid; }
  };


  struct LoopSubdiv : public Shape {
    int levels               = 3;
    int* indices             = nullptr;
    unsigned int num_indices = 0;
    float* P                 = nullptr; // Elements 3i, 3i+1 and 3i+2 are the xyz coords for point i.
    unsigned int num_points  = 0;       // Number of points. Multiply by 3 to get the number of floats in P.

    virtual ~LoopSubdiv() override {
      delete[] indices;
      delete[] P;
    }
    virtual ShapeType type() const override { return ShapeType::LoopSubdiv; }
    virtual TriangleMesh* triangle_mesh() const override;
  };


  struct Nurbs : public Shape {
    int nu        = 0;
    int nv        = 0;
    int uorder    = 0;
    int vorder    = 0;
    float* uknots = nullptr; // There are `nu + uorder` knots in the u direction.
    float* vknots = nullptr; // There are `nv + vorder` knots in the v direction.
    float u0      = 0.0f;
    float v0      = 0.0f;
    float u1      = 1.0f;
    float v1      = 1.0f;
    float* P      = nullptr; // Elements 3i, 3i+1 and 3i+2 are the xyz coords for control point i. There are `nu*nv` control points.
    float* Pw     = nullptr; // Elements 4i, 4i+1, 4i+2 aand 4i+3 are xyzw coords for control point i; w is a weight value. The are `nu*nv` control points.

    virtual ~Nurbs() override {
      delete[] uknots;
      delete[] vknots;
      delete[] P;
      delete[] Pw;
    }
    virtual ShapeType type() const override { return ShapeType::Nurbs; }
    virtual TriangleMesh* triangle_mesh() const override;
  };


  struct PLYMesh : public Shape {
    char* filename        = nullptr;
    uint32_t alpha        = kInvalidIndex;
    uint32_t shadowalpha  = kInvalidIndex;

    virtual ~PLYMesh() override {
      delete[] filename;
    }
    virtual ShapeType type() const override { return ShapeType::PLYMesh; }
    virtual TriangleMesh* triangle_mesh() const override;
  };


  struct Paraboloid : public Shape {
    float radius  = 1.0f;
    float zmin    = 0.0f;
    float zmax    = 1.0f;
    float phimax  = 360.0f;

    virtual ~Paraboloid() override {}
    virtual ShapeType type() const override { return ShapeType::Paraboloid; }
  };


  struct Sphere : public Shape {
    float radius  = 1.0f;
    float zmin    = 0.0f; // Will be set to -radius
    float zmax    = 0.0f; // Will be set to +radius
    float phimax  = 360.0f;

    virtual ~Sphere() override {}
    virtual ShapeType type() const override { return ShapeType::Sphere; }
  };


  struct TriangleMesh : public Shape {
    int* indices              = nullptr;
    unsigned int num_indices  = 0;
    float* P                  = nullptr; // Elements 3i, 3i+1 and 3i+2 are the xyz position components for vertex i.
    float* N                  = nullptr; // Elements 3i, 3i+1 and 3i+2 are the xyz normal components for veretx i.
    float* S                  = nullptr; // Elements 3i, 3i+1 and 3i+2 are the xyz tangent components for vertex i.
    float* uv                 = nullptr; // Elements 2i and 2i+1 are the uv coords for vertex i.
    unsigned int num_vertices = 0;       // Number of vertices. Multiply by 3 to get the number of floats in P, N or S. Multiply by 2 to get the number of floats in uv.
    uint32_t alpha            = kInvalidIndex;
    uint32_t shadowalpha      = kInvalidIndex;

    virtual ~TriangleMesh() override {
      delete[] indices;
      delete[] P;
      delete[] N;
      delete[] S;
      delete[] uv;
    }
    virtual ShapeType type() const override { return ShapeType::TriangleMesh; }
  };


  //
  // Texture types
  //

  enum class TextureType {
    Bilerp,
    Checkerboard2D,
    Checkerboard3D,
    Constant,
    Dots,
    FBM,
    ImageMap,
    Marble,
    Mix,
    Scale,
    UV,
    Windy,
    Wrinkled,
    PTex,
  };


  enum class TextureData {
    Float,
    Spectrum,
  };


  enum class TexCoordMapping {
    UV,
    Spherical,
    Cylindrical,
    Planar,
  };


  enum class WrapMode {
    Repeat,
    Black,
    Clamp,
  };


  enum class CheckerboardAAMode {
    ClosedForm,
    None,
  };


  struct Texture {
    const char* name;
    TextureData dataType;

    virtual ~Texture() { delete[] name; }
    virtual TextureType type() const = 0;
  };


  struct TextureAnyD : public Texture {
    virtual ~TextureAnyD() override {}
  };


  struct Texture2D : public Texture {
    TexCoordMapping mapping = TexCoordMapping::UV;
    float uscale            = 1.0f;
    float vscale            = 1.0f;
    float udelta            = 0.0f;
    float vdelta            = 0.0f;
    float v1[3]             = { 1.0f, 0.0f, 0.0f };
    float v2[3]             = { 0.0f, 1.0f, 0.0f };

    virtual ~Texture2D() override {}
  };


  struct Texture3D : public Texture {
    Transform objectToTexture; // row major

    virtual ~Texture3D() override {}
  };


  struct BilerpTexture : public Texture2D {
    ColorTex v00 = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    ColorTex v01 = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex v10 = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    ColorTex v11 = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };

    virtual ~BilerpTexture() override {}
    virtual TextureType type() const override { return TextureType::Bilerp; }
  };

  
  struct Checkerboard2DTexture : public Texture2D {
    ColorTex tex1             = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex tex2             = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    CheckerboardAAMode aamode = CheckerboardAAMode::ClosedForm;

    virtual ~Checkerboard2DTexture() override {}
    virtual TextureType type() const override { return TextureType::Checkerboard2D; }
  };

  
  struct Checkerboard3DTexture : public Texture3D {
    ColorTex tex1             = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex tex2             = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };

    virtual ~Checkerboard3DTexture() override {}
    virtual TextureType type() const override { return TextureType::Checkerboard3D; }
  };
  

  struct ConstantTexture : public TextureAnyD {
    float value[3];

    virtual ~ConstantTexture() override {}
    virtual TextureType type() const override { return TextureType::Constant; }
  };
  

  struct DotsTexture : public Texture2D {
    ColorTex inside  = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex outside = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };

    virtual ~DotsTexture() override {}
    virtual TextureType type() const override { return TextureType::Dots; }
  };
  

  struct FBMTexture : public Texture3D {
    int octaves     = 8;
    float roughness = 0.5f;

    virtual ~FBMTexture() override {}
    virtual TextureType type() const override { return TextureType::FBM; }
  };
  

  struct ImageMapTexture : public Texture2D {
    char* filename      = nullptr;
    WrapMode wrap       = WrapMode::Repeat;
    float maxanisotropy = 8.0f;
    bool trilinear      = false;
    float scale         = 1.0f;
    bool gamma          = false;

    virtual ~ImageMapTexture() override { delete[] filename; }
    virtual TextureType type() const override { return TextureType::ImageMap; }
  };
  

  struct MarbleTexture : public Texture3D {
    int octaves     = 8;
    float roughness = 0.5f;
    float scale     = 1.0f;
    float variation = 0.2f;

    virtual ~MarbleTexture() override {}
    virtual TextureType type() const override { return TextureType::Marble; }
  };
  

  struct MixTexture : public TextureAnyD {
    ColorTex tex1   = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex tex2   = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };
    FloatTex amount = { kInvalidIndex, 0.5f               };

    virtual ~MixTexture() override {}
    virtual TextureType type() const override { return TextureType::Mix; }
  };
  

  struct ScaleTexture : public TextureAnyD {
    ColorTex tex1 = { kInvalidIndex, {1.0f, 1.0f, 1.0f} };
    ColorTex tex2 = { kInvalidIndex, {0.0f, 0.0f, 0.0f} };

    virtual ~ScaleTexture() override {}
    virtual TextureType type() const override { return TextureType::Scale; }
  };
  

  struct UVTexture : public Texture2D {
    // not documented

    virtual ~UVTexture() override {}
    virtual TextureType type() const override { return TextureType::UV; }
  };
  

  struct WindyTexture : public Texture3D {
    // not documented

    virtual ~WindyTexture() override {}
    virtual TextureType type() const override { return TextureType::Windy; }
  };
  

  struct WrinkledTexture : public Texture3D {
    int octaves     = 8;
    float roughness = 0.5f;

    virtual ~WrinkledTexture() override {}
    virtual TextureType type() const override { return TextureType::Wrinkled; }
  };


  struct PTexTexture : public Texture2D {
    char* filename = nullptr;
    float gamma    = 2.2f;

    virtual ~PTexTexture() override { delete[] filename; }
    virtual TextureType type() const override { return TextureType::PTex; }
  };
  

  //
  // Object instancing types
  //

  struct Object {
    const char* name = nullptr;
    Transform objectToInstance;
    uint32_t firstShape        = kInvalidIndex;
    unsigned int numShapes     = 0;

    ~Object() {
      delete[] name;
    }
  };


  struct Instance {
    Transform instanceToWorld;
    uint32_t object          = kInvalidIndex; //!< The object that this is an instance of.
    uint32_t areaLight       = kInvalidIndex; //!< If non-null, the instance emits light as described by this area light object.
    uint32_t insideMedium    = kInvalidIndex;
    uint32_t outsideMedium   = kInvalidIndex;
    bool reverseOrientation  = false;
  };


  //
  // Scene types
  //

  struct Scene {
    float startTime          = 0.0f;
    float endTime            = 0.0f;

    Accelerator* accelerator = nullptr;
    Camera* camera           = nullptr;
    Film* film               = nullptr;
    Filter* filter           = nullptr;
    Integrator* integrator   = nullptr;
    Sampler* sampler         = nullptr;

    Medium* outsideMedium    = nullptr; // Borrowed pointer

    std::vector<Shape*>     shapes;
    std::vector<Object*>    objects;
    std::vector<Instance*>  instances;
    std::vector<Light*>     lights;
    std::vector<AreaLight*> areaLights;
    std::vector<Material*>  materials;
    std::vector<Texture*>   textures;
    std::vector<Medium*>    mediums;

    Scene();
    ~Scene();

    /// Call `triangle_mesh()` on the shape at index i in the list. If the
    /// result is non-null, replace the original shape with the new triangle
    /// mesh. Return value indicates whether the original shape was replaced.
    ///
    /// This is safe to call concurrently from multiple threads, as long as
    /// each thread provides a different `shapeIndex` value.
    bool to_triangle_mesh(uint32_t shapeIndex);

    /// Convert all shapes with one of the given types into a triangle mesh.
    /// This is equivalent to iterating over the shapes list & calling
    /// `to_triangle_mesh()` on every item with a type contained in
    /// `typesToConvert`. The return value is `true` if we were able to convert
    /// all relevant shapes successfully, or `false` if there were any we
    /// couldn't convert.
    bool shapes_to_triangle_mesh(Bits<ShapeType> typesToConvert);

    /// Convert all shapes to triangle meshes.
    bool all_to_triangle_mesh();

    /// Shorthand for `shapes_to_triangle_mesh(ShapeType::PLYMesh)`.
    bool load_all_ply_meshes();
  };


  //
  // Error struct declaration
  //

  /// The class used to represent an error during parsing. It records where in
  /// the input file(s) the error occurred.
  class Error {
  public:
    // Error takes a copy of `theFilename`, but takes ownership of `theMessage`.
    Error(const char* theFilename, int64_t theOffset, const char* theMessage);
    ~Error();

    const char* filename() const;
    const char* message() const;
    int64_t offset() const;
    int64_t line() const;
    int64_t column() const;

    bool has_line_and_column() const;
    void set_line_and_column(int64_t theLine, int64_t theColumn);

    int compare(const Error& rhs) const;

  private:
    const char* m_filename = nullptr; //!< Name of the file the error occurred in.
    const char* m_message  = nullptr; //!< The error message.
    int64_t m_offset       = 0;       //!< Char offset within the file at which the error occurred.
    int64_t m_line         = 0;       //!< Line number the error occurred on. The first line in a file is 1, 0 indicates the line number hasn't been calculated yet.
    int64_t m_column       = 0;       //!< Column number the error occurred on. The first column is 1, 0 indicates the column number hasn't been calculated yet.
  };


  //
  // Loader class declaration
  //

  class Loader {
  public:
    Loader();
    ~Loader();

    void set_buffer_capacity(size_t n);
    void set_max_include_depth(uint32_t n);

    bool load(const char* filename);

    Scene* take_scene();
    Scene* borrow_scene();

    const Error* error() const;

  private:
    Scene* m_scene;
    Parser* m_parser;
  };

} // namespace minipbrt
