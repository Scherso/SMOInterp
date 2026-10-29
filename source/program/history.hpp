#pragma once

#include <cstddef>
#include <cstdint>

#include <atomic>
#include <cmath>

#include "tuning.hpp"

/*
 * Shared by actors.cpp and effects.cpp: both keep the last two ticks of a transform per game object,
 * looked up by the object's address from the engine's worker threads, and blend between them.
 */
namespace smo::history {
    /*
     * The 4x3 transforms the mod blends (nn::g3d bone world matrices, nn::vfx emitter placements) are
     * stored as four float4s: three basis vectors, then the translation. Each float4's last float is
     * padding.
     */
    constexpr size_t FloatsPerColumn = 4;
    constexpr size_t BasisColumns = 3;
    constexpr size_t TransformFloats = (BasisColumns + 1) * FloatsPerColumn;
    constexpr size_t TranslationOffset = BasisColumns * FloatsPerColumn;

    /* A basis vector shorter than this is degenerate (scaled to nothing): no rotation to extract. */
    constexpr float MinBasisLength = 1e-6f;
    /* Quaternions closer than this (cosine of half the angle) are blended linearly; slerp's sin(theta) would vanish. */
    constexpr float SlerpLinearThreshold = 0.9995f;

    using tuning::TeleportDistance;

    inline bool IsTeleport(const float* prev, const float* curr) {
        float dx = curr[TranslationOffset + 0] - prev[TranslationOffset + 0];
        float dy = curr[TranslationOffset + 1] - prev[TranslationOffset + 1];
        float dz = curr[TranslationOffset + 2] - prev[TranslationOffset + 2];
        return std::sqrt(dx * dx + dy * dy + dz * dz) > TeleportDistance;
    }

    /* out = prev + (curr - prev) * alpha, element-wise. */
    inline void Blend(float* out, const float* prev, const float* curr, size_t count, float alpha) {
        for (size_t i = 0; i < count; ++i)
            out[i] = prev[i] + (curr[i] - prev[i]) * alpha;
    }

    /*
     * Rigid blending for 4x3 transforms stored as four float4 columns (three basis columns, then
     * translation), as nn::g3d stores bone world matrices. Each transform is split into rotation
     * (a quaternion), per-column scale and translation; rotation is slerped, which stays a proper
     * rotation even past t = 1, where blending matrix components would stretch and shear.
     */
    struct Quat {
        float w, x, y, z;
    };

    /* Rotation and per-column scale of a 4x3 column-major transform. False if it isn't a rotation times a positive
     * scale. */
    inline bool Decompose(const float* m, Quat& q, float scale[3]) {
        float c[BasisColumns][3];
        for (size_t i = 0; i < BasisColumns; ++i) {
            const float* col = m + i * FloatsPerColumn;
            scale[i] = std::sqrt(col[0] * col[0] + col[1] * col[1] + col[2] * col[2]);
            if (!(scale[i] > MinBasisLength))
                return false;
            for (int j = 0; j < 3; ++j)
                c[i][j] = col[j] / scale[i];
        }
        /* c[i] is column i; a mirrored (det < 0) basis has no quaternion. */
        float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) -
                    c[1][0] * (c[0][1] * c[2][2] - c[0][2] * c[2][1]) +
                    c[2][0] * (c[0][1] * c[1][2] - c[0][2] * c[1][1]);
        if (!(det > 0.f))
            return false;

        /* Shepperd's method; R(row, col) = c[col][row]. */
        float r00 = c[0][0], r11 = c[1][1], r22 = c[2][2];
        float trace = r00 + r11 + r22;
        if (trace > 0.f) {
            float s = std::sqrt(trace + 1.f) * 2.f;
            q = { 0.25f * s, (c[1][2] - c[2][1]) / s, (c[2][0] - c[0][2]) / s, (c[0][1] - c[1][0]) / s };
        } else if (r00 > r11 && r00 > r22) {
            float s = std::sqrt(1.f + r00 - r11 - r22) * 2.f;
            q = { (c[1][2] - c[2][1]) / s, 0.25f * s, (c[1][0] + c[0][1]) / s, (c[2][0] + c[0][2]) / s };
        } else if (r11 > r22) {
            float s = std::sqrt(1.f + r11 - r00 - r22) * 2.f;
            q = { (c[2][0] - c[0][2]) / s, (c[1][0] + c[0][1]) / s, 0.25f * s, (c[2][1] + c[1][2]) / s };
        } else {
            float s = std::sqrt(1.f + r22 - r00 - r11) * 2.f;
            q = { (c[0][1] - c[1][0]) / s, (c[2][0] + c[0][2]) / s, (c[2][1] + c[1][2]) / s, 0.25f * s };
        }
        return true;
    }

    /* q0 -> q1 along the shorter arc; t outside [0, 1] keeps turning at the same rate. */
    inline Quat Slerp(Quat a, Quat b, float t) {
        float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
        if (d < 0.f) {
            b = { -b.w, -b.x, -b.y, -b.z };
            d = -d;
        }
        float wa, wb;
        if (d > SlerpLinearThreshold) {
            /* Nearly the same rotation: plain lerp, normalized below. */
            wa = 1.f - t;
            wb = t;
        } else {
            float theta = std::acos(d);
            float sinTheta = std::sin(theta);
            wa = std::sin((1.f - t) * theta) / sinTheta;
            wb = std::sin(t * theta) / sinTheta;
        }
        Quat r = { wa * a.w + wb * b.w, wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z };
        float len = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
        return { r.w / len, r.x / len, r.y / len, r.z / len };
    }

    /* Cosine of half the angle between two rotations (1 = identical, 0 = 180 degrees apart). */
    inline float HalfTurnCos(Quat a, Quat b) {
        return std::fabs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
    }

    /* One transform: rotation slerped, scale and translation blended linearly. */
    inline void BlendRigid(float* out, const float* prev, const float* curr, float t) {
        Quat qa, qb;
        float sa[3], sb[3];
        if (!Decompose(prev, qa, sa) || !Decompose(curr, qb, sb)) {
            Blend(out, prev, curr, TransformFloats, t);
            return;
        }
        Quat q = Slerp(qa, qb, t);
        float r[3][3] = {
            { 1.f - 2.f * (q.y * q.y + q.z * q.z), 2.f * (q.x * q.y + q.w * q.z), 2.f * (q.x * q.z - q.w * q.y) },
            { 2.f * (q.x * q.y - q.w * q.z), 1.f - 2.f * (q.x * q.x + q.z * q.z), 2.f * (q.y * q.z + q.w * q.x) },
            { 2.f * (q.x * q.z + q.w * q.y), 2.f * (q.y * q.z - q.w * q.x), 1.f - 2.f * (q.x * q.x + q.y * q.y) },
        };
        for (size_t i = 0; i < BasisColumns; ++i) {
            float* col = out + i * FloatsPerColumn;
            float scale = std::fmax(sa[i] + (sb[i] - sa[i]) * t, 0.f);
            for (int j = 0; j < 3; ++j)
                col[j] = r[i][j] * scale;
            col[3] = curr[i * FloatsPerColumn + 3]; /* padding */
        }
        Blend(out + TranslationOffset, prev + TranslationOffset, curr + TranslationOffset, FloatsPerColumn, t);
    }

    /* The smallest HalfTurnCos over every rotation-decomposable transform: how far the fastest one turned. */
    inline float MinHalfTurnCos(const float* prev, const float* curr, size_t transforms) {
        float least = 1.f;
        for (size_t i = 0; i < transforms; ++i) {
            Quat qa, qb;
            float sa[3], sb[3];
            if (Decompose(prev + i * TransformFloats, qa, sa) && Decompose(curr + i * TransformFloats, qb, sb))
                least = std::fmin(least, HalfTurnCos(qa, qb));
        }
        return least;
    }

    /*
     * Fixed-size open-addressing map from an object's address to an Entry, safe for concurrent
     * lookups and inserts. Entries are never removed one by one: Entry needs a
     * std::atomic<uintptr_t> key, and the owner clears the keys (with ForEach) when nothing can be
     * looking anything up.
     */
    template<typename Entry, size_t Size>
    class PointerTable {
        static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");

    public:
        /* Returns nullptr if the object isn't present (and !insert) or its probe window is full. */
        Entry* Find(const void* object, bool insert) {
            uintptr_t key = reinterpret_cast<uintptr_t>(object);
            size_t start = Hash(key);
            for (size_t i = 0; i < MaxProbe; ++i) {
                Entry& e = m_Entries[(start + i) & (Size - 1)];
                uintptr_t found = e.key.load(std::memory_order_acquire);
                if (found == key)
                    return &e;
                if (found == 0) {
                    if (!insert)
                        return nullptr;
                    uintptr_t expected = 0;
                    if (e.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel) || expected == key)
                        return &e;
                }
            }
            return nullptr;
        }

        template<typename Fn>
        void ForEach(Fn fn) {
            for (auto& e : m_Entries)
                fn(e);
        }

    private:
        static constexpr size_t MaxProbe = tuning::TableMaxProbe;

        /*
         * MurmurHash3's 64-bit finalizer. Objects are aligned, so their addresses share low bits;
         * mixing makes the low bits used as the table index depend on the whole address.
         */
        static size_t Hash(uintptr_t key) {
            key ^= key >> 33;
            key *= 0xff51afd7ed558ccdull;
            key ^= key >> 33;
            return static_cast<size_t>(key);
        }

        Entry m_Entries[Size];
    };
}
