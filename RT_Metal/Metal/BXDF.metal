#include "BXDF.hh"

float FrDielectric(float cosThetaI, float etaI, float etaT) {
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    //<<Potentially swap indices of refraction>>
       bool entering = cosThetaI > 0.f;
       if (!entering) {
           auto tmp = etaI;
           etaI = etaT; etaT = tmp;
           cosThetaI = abs(cosThetaI);
       }

    //<<Compute cosThetaT using Snell’s law>>
    float sinThetaI = sqrt(max(FLT_EPSILON, 1 - cosThetaI * cosThetaI));
    float sinThetaT = etaI / etaT * sinThetaI;
       //<<Handle total internal reflection>>
          if (sinThetaT >= 1)
              return 1;

    float cosThetaT = sqrt(max(FLT_EPSILON, 1 - sinThetaT * sinThetaT));

    float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
                  ((etaT * cosThetaI) + (etaI * cosThetaT));
    float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
                  ((etaI * cosThetaI) + (etaT * cosThetaT));
    return (Rparl * Rparl + Rperp * Rperp) / 2;
}

float FrConductor(float cosi, const float eta, const float k)
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
