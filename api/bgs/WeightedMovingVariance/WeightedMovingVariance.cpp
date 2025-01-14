#include "WeightedMovingVariance.hpp"

// opencv legacy includes
#include <opencv2/imgproc/types_c.h>
#include <execution>
#include <iostream>

using namespace sky360lib::bgs;

WeightedMovingVariance::WeightedMovingVariance(const WeightedMovingVarianceParams &_params,
                                               size_t _numProcessesParallel)
    : CoreBgs(_numProcessesParallel),
      m_params(_params)
{
}

WeightedMovingVariance::~WeightedMovingVariance()
{
}

void WeightedMovingVariance::getBackgroundImage(cv::Mat &)
{
    // Not implemented
}

void WeightedMovingVariance::initialize(const cv::Mat &)
{
    imgInputPrev.resize(m_numProcessesParallel);
    for (size_t i = 0; i < m_numProcessesParallel; ++i)
    {
        imgInputPrev[i].currentRollingIdx = 0;
        imgInputPrev[i].firstPhase = 0;
        imgInputPrev[i].pImgSize = m_imgSizesParallel[i].get();
        imgInputPrev[i].pImgInput = nullptr;
        imgInputPrev[i].pImgInputPrev1 = nullptr;
        imgInputPrev[i].pImgInputPrev2 = nullptr;
        imgInputPrev[i].pImgMem[0] = std::make_unique_for_overwrite<uint8_t[]>(imgInputPrev[i].pImgSize->sizeInBytes);
        imgInputPrev[i].pImgMem[1] = std::make_unique_for_overwrite<uint8_t[]>(imgInputPrev[i].pImgSize->sizeInBytes);
        imgInputPrev[i].pImgMem[2] = std::make_unique_for_overwrite<uint8_t[]>(imgInputPrev[i].pImgSize->sizeInBytes);
        rollImages(imgInputPrev[i]);
    }
}

void WeightedMovingVariance::rollImages(RollingImages &rollingImages)
{
    const auto rollingIdx = ROLLING_BG_IDX[rollingImages.currentRollingIdx % 3];
    rollingImages.pImgInput = rollingImages.pImgMem[rollingIdx[0]].get();
    rollingImages.pImgInputPrev1 = rollingImages.pImgMem[rollingIdx[1]].get();
    rollingImages.pImgInputPrev2 = rollingImages.pImgMem[rollingIdx[2]].get();

    ++rollingImages.currentRollingIdx;
}

void WeightedMovingVariance::process(const cv::Mat &_imgInput, cv::Mat &_imgOutput, int _numProcess)
{
    if (_imgOutput.empty())
    {
        _imgOutput.create(_imgInput.size(), CV_8UC1);
    }
    process(_imgInput, _imgOutput, imgInputPrev[_numProcess], m_params);
    rollImages(imgInputPrev[_numProcess]);
}

void WeightedMovingVariance::process(const cv::Mat &_inImage,
                                     cv::Mat &_outImg,
                                     RollingImages &_imgInputPrev,
                                     const WeightedMovingVarianceParams &_params)
{
    memcpy(_imgInputPrev.pImgInput, _inImage.data, _imgInputPrev.pImgSize->sizeInBytes);

    if (_imgInputPrev.firstPhase < 2)
    {
        ++_imgInputPrev.firstPhase;
        return;
    }

    if (_imgInputPrev.pImgSize->numChannels == 1)
    {
        if (_imgInputPrev.pImgSize->bytesPerPixel == 1)
        {
            weightedVarianceMono(_imgInputPrev.pImgInput, _imgInputPrev.pImgInputPrev1, _imgInputPrev.pImgInputPrev2,
                                _outImg.data, (size_t)_imgInputPrev.pImgSize->numPixels, 
                                _params.weight, _params.enableThreshold, _params.thresholdSquared);
        }
        else
        {
            weightedVarianceMono((uint16_t*)_imgInputPrev.pImgInput, (uint16_t*)_imgInputPrev.pImgInputPrev1, (uint16_t*)_imgInputPrev.pImgInputPrev2,
                                _outImg.data, (size_t)_imgInputPrev.pImgSize->numPixels, 
                                _params.weight, _params.enableThreshold, _params.thresholdSquared16);
        }
    }
    else
    {
        if (_imgInputPrev.pImgSize->bytesPerPixel == 1)
        {
            weightedVarianceColor(_imgInputPrev.pImgInput, _imgInputPrev.pImgInputPrev1, _imgInputPrev.pImgInputPrev2,
                                _outImg.data, (size_t)_imgInputPrev.pImgSize->numPixels, 
                                _params.weight, _params.enableThreshold, _params.thresholdSquared);
        }
        else
        {
            weightedVarianceColor((uint16_t*)_imgInputPrev.pImgInput, (uint16_t*)_imgInputPrev.pImgInputPrev1, (uint16_t*)_imgInputPrev.pImgInputPrev2,
                                _outImg.data, (size_t)_imgInputPrev.pImgSize->numPixels, 
                                _params.weight, _params.enableThreshold, _params.thresholdSquared16);
        }
    }
}

template<class T>
inline void calcWeightedVarianceMono(const T *const i1, const T *const i2, const T *const i3,
                                     uint8_t *const o, uint32_t totalPixels, const float* weight)
{
    for (uint32_t i{0}; i < totalPixels; ++i)
    {
        const float dI[]{(float)i1[i], (float)i2[i], (float)i3[i]};
        const float mean{(dI[0] * weight[0]) + (dI[1] * weight[1]) + (dI[2] * weight[2])};
        const float value[]{dI[0] - mean, dI[1] - mean, dI[2] - mean};
        o[i] = std::sqrt(((value[0] * value[0]) * weight[0]) + ((value[1] * value[1]) * weight[1]) + ((value[2] * value[2]) * weight[2]));
    }
}

template<class T>
inline void calcWeightedVarianceMonoThreshold(const T *const i1, const T *const i2, const T *const i3,
                                              uint8_t *const o, uint32_t totalPixels, 
                                              const float* weight, const float thresholdSquared)
{
    for (uint32_t i{0}; i < totalPixels; ++i)
    {
        const float dI[]{(float)i1[i], (float)i2[i], (float)i3[i]};
        const float mean{(dI[0] * weight[0]) + (dI[1] * weight[1]) + (dI[2] * weight[2])};
        const float value[]{dI[0] - mean, dI[1] - mean, dI[2] - mean};
        const float result{((value[0] * value[0]) * weight[0]) + ((value[1] * value[1]) * weight[1]) + ((value[2] * value[2]) * weight[2])};
        o[i] = result > thresholdSquared ? UCHAR_MAX : ZERO_UC;
    }
}

template<class T>
inline void calcWeightedVarianceColor(const T *const i1, const T *const i2, const T *const i3,
                                      uint8_t *const o, uint32_t totalPixels, 
                                      const float* weight)
{
    for (uint32_t j{0}, j3{0}; j < totalPixels; ++j, j3 += 3)
    {
        const float dI1[]{(float)i1[j3], (float)i1[j3 + 1], (float)i1[j3 + 2]};
        const float dI2[]{(float)i2[j3], (float)i2[j3 + 1], (float)i2[j3 + 2]};
        const float dI3[]{(float)i3[j3], (float)i3[j3 + 1], (float)i3[j3 + 2]};
        const float meanR{(dI1[0] * weight[0]) + (dI2[0] * weight[1]) + (dI3[0] * weight[2])};
        const float meanG{(dI1[1] * weight[0]) + (dI2[1] * weight[1]) + (dI3[1] * weight[2])};
        const float meanB{(dI1[2] * weight[0]) + (dI2[2] * weight[1]) + (dI3[2] * weight[2])};
        const float valueR[]{dI1[0] - meanR, dI2[0] - meanR, dI2[0] - meanR};
        const float valueG[]{dI1[1] - meanG, dI2[1] - meanG, dI2[1] - meanG};
        const float valueB[]{dI1[2] - meanB, dI2[2] - meanB, dI2[2] - meanB};
        const float r{std::sqrt(((valueR[0] * valueR[0]) * weight[0]) + ((valueR[1] * valueR[1]) * weight[1]) + ((valueR[2] * valueR[2]) * weight[2]))};
        const float g{std::sqrt(((valueG[0] * valueG[0]) * weight[0]) + ((valueG[1] * valueG[1]) * weight[1]) + ((valueG[2] * valueG[2]) * weight[2]))};
        const float b{std::sqrt(((valueB[0] * valueB[0]) * weight[0]) + ((valueB[1] * valueB[1]) * weight[1]) + ((valueB[2] * valueB[2]) * weight[2]))};
        o[j] = 0.299f * r + 0.587f * g + 0.114f * b;
    }
}

template<class T>
inline void calcWeightedVarianceColorThreshold(const T *const i1, const T *const i2, const T *const i3,
                                               uint8_t *const o, uint32_t totalPixels, 
                                               const float* weight, const float thresholdSquared)
{
    for (uint32_t j{0}, j3{0}; j < totalPixels; ++j, j3 += 3)
    {
        const float dI1[]{(float)i1[j3], (float)i1[j3 + 1], (float)i1[j3 + 2]};
        const float dI2[]{(float)i2[j3], (float)i2[j3 + 1], (float)i2[j3 + 2]};
        const float dI3[]{(float)i3[j3], (float)i3[j3 + 1], (float)i3[j3 + 2]};
        const float meanR{(dI1[0] * weight[0]) + (dI2[0] * weight[1]) + (dI3[0] * weight[2])};
        const float meanG{(dI1[1] * weight[0]) + (dI2[1] * weight[1]) + (dI3[1] * weight[2])};
        const float meanB{(dI1[2] * weight[0]) + (dI2[2] * weight[1]) + (dI3[2] * weight[2])};
        const float valueR[]{dI1[0] - meanR, dI2[0] - meanR, dI2[0] - meanR};
        const float valueG[]{dI1[1] - meanG, dI2[1] - meanG, dI2[1] - meanG};
        const float valueB[]{dI1[2] - meanB, dI2[2] - meanB, dI2[2] - meanB};
        const float r2{((valueR[0] * valueR[0]) * weight[0]) + ((valueR[1] * valueR[1]) * weight[1]) + ((valueR[2] * valueR[2]) * weight[2])};
        const float g2{((valueG[0] * valueG[0]) * weight[0]) + ((valueG[1] * valueG[1]) * weight[1]) + ((valueG[2] * valueG[2]) * weight[2])};
        const float b2{((valueB[0] * valueB[0]) * weight[0]) + ((valueB[1] * valueB[1]) * weight[1]) + ((valueB[2] * valueB[2]) * weight[2])};
        const float result{0.299f * r2 + 0.587f * g2 + 0.114f * b2};
        o[j] = result > thresholdSquared ? UCHAR_MAX : ZERO_UC;
    }
}

template<class T>
void WeightedMovingVariance::weightedVarianceMono(
    const T *const img1,
    const T *const img2,
    const T *const img3,
    uint8_t *const outImg,
    const size_t totalPixels,
    const float* weight,
    const bool enableThreshold, 
    const float thresholdSquared)
{
    if (enableThreshold)
        calcWeightedVarianceMonoThreshold(img1, img2, img3, outImg, totalPixels, weight, thresholdSquared);
    else
        calcWeightedVarianceMono(img1, img2, img3, outImg, totalPixels, weight);
}

template<class T>
void WeightedMovingVariance::weightedVarianceColor(
    const T *const img1,
    const T *const img2,
    const T *const img3,
    uint8_t *const outImg,
    const size_t totalPixels,
    const float* weight, 
    const bool enableThreshold,
    const float thresholdSquared)
{
    if (enableThreshold)
        calcWeightedVarianceColorThreshold(img1, img2, img3, outImg, totalPixels, weight, thresholdSquared);
    else
        calcWeightedVarianceColor(img1, img2, img3, outImg, totalPixels, weight);
}
