float3x2 transform_geometry;
float stroke_width;
float4 stroke_aa;
float4 transform_rtx;
float4 transform_rty;

struct output
{
    nointerpolation float2 p0 : TRIANGLE0;
    nointerpolation float2 p1 : TRIANGLE1;
    nointerpolation float2 p2 : TRIANGLE2;
    nointerpolation float4 curve0 : CURVE0;
    nointerpolation float4 curve1 : CURVE1;
    nointerpolation float4 curve2 : CURVE2;
    float3 clip_distance : SV_ClipDistance;
    float4 position : SV_POSITION;
};

float2 transform_to_pixel(float2 position, float2 prev, float2 next)
{
    float2 world = mul(float3(position, 1.0f), transform_geometry);

    if (stroke_width > 0.0f)
    {
        float2x2 geom = float2x2(transform_geometry._11_21,
                transform_geometry._12_22);
        float2 q_prev = normalize(mul(geom, prev));
        float2 q_next = normalize(mul(geom, next));
        float2 normal = float2(-q_prev.y, q_prev.x);
        float denominator = 1.0f + dot(q_prev, q_next);
        float tangent = -dot(normal, q_next) / denominator;

        world += stroke_width * 0.5f * (tangent * q_prev + normal);
    }
    return float2(dot(float3(world, 1.0f), transform_rtx.xyz),
            dot(float3(world, 1.0f), transform_rty.xyz));
}

float edge_value(float2 a, float2 b, float2 p)
{
    float2 edge = b - a;
    float2 offset = p - a;

    return edge.x * offset.y - edge.y * offset.x;
}

float3 expanded_edge(float2 a, float2 b)
{
    float2 edge = b - a;
    float radius = 0.501f * (abs(edge.x) + abs(edge.y));

    return float3(-edge.y, edge.x,
            edge.y * a.x - edge.x * a.y + radius);
}

float edge_distance(float3 edge, float2 position)
{
    return dot(edge.xy, position) + edge.z;
}

void main(float2 position0 : POSITION0, float2 prev0 : PREV0, float2 next0 : NEXT0,
        float4 curve0 : CURVE0,
        float2 position1 : POSITION1, float2 prev1 : PREV1, float2 next1 : NEXT1,
        float4 curve1 : CURVE1,
        float2 position2 : POSITION2, float2 prev2 : PREV2, float2 next2 : NEXT2,
        float4 curve2 : CURVE2,
        uint vertex_id : SV_VertexID,
        out struct output o)
{
    float2 extent, lower, upper, corner, q0, q1, q2, swap_position;
    float3 edge0, edge1, edge2;
    float area;

    o.p0 = transform_to_pixel(position0, prev0, next0);
    o.p1 = transform_to_pixel(position1, prev1, next1);
    o.p2 = transform_to_pixel(position2, prev2, next2);
    o.curve0 = curve0;
    o.curve1 = curve1;
    o.curve2 = curve2;

    extent = float2(2.0f / transform_rtx.w, -2.0f / transform_rty.w);
    lower = clamp(floor(min(o.p0, min(o.p1, o.p2))), 0.0f, extent);
    upper = clamp(ceil(max(o.p0, max(o.p1, o.p2))), 0.0f, extent);

    q0 = o.p0;
    q1 = o.p1;
    q2 = o.p2;
    area = edge_value(q0, q1, q2);
    if (area < 0.0f)
    {
        swap_position = q1;
        q1 = q2;
        q2 = swap_position;
        area = -area;
    }

    if (vertex_id == 0)
        corner = lower;
    else if (vertex_id == 1)
        corner = float2(upper.x, lower.y);
    else if (vertex_id == 2)
        corner = float2(lower.x, upper.y);
    else
        corner = upper;

    if (area > 1.0e-8f)
    {
        edge0 = expanded_edge(q0, q1);
        edge1 = expanded_edge(q1, q2);
        edge2 = expanded_edge(q2, q0);
        o.clip_distance = float3(edge_distance(edge0, corner),
                edge_distance(edge1, corner), edge_distance(edge2, corner));
    }
    else
        o.clip_distance = 0.0f;

    o.position = float4(corner.x * transform_rtx.w - 1.0f,
            corner.y * transform_rty.w + 1.0f, 0.0f, 1.0f);
}
