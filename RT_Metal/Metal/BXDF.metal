#include "BXDF.hh"

float3 FrDielectric(float cosi, float eta) {
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

float3 FrConductor(float cosi, const thread float3& eta, const thread float3& k)
{
    auto tmp = (eta*eta + k*k) * cosi * cosi;
    auto Rparl2 = (tmp - (2.f * eta * cosi) + 1) /
        (tmp + (2.f * eta * cosi) + 1);
    auto tmp_f = eta*eta + k*k;
    auto Rperp2 =
        (tmp_f - (2.f * eta * cosi) + cosi * cosi) /
        (tmp_f + (2.f * eta * cosi) + cosi * cosi);

    return 0.5f * (Rparl2 + Rperp2);    
    
//    auto cosThetaI = clamp(cosi, -1.0, 1.0);
//    //Spectrum eta = etat / etai;
//    auto etak = k / eta;
//
//    float cosThetaI2 = cosThetaI * cosThetaI;
//    float sinThetaI2 = 1. - cosThetaI2;
//    auto eta2 = eta * eta;
//    auto etak2 = etak * etak;
//
//    auto t0 = eta2 - etak2 - sinThetaI2;
//    auto a2plusb2 = sqrt(t0 * t0 + 4 * eta2 * etak2);
//    auto t1 = a2plusb2 + cosThetaI2;
//    auto a = sqrt(0.5f * (a2plusb2 + t0));
//    auto t2 = (float)2 * cosThetaI * a;
//    auto Rs = (t1 - t2) / (t1 + t2);
//
//    auto t3 = cosThetaI2 * a2plusb2 + sinThetaI2 * sinThetaI2;
//    auto t4 = t2 * sinThetaI2;
//    auto Rp = Rs * (t3 - t4) / (t3 + t4);
//
//    return 0.5 * (Rp + Rs);
}
