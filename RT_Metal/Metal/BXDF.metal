#include "BXDF.hh"

float FrDielectric(float cosi, float eta) {
    cosi = clamp(cosi, -1.0, 1.0);
    //<<Potentially swap indices of refraction>>
       bool entering = cosi > 0.f;
       if (!entering) {
           eta = 1 / eta;
           cosi = -cosi;
       }

    // Compute $\cos\,\theta_\roman{t}$ for Fresnel equations using Snell's law
    float sin2Theta_i = 1 - cosi * cosi;
    float sin2Theta_t = sin2Theta_i / Sqr(eta);
    if (sin2Theta_t >= 1)
        return 1.f;
    float cosTheta_t = sqrt(max(0.0, 1 - sin2Theta_t));

    float r_parl = (eta * cosi - cosTheta_t) / (eta * cosi + cosTheta_t);
    float r_perp = (cosi - eta * cosTheta_t) / (cosi + eta * cosTheta_t);
    return (r_parl * r_parl + r_perp * r_perp) / 2;
}

float FrConductor(float cosi, float eta, const float k)
{
    float tmp = (eta*eta + k*k) * cosi * cosi;
    float Rparl2 = (tmp - (2.f * eta * cosi) + 1) /
        (tmp + (2.f * eta * cosi) + 1);
    float tmp_f = eta*eta + k*k;
    float Rperp2 =
        (tmp_f - (2.f * eta * cosi) + cosi * cosi) /
        (tmp_f + (2.f * eta * cosi) + cosi * cosi);

    return 0.5f * (Rparl2 + Rperp2);
}
