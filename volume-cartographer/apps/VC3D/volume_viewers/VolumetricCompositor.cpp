#include "VolumetricCompositor.hpp"

#include <algorithm>
#include <cmath>

namespace vc3d::volumetric {

namespace {

constexpr float kMaxTiltDeg = 85.0f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Orthonormal screen basis for the turntable camera: the patch is spun by
// azimuth about the normal, then the view tips by tilt about the
// screen-horizontal axis. e1 = screen x, e2 = screen y (down), both in slab
// coordinates; reduces to (1,0,0)/(0,1,0) at zero azimuth and tilt.
struct ScreenBasis {
    std::array<float, 3> e1;
    std::array<float, 3> e2;
};

ScreenBasis screenBasis(const CameraParams& cam)
{
    const float tilt = std::clamp(cam.tiltDeg, 0.0f, kMaxTiltDeg) * kDegToRad;
    const float az = cam.azimuthDeg * kDegToRad;
    const float ct = std::cos(tilt), st = std::sin(tilt);
    const float ca = std::cos(az), sa = std::sin(az);
    return {{ca, -sa, 0.0f},
            {sa * ct, ca * ct, -st}};
}

} // namespace

std::array<float, 3> viewDirection(const CameraParams& cam)
{
    // d = e1 x e2 x ... = the view axis of the turntable camera (toward the
    // camera, d_w = cos(tilt) > 0). In slab coordinates the tilt leans toward
    // the (sin(az), cos(az)) in-plane direction.
    const float tilt = std::clamp(cam.tiltDeg, 0.0f, kMaxTiltDeg) * kDegToRad;
    const float az = cam.azimuthDeg * kDegToRad;
    return {std::sin(tilt) * std::sin(az),
            std::sin(tilt) * std::cos(az),
            std::cos(tilt)};
}

SlabProjection slabProjection(const CameraParams& cam,
                              int numLayers,
                              int zStart,
                              float outputScale,
                              float centerU,
                              float centerV)
{
    const auto d = viewDirection(cam);
    const float slopeU = d[0] / d[2];
    const float slopeV = d[1] / d[2];
    const auto [e1, e2] = screenBasis(cam);

    // A screen point s (relative to the view center) maps onto the layer
    // plane at height w by following the parallel ray s1*e1 + s2*e2 + t*d
    // to w: q_uv = M*s + w*(d_uv/d_w), with
    // M = [e_j(uv) - e_j(w) * d_uv/d_w].
    SlabProjection proj;
    proj.m00 = e1[0] - e1[2] * slopeU;
    proj.m01 = e2[0] - e2[2] * slopeU;
    proj.m10 = e1[1] - e1[2] * slopeV;
    proj.m11 = e2[1] - e2[2] * slopeV;

    // Fold the rotation-center terms into the per-layer offset so the
    // compositor can apply q = M*p + off directly on raw pixel coords.
    const float centerOffU = centerU - (proj.m00 * centerU + proj.m01 * centerV);
    const float centerOffV = centerV - (proj.m10 * centerU + proj.m11 * centerV);

    proj.layerOffsets.resize(std::max(numLayers, 0));
    for (int i = 0; i < numLayers; ++i) {
        const float wPx = float(zStart + i) * outputScale;
        proj.layerOffsets[i] = {centerOffU + wPx * slopeU,
                                centerOffV + wPx * slopeV};
    }
    return proj;
}

PerspectiveCamera perspectiveCamera(const CameraParams& cam,
                                    int numLayers,
                                    int zStart,
                                    float outputScale,
                                    float centerU,
                                    float centerV,
                                    float screenW,
                                    float screenH)
{
    const auto d = viewDirection(cam);
    const auto [e1, e2] = screenBasis(cam);

    // perspective maps to a half-FOV of perspective * 45 deg at the screen
    // edge; the camera distance D follows from the view size. Focal length
    // f = D makes magnification exactly 1 on the plane through the view
    // center perpendicular to the view axis, so the coverage there (and the
    // central ray) match the orthographic render.
    const float p = std::clamp(cam.perspective, 0.01f, 1.0f);
    const float halfSpan = 0.5f * std::max(std::max(screenW, screenH), 1.0f);
    const float dist = halfSpan / std::tan(p * 45.0f * kDegToRad);
    const float focal = dist;

    PerspectiveCamera pc;
    pc.pos = {centerU + dist * d[0], centerV + dist * d[1], dist * d[2]};
    pc.rayBase = {-d[0], -d[1], -d[2]};
    pc.e1OverF = {e1[0] / focal, e1[1] / focal, e1[2] / focal};
    pc.e2OverF = {e2[0] / focal, e2[1] / focal, e2[2] / focal};
    pc.centerU = centerU;
    pc.centerV = centerV;
    pc.layerNum.resize(std::max(numLayers, 0));
    for (int i = 0; i < numLayers; ++i) {
        pc.layerNum[i] = float(zStart + i) * outputScale - pc.pos[2];
    }
    return pc;
}

std::array<float, 256> buildOpacityLut(float alphaMin,
                                       float alphaMax,
                                       float opacity,
                                       float gamma,
                                       uint8_t isoCutoff,
                                       float segmentLength)
{
    std::array<float, 256> lut{};
    const float lo = std::clamp(alphaMin, 0.0f, 1.0f) * 255.0f;
    const float hi = std::clamp(alphaMax, 0.0f, 1.0f) * 255.0f;
    const float range = std::max(hi - lo, 1e-3f);
    const float g = std::max(gamma, 1e-3f);
    const float scale = std::max(opacity, 0.0f) * std::max(segmentLength, 1.0f);
    for (int v = 0; v < 256; ++v) {
        if (v < int(isoCutoff)) {
            lut[v] = 0.0f;
            continue;
        }
        const float rho = std::clamp((float(v) - lo) / range, 0.0f, 1.0f);
        lut[v] = std::min(scale * std::pow(rho, g), 1.0f);
    }
    return lut;
}

void compositeVolumetric(const std::vector<cv::Mat_<uint8_t>>& layerValues,
                         const std::vector<cv::Mat_<uint8_t>>& layerCoverage,
                         const CameraParams& cam,
                         int zStart,
                         float outputScale,
                         const std::array<uint32_t, 256>& colorLut,
                         const std::array<float, 256>& opacityLut,
                         cv::Mat_<cv::Vec3b>& colorOut,
                         cv::Mat_<uint8_t>& coverageOut)
{
    const int numLayers = int(layerValues.size());
    if (numLayers == 0 || layerCoverage.size() != layerValues.size() ||
        layerValues[0].empty()) {
        return;
    }
    const int rows = layerValues[0].rows;
    const int cols = layerValues[0].cols;
    colorOut.create(rows, cols);
    colorOut.setTo(cv::Vec3b(0, 0, 0));
    coverageOut.create(rows, cols);
    coverageOut.setTo(uint8_t(0));

    const bool perspective = cam.perspective > 0.0f;
    const auto proj = slabProjection(cam, numLayers, zStart, outputScale,
                                     float(cols) * 0.5f, float(rows) * 0.5f);
    const auto pcam = perspective
        ? perspectiveCamera(cam, numLayers, zStart, outputScale,
                            float(cols) * 0.5f, float(rows) * 0.5f,
                            float(cols), float(rows))
        : PerspectiveCamera{};

    // Per-value emission premultiplied by alpha, so the inner loop is one
    // multiply-add per channel.
    std::array<std::array<float, 3>, 256> premul;
    for (int v = 0; v < 256; ++v) {
        const uint32_t c = colorLut[v];
        const float a = opacityLut[v];
        premul[v] = {a * float((c >> 16) & 0xFFu),
                     a * float((c >> 8) & 0xFFu),
                     a * float(c & 0xFFu)};
    }

    constexpr float kEarlyOutT = 0.004f;

    for (int y = 0; y < rows; ++y) {
        auto* outRow = colorOut.ptr<cv::Vec3b>(y);
        auto* covRow = coverageOut.ptr<uint8_t>(y);
        for (int x = 0; x < cols; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            float T = 1.0f;
            bool anyValid = false;
            // Orthographic: the linear part is shared by all layers; only the
            // offset varies. Perspective: the pixel's ray direction and the
            // 1/r_w division are per-pixel; per layer it's one multiply-add.
            float baseU = 0.0f, baseV = 0.0f;
            float rayU = 0.0f, rayV = 0.0f, invRayW = 0.0f;
            if (perspective) {
                const float s1 = float(x) - pcam.centerU;
                const float s2 = float(y) - pcam.centerV;
                rayU = pcam.rayBase[0] + s1 * pcam.e1OverF[0] + s2 * pcam.e2OverF[0];
                rayV = pcam.rayBase[1] + s1 * pcam.e1OverF[1] + s2 * pcam.e2OverF[1];
                const float rayW =
                    pcam.rayBase[2] + s1 * pcam.e1OverF[2] + s2 * pcam.e2OverF[2];
                if (rayW >= -1e-4f)
                    continue;  // ray points away from the slab
                invRayW = 1.0f / rayW;
            } else {
                baseU = proj.m00 * float(x) + proj.m01 * float(y);
                baseV = proj.m10 * float(x) + proj.m11 * float(y);
            }
            // Near-to-far: the camera sits on the +w side, so highest w first.
            for (int i = numLayers - 1; i >= 0; --i) {
                float qu, qv;
                if (perspective) {
                    const float t = pcam.layerNum[i] * invRayW;
                    qu = pcam.pos[0] + t * rayU;
                    qv = pcam.pos[1] + t * rayV;
                } else {
                    qu = baseU + proj.layerOffsets[i][0];
                    qv = baseV + proj.layerOffsets[i][1];
                }
                const int x0 = int(std::floor(qu));
                const int y0 = int(std::floor(qv));
                if (x0 < 0 || y0 < 0 || x0 + 1 >= cols || y0 + 1 >= rows)
                    continue;
                const float fx = qu - float(x0);
                const float fy = qv - float(y0);
                const auto& vals = layerValues[i];
                const auto& cov = layerCoverage[i];
                const uint8_t* v0 = vals.ptr<uint8_t>(y0) + x0;
                const uint8_t* v1 = vals.ptr<uint8_t>(y0 + 1) + x0;
                const uint8_t* c0 = cov.ptr<uint8_t>(y0) + x0;
                const uint8_t* c1 = cov.ptr<uint8_t>(y0 + 1) + x0;
                // Coverage-weighted bilinear: uncovered texels are fully
                // transparent, not value 0 (avoids dark halos at patch
                // borders under tilt).
                const float w00 = c0[0] ? (1.0f - fx) * (1.0f - fy) : 0.0f;
                const float w10 = c0[1] ? fx * (1.0f - fy) : 0.0f;
                const float w01 = c1[0] ? (1.0f - fx) * fy : 0.0f;
                const float w11 = c1[1] ? fx * fy : 0.0f;
                const float wsum = w00 + w10 + w01 + w11;
                if (wsum <= 1e-6f)
                    continue;
                const float value = (w00 * float(v0[0]) + w10 * float(v0[1]) +
                                     w01 * float(v1[0]) + w11 * float(v1[1])) / wsum;
                anyValid = true;
                const int idx = std::clamp(int(value + 0.5f), 0, 255);
                const float alpha = opacityLut[idx];
                if (alpha <= 0.0f)
                    continue;
                const auto& e = premul[idx];
                r += T * e[0];
                g += T * e[1];
                b += T * e[2];
                T *= 1.0f - alpha;
                if (T < kEarlyOutT)
                    break;
            }
            if (anyValid) {
                outRow[x] = cv::Vec3b(uint8_t(std::min(r, 255.0f)),
                                      uint8_t(std::min(g, 255.0f)),
                                      uint8_t(std::min(b, 255.0f)));
                covRow[x] = 1;
            }
        }
    }
}

} // namespace vc3d::volumetric
