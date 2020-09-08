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
