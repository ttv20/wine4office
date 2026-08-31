struct input
{
    nointerpolation float2 p0 : TRIANGLE0;
    nointerpolation float2 p1 : TRIANGLE1;
    nointerpolation float2 p2 : TRIANGLE2;
    nointerpolation float4 curve0 : CURVE0;
    nointerpolation float4 curve1 : CURVE1;
    nointerpolation float4 curve2 : CURVE2;
    float4 position : SV_POSITION;
};

/* D3D11's standard 16-sample pattern, in sixteenths of a pixel. */
static const int2 sample_positions[16] =
{
    int2( 1,  1), int2(-1, -3), int2(-3,  2), int2( 4, -1),
    int2(-5, -2), int2( 2,  5), int2( 5,  3), int2( 3, -5),
    int2(-2,  6), int2( 0, -7), int2(-4, -6), int2(-6,  4),
    int2(-8,  0), int2( 7, -4), int2( 6,  7), int2(-7, -8),
};

float edge_value(float2 a, float2 b, float2 p)
{
    float2 edge = b - a;
    float2 offset = p - a;

    return edge.x * offset.y - edge.y * offset.x;
}

bool is_top_left(float2 a, float2 b)
{
    float2 edge = b - a;

    return edge.y < 0.0f || (edge.y == 0.0f && edge.x > 0.0f);
}

bool edge_contains(float2 a, float2 b, float2 p)
{
    float value = edge_value(a, b, p);

    return value > 0.0f || (value == 0.0f && is_top_left(a, b));
}

bool edge_may_cover_pixel(float2 a, float2 b, float2 center)
{
    float2 edge = b - a;
    float conservative_radius = 0.5f * (abs(edge.x) + abs(edge.y));

    return edge_value(a, b, center) >= -conservative_radius;
}

uint main(struct input i) : SV_Target
{
    float2 p0 = i.p0, p1 = i.p1, p2 = i.p2, tmp;
    float4 curve0 = i.curve0, curve1 = i.curve1, curve2 = i.curve2, curve_tmp;
    float2 pixel_origin = floor(i.position.xy);
    float2 pixel_center = pixel_origin + 0.5f;
    float area = edge_value(p0, p1, p2);
    float curve_type = curve0.w;
    uint mask = 0;
    unsigned int j;

    if (abs(area) <= 1.0e-8f)
        return 0;
    if (area < 0.0f)
    {
        tmp = p1;
        p1 = p2;
        p2 = tmp;
        curve_tmp = curve1;
        curve1 = curve2;
        curve2 = curve_tmp;
        area = -area;
    }

    if (!edge_may_cover_pixel(p0, p1, pixel_center)
            || !edge_may_cover_pixel(p1, p2, pixel_center)
            || !edge_may_cover_pixel(p2, p0, pixel_center))
        return 0;

    [unroll]
    for (j = 0; j < 16; ++j)
    {
        float2 sample_position = pixel_center + float2(sample_positions[j]) / 16.0f;

        if (edge_contains(p0, p1, sample_position)
                && edge_contains(p1, p2, sample_position)
                && edge_contains(p2, p0, sample_position))
        {
            float w0 = edge_value(p1, p2, sample_position) / area;
            float w1 = edge_value(p2, p0, sample_position) / area;
            float w2 = edge_value(p0, p1, sample_position) / area;
            float3 curve = w0 * curve0.xyz + w1 * curve1.xyz + w2 * curve2.xyz;
            float value = curve_type == 1.0f
                    ? curve.x * curve.x - curve.y
                    : curve.x * curve.x + curve.y * curve.y - 1.0f;

            if (value * curve.z >= 0.0f)
                mask |= 1u << j;
        }
    }
    return mask;
}
