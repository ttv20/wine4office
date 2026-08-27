#define BRUSH_TYPE_SOLID  0
#define BRUSH_TYPE_LINEAR 1
#define BRUSH_TYPE_RADIAL 2
#define BRUSH_TYPE_BITMAP 3
#define BRUSH_TYPE_COUNT  4

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

/* Keep t0-t3 identical to shape_ps so the ordinary brush resource binding can
 * be reused for the final composite. Coverage resources occupy t4/t5. */
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Buffer<float4> b0 : register(t2);
Buffer<float4> b1 : register(t3);
Texture2D<uint> coverage_mask : register(t4);
Texture2DMS<uint> coverage_msaa_mask : register(t5);

float4 sample_gradient(Buffer<float4> gradient, uint stop_count, float position)
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

float4 brush_linear(struct brush brush, Buffer<float4> gradient, float2 position)
{
    float2 start, end, v_p, v_q;
    uint stop_count;
    float p;

    start = brush.data[0].xy;
    end = brush.data[0].zw;
    stop_count = asuint(brush.data[1].x);
    v_p = position - start;
    v_q = end - start;
    p = dot(v_q, v_p) / dot(v_q, v_q);
    return sample_gradient(gradient, stop_count, p);
}

float4 brush_radial(struct brush brush, Buffer<float4> gradient, float2 position)
{
    float2 centre, offset, ra, rb, v_p, v_q, r;
    float b, c, l, t;
    uint stop_count;

    centre = brush.data[0].xy;
    offset = brush.data[0].zw;
    ra = brush.data[1].xy;
    rb = brush.data[1].zw;
    stop_count = asuint(brush.data[2].x);
    r = float2(dot(ra, ra), dot(rb, rb));
    v_p = position - (centre + offset);
    v_p = float2(dot(v_p, ra), dot(v_p, rb)) / r;
    v_q = float2(dot(offset, ra), dot(offset, rb)) / r;
    l = length(v_p);
    b = dot(v_p, v_q) / l;
    c = dot(v_q, v_q) - 1.0f;
    t = -b + sqrt(b * b - c);
    return sample_gradient(gradient, stop_count, l / t);
}

float4 brush_bitmap(struct brush brush, Texture2D texture_resource,
        SamplerState sampler_resource, float2 position)
{
    float3 transform[2];
    bool ignore_alpha;
    float2 texcoord;
    float4 colour;

    transform[0] = brush.data[0].xyz;
    transform[1] = brush.data[1].xyz;
    ignore_alpha = asuint(brush.data[1].w);
    texcoord.x = dot(position.xy, transform[0].xy) + transform[0].z;
    texcoord.y = dot(position.xy, transform[1].xy) + transform[1].z;
    colour = texture_resource.Sample(sampler_resource, texcoord);
    if (ignore_alpha)
        colour.a = 1.0f;
    return colour;
}

float4 sample_brush(struct brush brush, Texture2D texture_resource,
        SamplerState sampler_resource, Buffer<float4> gradient, float2 position)
{
    if (brush.type == BRUSH_TYPE_SOLID)
        return brush.data[0] * brush.opacity;
    if (brush.type == BRUSH_TYPE_LINEAR)
        return brush_linear(brush, gradient, position) * brush.opacity;
    if (brush.type == BRUSH_TYPE_RADIAL)
        return brush_radial(brush, gradient, position) * brush.opacity;
    if (brush.type == BRUSH_TYPE_BITMAP)
        return brush_bitmap(brush, texture_resource, sampler_resource, position) * brush.opacity;
    return float4(0.0f, 0.0f, 0.0f, brush.opacity);
}

float4 main(float4 position : SV_POSITION) : SV_Target
{
    int2 mask_position;
    uint mask_count = 0;
    float4 colour;
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
    colour = sample_brush(colour_brush, t0, s0, b0, world);
    if (opacity_brush.type < BRUSH_TYPE_COUNT)
        colour *= sample_brush(opacity_brush, t1, s1, b1, world).a;
    return colour * coverage;
}
