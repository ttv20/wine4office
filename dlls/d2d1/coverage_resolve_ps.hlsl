#define BRUSH_TYPE_SOLID  0
#define BRUSH_TYPE_LINEAR 1

bool outline;
bool is_curve;
bool is_arc;
bool antialias;
struct brush
{
    uint type;
    float opacity;
    float4 data[3];
} colour_brush, opacity_brush;
bool coverage_resolve;
bool coverage_msaa;
uint coverage_sample_count;
uint coverage_pad;
float4 coverage_world_x;
float4 coverage_world_y;

/* Keep t0-t3 identical to shape_ps: bitmap brushes use t0/t1 and gradient
 * buffers use t2/t3. Coverage resources live after the normal brush slots. */
Buffer<float4> gradient : register(t2);
Texture2D<uint> coverage_mask : register(t4);
Texture2DMS<uint> coverage_msaa_mask : register(t5);

float4 sample_gradient(uint stop_count, float position)
{
    float4 c_low, c_high;
    float p_low, p_high;
    uint i;

    p_low = gradient.Load(0).x;
    c_low = gradient.Load(1);
    c_high = c_low;
    if (position < p_low)
        return c_low;

    [loop]
    for (i = 1; i < stop_count; ++i)
    {
        p_high = gradient.Load(i * 2).x;
        c_high = gradient.Load(i * 2 + 1);
        if (position >= p_low && position <= p_high)
            return lerp(c_low, c_high, (position - p_low) / (p_high - p_low));
        p_low = p_high;
        c_low = c_high;
    }
    return c_high;
}

float4 sample_brush(float2 position)
{
    float2 start, end, v_p, v_q;
    float p;

    if (colour_brush.type == BRUSH_TYPE_SOLID)
        return colour_brush.data[0] * colour_brush.opacity;
    if (colour_brush.type == BRUSH_TYPE_LINEAR)
    {
        start = colour_brush.data[0].xy;
        end = colour_brush.data[0].zw;
        v_p = position - start;
        v_q = end - start;
        p = dot(v_q, v_p) / dot(v_q, v_q);
        return sample_gradient(asuint(colour_brush.data[1].x), p) * colour_brush.opacity;
    }
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

float4 main(float4 position : SV_POSITION) : SV_Target
{
    int2 mask_position;
    uint mask_count = 0;
    uint i;
    float2 world;
    float coverage;

    mask_position = int2(position.xy) - int2(coverage_world_x.w, coverage_world_y.w);
    if (coverage_msaa)
    {
        [loop]
        for (i = 0; i < coverage_sample_count; ++i)
            mask_count += coverage_msaa_mask.Load(mask_position, i) != 0;
    }
    else
    {
        mask_count = countbits(coverage_mask.Load(int3(mask_position, 0)));
    }
    coverage = (float)mask_count / coverage_sample_count;
    clip(coverage - 1.0f / (2.0f * coverage_sample_count));
    world.x = dot(float3(position.xy, 1.0f), coverage_world_x.xyz);
    world.y = dot(float3(position.xy, 1.0f), coverage_world_y.xyz);
    return sample_brush(world) * coverage;
}
