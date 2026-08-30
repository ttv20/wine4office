struct input
{
    nointerpolation float2 p0 : TRIANGLE0;
    nointerpolation float2 p1 : TRIANGLE1;
    nointerpolation float2 p2 : TRIANGLE2;
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

bool edge_fully_covers_pixel(float2 a, float2 b, float2 center)
{
    float2 edge = b - a;
    float conservative_radius = 0.5f * (abs(edge.x) + abs(edge.y));

    /* Use the complete pixel square, rather than only the fixed sample
     * positions. A strict comparison also leaves top-left edge ownership to
     * the exact sample loop. */
    return edge_value(a, b, center) > conservative_radius;
}

uint main(struct input i) : SV_Target
{
    float2 p0 = i.p0, p1 = i.p1, p2 = i.p2, tmp;
    float2 pixel_origin = floor(i.position.xy);
    float2 pixel_center = pixel_origin + 0.5f;
    float area = edge_value(p0, p1, p2);
    uint mask = 0;
    unsigned int j;

    if (abs(area) <= 1.0e-8f)
        return 0;
    if (area < 0.0f)
    {
        tmp = p1;
        p1 = p2;
        p2 = tmp;
    }

    if (!edge_may_cover_pixel(p0, p1, pixel_center)
            || !edge_may_cover_pixel(p1, p2, pixel_center)
            || !edge_may_cover_pixel(p2, p0, pixel_center))
        return 0;

    if (edge_fully_covers_pixel(p0, p1, pixel_center)
            && edge_fully_covers_pixel(p1, p2, pixel_center)
            && edge_fully_covers_pixel(p2, p0, pixel_center))
        return 0xffffu;

    [unroll]
    for (j = 0; j < 16; ++j)
    {
        float2 sample_position = pixel_center + float2(sample_positions[j]) / 16.0f;

        if (edge_contains(p0, p1, sample_position)
                && edge_contains(p1, p2, sample_position)
                && edge_contains(p2, p0, sample_position))
            mask |= 1u << j;
    }
    return mask;
}
