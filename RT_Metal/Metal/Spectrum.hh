#ifndef Spectrum_h
#define Spectrum_h

// Spectrum Declarations
template <int nSpectrumSamples>
class CoefficientSpectrum {
  public:
    // CoefficientSpectrum Public Methods
    CoefficientSpectrum(float v = 0.f) {
        for (int i = 0; i < nSpectrumSamples; ++i) c[i] = v;
        //DCHECK(!HasNaNs());
    }
    
    CoefficientSpectrum(float3 v) {
        for (int i = 0; i < 3; ++i) c[i] = v[i];
    }
    
    float3 raw() {
        return { c[0], c[1], c[2] };
    }
    
#ifdef DEBUG
    CoefficientSpectrum(const CoefficientSpectrum &s) {
        DCHECK(!s.HasNaNs());
        for (int i = 0; i < nSpectrumSamples; ++i) c[i] = s.c[i];
    }

    CoefficientSpectrum &operator=(const CoefficientSpectrum &s) {
        DCHECK(!s.HasNaNs());
        for (int i = 0; i < nSpectrumSamples; ++i) c[i] = s.c[i];
        return *this;
    }
#endif  // DEBUG
     
    thread CoefficientSpectrum &operator+=(const thread CoefficientSpectrum &s2) {
        DCHECK(!s2.HasNaNs());
        for (int i = 0; i < nSpectrumSamples; ++i) c[i] += s2.c[i];
        return *this;
    }
    CoefficientSpectrum operator+(const thread CoefficientSpectrum &s2) const {
        DCHECK(!s2.HasNaNs());
        CoefficientSpectrum ret = *this;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] += s2.c[i];
        return ret;
    }
    CoefficientSpectrum operator-(const thread CoefficientSpectrum &s2) const {
        DCHECK(!s2.HasNaNs());
        CoefficientSpectrum ret = *this;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] -= s2.c[i];
        return ret;
    }
    CoefficientSpectrum operator/(const thread CoefficientSpectrum &s2) const {
        DCHECK(!s2.HasNaNs());
        CoefficientSpectrum ret = *this;
        for (int i = 0; i < nSpectrumSamples; ++i) {
          CHECK_NE(s2.c[i], 0);
          ret.c[i] /= s2.c[i];
        }
        return ret;
    }
    CoefficientSpectrum operator*(const thread CoefficientSpectrum &sp) const {
        //DCHECK(!sp.HasNaNs());
        CoefficientSpectrum ret = *this;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] *= sp.c[i];
        return ret;
    }
    thread CoefficientSpectrum &operator*=(const thread CoefficientSpectrum &sp) {
        //DCHECK(!sp.HasNaNs());
        for (int i = 0; i < nSpectrumSamples; ++i) c[i] *= sp.c[i];
        return *this;
    }
    CoefficientSpectrum operator*(float a) const {
        CoefficientSpectrum ret = *this;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] *= a;
        //DCHECK(!ret.HasNaNs());
        return ret;
    }
    thread CoefficientSpectrum &operator*=(float a) {
        for (int i = 0; i < nSpectrumSamples; ++i) c[i] *= a;
        //DCHECK(!HasNaNs());
        return *this;
    }
    friend inline CoefficientSpectrum operator*(float a,
                                                const thread CoefficientSpectrum &s) {
        //DCHECK(!std::isnan(a) && !s.HasNaNs());
        return s * a;
    }
    thread CoefficientSpectrum operator/(float a) const {
        //CHECK_NE(a, 0);
        //DCHECK(!std::isnan(a));
        CoefficientSpectrum ret = *this;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] /= a;
        DCHECK(!ret.HasNaNs());
        return ret;
    }
    thread CoefficientSpectrum &operator/=(float a) {
        //CHECK_NE(a, 0);
        //DCHECK(!std::isnan(a));
        for (int i = 0; i < nSpectrumSamples; ++i) c[i] /= a;
        return *this;
    }
    bool operator==(const thread CoefficientSpectrum &sp) const {
        for (int i = 0; i < nSpectrumSamples; ++i)
            if (c[i] != sp.c[i]) return false;
        return true;
    }
    bool operator!=(const thread CoefficientSpectrum &sp) const {
        return !(*this == sp);
    }
    bool IsBlack() const {
        for (int i = 0; i < nSpectrumSamples; ++i)
            if (c[i] != 0.) return false;
        return true;
    }
    friend CoefficientSpectrum Sqrt(const thread CoefficientSpectrum &s) {
        CoefficientSpectrum ret;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] = sqrt(s.c[i]);
        DCHECK(!ret.HasNaNs());
        return ret;
    }
    template <int n>
    friend inline CoefficientSpectrum<n> Pow(const thread CoefficientSpectrum<n> &s,
                                             float e);
    CoefficientSpectrum operator-() const {
        CoefficientSpectrum ret;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] = -c[i];
        return ret;
    }
    friend CoefficientSpectrum Exp(const thread CoefficientSpectrum &s) {
        CoefficientSpectrum ret;
        for (int i = 0; i < nSpectrumSamples; ++i) ret.c[i] = exp(s.c[i]);
        DCHECK(!ret.HasNaNs());
        return ret;
    }
     
    CoefficientSpectrum Clamp(float low = 0, float high = FLT_MAX) const {
        CoefficientSpectrum ret;
        for (int i = 0; i < nSpectrumSamples; ++i)
            ret.c[i] = clamp(c[i], low, high);
        //DCHECK(!ret.HasNaNs());
        return ret;
    }
    float MaxComponentValue() const {
        float m = c[0];
        for (int i = 1; i < nSpectrumSamples; ++i)
            m = max(m, c[i]);
        return m;
    }
//    bool HasNaNs() const {
//        for (int i = 0; i < nSpectrumSamples; ++i)
//            if (isnan(c[i])) return true;
//        return false;
//    }
    
    thread float &operator[](int i) {
        //DCHECK(i >= 0 && i < nSpectrumSamples);
        return c[i];
    }
    float operator[](int i) const {
        //DCHECK(i >= 0 && i < nSpectrumSamples);
        return c[i];
    }

    // CoefficientSpectrum Public Data
    //static const int nSamples = nSpectrumSamples;

  protected:
    // CoefficientSpectrum Protected Data
    float c[nSpectrumSamples];
};

typedef CoefficientSpectrum<3> RGBSpectrum;
typedef float3 Spectrum;

//Float y() const {
//    const Float YWeight[3] = {0.212671f, 0.715160f, 0.072169f};
//    return YWeight[0] * c[0] + YWeight[1] * c[1] + YWeight[2] * c[2];
//}

inline void XYZToRGB(const float xyz[3], float rgb[3]) {
    rgb[0] = 3.240479f * xyz[0] - 1.537150f * xyz[1] - 0.498535f * xyz[2];
    rgb[1] = -0.969256f * xyz[0] + 1.875991f * xyz[1] + 0.041556f * xyz[2];
    rgb[2] = 0.055648f * xyz[0] - 0.204043f * xyz[1] + 1.057311f * xyz[2];
}

inline void RGBToXYZ(const thread float3& rgb, thread float3& xyz) {
    xyz[0] = 0.412453f * rgb[0] + 0.357580f * rgb[1] + 0.180423f * rgb[2];
    xyz[1] = 0.212671f * rgb[0] + 0.715160f * rgb[1] + 0.072169f * rgb[2];
    xyz[2] = 0.019334f * rgb[0] + 0.119193f * rgb[1] + 0.950227f * rgb[2];
}


#endif /* Spectrum_h */
