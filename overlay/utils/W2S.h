#pragma once
#include "../rbx/math/math.h"
#include "../rbx/offsets.h"
#include "../rbx/globals/options.h"
#include "../rbx/globals/globals.h"
#include "imgui/imgui.h"

inline Vectors::Vector2 WorldToScreen(Vectors::Vector3 world)
{
    if (!Globals::Viewport::Valid)
        return { -1, -1 };

    const auto& viewmatrix = Globals::Viewport::ViewMatrix;

    Vectors::Vector4 quaternion;

    quaternion.x = (world.x * viewmatrix.data[0]) + (world.y * viewmatrix.data[1]) + (world.z * viewmatrix.data[2]) + viewmatrix.data[3];
    quaternion.y = (world.x * viewmatrix.data[4]) + (world.y * viewmatrix.data[5]) + (world.z * viewmatrix.data[6]) + viewmatrix.data[7];
    quaternion.z = (world.x * viewmatrix.data[8]) + (world.y * viewmatrix.data[9]) + (world.z * viewmatrix.data[10]) + viewmatrix.data[11];
    quaternion.w = (world.x * viewmatrix.data[12]) + (world.y * viewmatrix.data[13]) + (world.z * viewmatrix.data[14]) + viewmatrix.data[15];

    if (quaternion.w < 0.1f)
        return{ -1, -1 };

    float inv_w = 1.0f / quaternion.w;
    Vectors::Vector3 ndc;
    ndc.x = quaternion.x * inv_w;
    ndc.y = quaternion.y * inv_w;
    ndc.z = quaternion.z * inv_w;

    const auto& Dimensions = Globals::Viewport::Dimensions;
    const auto& point = Globals::Viewport::ScreenPos;

    return
    {
        ((Dimensions.x / 2.0f) * ndc.x + (Dimensions.x / 2.0f)) + point.x,
        (-(Dimensions.y / 2.0f) * ndc.y + (Dimensions.y / 2.0f)) + point.y
    };
}