#pragma once

struct WeightedMovingVarianceParams
{
    WeightedMovingVarianceParams(bool _enableWeight,
                                    bool _enableThreshold,
                                    float _threshold,
                                    float _weight1,
                                    float _weight2,
                                    float _weight3)
        : enableWeight(_enableWeight),
            enableThreshold(_enableThreshold),
            threshold(_threshold),
            weight1(_weight1),
            weight2(_weight2),
            weight3(_weight3),
            thresholdSquared(_threshold * _threshold)
    {
    }

    const bool enableWeight;
    const bool enableThreshold;
    const float threshold;
    const float thresholdSquared;

    const float weight1;
    const float weight2;
    const float weight3;
};
