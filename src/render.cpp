#include "render.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
const char kShaders[] = R"HLSL(
cbuffer Constants : register(b0) {
    float2 target;
    float2 texel;
    float2 quadPos;
    float2 quadSize;
    float fade;
    float time;
    float riseRate;
    float fallRate;
    float occlude;
    float3 padding;
};

struct Inst { float4 v; };

Texture2D cells : register(t0);             // PSEase: raw cells, PSSnow: eased cells
StructuredBuffer<Inst> inst : register(t1);
Texture2D occluder : register(t2);
Texture2D previous : register(t3);

struct ShapeData { float4 ends; float2 radii; uint color; float padding2; };
StructuredBuffer<ShapeData> shapes : register(t4);
SamplerState samp : register(s0);

struct QuadOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
struct FlakeOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float a : TEXCOORD1; };
struct ShapeOut {
    float4 pos : SV_Position;
    float2 pixel : TEXCOORD0;
    nointerpolation float4 ends : TEXCOORD1;
    nointerpolation float2 radii : TEXCOORD2;
    nointerpolation float4 color : TEXCOORD3;
};

static const float2 corners[6] = { {0,0},{1,0},{0,1},{0,1},{1,0},{1,1} };

QuadOut VSQuad(uint id : SV_VertexID) {
    QuadOut o;
    float2 c = corners[id];
    float2 p = (quadPos + c * quadSize) / target;
    o.pos = float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);
    o.uv = c;
    return o;
}

FlakeOut VSFlake(uint id : SV_VertexID, uint i : SV_InstanceID) {
    FlakeOut o;
    float4 v = inst[i].v;
    float2 c = corners[id];
    float2 p = (v.xy + (c - 0.5) * v.z * 2.0) / target;
    o.pos = float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);
    o.uv = c * 2 - 1;
    o.a = v.w;
    return o;
}

float visibleAt(float2 pixel) {
    return 1.0 - occlude * occluder.SampleLevel(samp, pixel / target, 0).r;
}

ShapeOut VSShape(uint id : SV_VertexID, uint i : SV_InstanceID) {
    ShapeData d = shapes[i];
    float reach = max(d.radii.x, d.radii.y) + 1.5;
    float2 low = min(d.ends.xy, d.ends.zw) - reach;
    float2 high = max(d.ends.xy, d.ends.zw) + reach;
    float2 pixel = low + corners[id] * (high - low);
    float2 p = pixel / target;

    ShapeOut o;
    o.pos = float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);
    o.pixel = pixel;
    o.ends = d.ends;
    o.radii = d.radii;
    o.color = float4(d.color & 255, (d.color >> 8) & 255, (d.color >> 16) & 255, d.color >> 24) / 255.0;
    return o;
}

// Distance to a capsule whose radius changes along its length, with a one-pixel soft edge.
float4 PSShape(ShapeOut i) : SV_Target {
    float2 pa = i.pixel - i.ends.xy;
    float2 ba = i.ends.zw - i.ends.xy;
    float len = dot(ba, ba);
    float h = len > 0.0001 ? saturate(dot(pa, ba) / len) : 0;
    float d = length(pa - ba * h) - lerp(i.radii.x, i.radii.y, h);
    float a = saturate(0.5 - d) * i.color.a * fade * visibleAt(i.pos.xy);
    if (a <= 0.002) discard;
    return float4(i.color.rgb * a, a);
}

// Each cell glides towards its current value, so snow swells and thins out instead of popping.
float PSEase(QuadOut i) : SV_Target {
    int3 p = int3(i.pos.xy, 0);
    float now = cells.Load(p).r;
    float was = previous.Load(p).r;
    return was + (now - was) * (now > was ? riseRate : fallRate);
}

float S(float2 uv) { return cells.SampleLevel(samp, uv, 0).r; }

float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }

float4 PSSnow(QuadOut i) : SV_Target {
    // Most of the screen is covered or empty; leave those pixels after as few samples as possible.
    float visible = visibleAt(i.pos.xy);
    if (visible <= 0.002) discard;
    float2 t = texel;
    float2 cross = float2(t.x, -t.y);
    float center = S(i.uv), diagonals = S(i.uv + t) + S(i.uv - t) + S(i.uv + cross) + S(i.uv - cross);
    if (center + diagonals < 0.01) discard;

    float a = center * 0.28 + diagonals * 0.05
        + (S(i.uv + float2(t.x, 0)) + S(i.uv - float2(t.x, 0)) + S(i.uv + float2(0, t.y)) + S(i.uv - float2(0, t.y))) * 0.13;
    float alpha = smoothstep(0.22, 0.50, a) * visible;
    if (alpha <= 0.002) discard;

    float dx = S(i.uv + float2(t.x * 2, 0)) - S(i.uv - float2(t.x * 2, 0));
    float dy = S(i.uv + float2(0, t.y * 2)) - S(i.uv - float2(0, t.y * 2));
    float3 n = normalize(float3(dx, dy, 0.55));
    float light = saturate(0.74 + 0.36 * dot(n, normalize(float3(-0.35, -0.8, 0.5))));

    float cover = S(i.uv - float2(0, t.y * 3)) * 0.5 + S(i.uv - float2(0, t.y * 6)) * 0.5;
    float3 shade = float3(0.66, 0.76, 0.92);
    float3 lit = float3(0.93, 0.95, 0.985);
    float3 col = lerp(shade, lit, saturate(light - cover * 0.20));

    float h = hash(floor(i.pos.xy / 2.0));
    if (h > 0.9965 && cover < 0.6) {
        float tw = 0.5 + 0.5 * sin(time * (1.0 + h * 3.0) + h * 40.0);
        col += 0.1 * tw * tw;
    }
    alpha *= fade;
    return float4(col * alpha, alpha);
}

float4 PSFlake(FlakeOut i) : SV_Target {
    float r = length(i.uv);
    float a = smoothstep(1.0, 0.25, r) * i.a * fade * visibleAt(i.pos.xy);
    if (a <= 0.002) discard;
    return float4(float3(0.93, 0.95, 0.99) * a, a);
}
)HLSL";

struct Constants {
    float target[2];
    float texel[2];
    float quadPos[2];
    float quadSize[2];
    float fade;
    float time;
    float riseRate;
    float fallRate;
    float occlude;
    float padding[3];
};

// Seconds for the eased snow to cover about two thirds of a change.
constexpr float kRiseSeconds = 0.20f;
constexpr float kFallSeconds = 0.07f;
constexpr float kQuickSeconds = 0.012f;
constexpr size_t kMaxShapes = 3000;

ComPtr<ID3DBlob> compile(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code, err;
    HRESULT hr = D3DCompile(kShaders, sizeof(kShaders) - 1, "snow", nullptr, nullptr, entry, profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    return SUCCEEDED(hr) ? code : nullptr;
}

D3D11_TEXTURE2D_DESC textureDesc(UINT w, UINT h, DXGI_FORMAT format, UINT bind) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = bind;
    return d;
}

float rateFor(float dt, float seconds) { return 1 - std::exp(-dt / seconds); }
}

bool Renderer::init() {
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgi;
    dev.As(&dxgi);
    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(&adapter);
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
    if (FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&dcomp)))) return false;

    auto vq = compile("VSQuad", "vs_5_0"), vf = compile("VSFlake", "vs_5_0");
    auto pe = compile("PSEase", "ps_5_0"), ps = compile("PSSnow", "ps_5_0"), pf = compile("PSFlake", "ps_5_0");
    auto vs = compile("VSShape", "vs_5_0"), pa = compile("PSShape", "ps_5_0");
    if (!vq || !vf || !pe || !ps || !pf || !vs || !pa) return false;
    dev->CreateVertexShader(vq->GetBufferPointer(), vq->GetBufferSize(), nullptr, &vsQuad);
    dev->CreateVertexShader(vf->GetBufferPointer(), vf->GetBufferSize(), nullptr, &vsFlake);
    dev->CreatePixelShader(pe->GetBufferPointer(), pe->GetBufferSize(), nullptr, &psEase);
    dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &psSnow);
    dev->CreatePixelShader(pf->GetBufferPointer(), pf->GetBufferSize(), nullptr, &psFlake);
    dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vsShape);
    dev->CreatePixelShader(pa->GetBufferPointer(), pa->GetBufferSize(), nullptr, &psShape);

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Constants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb))) return false;

    instances.resize(MAX_PARTICLES);
    D3D11_BUFFER_DESC ib{};
    ib.ByteWidth = UINT(sizeof(Instance) * instances.size());
    ib.Usage = D3D11_USAGE_DYNAMIC;
    ib.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ib.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ib.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    ib.StructureByteStride = sizeof(Instance);
    if (FAILED(dev->CreateBuffer(&ib, nullptr, &instanceBuf))) return false;
    if (FAILED(dev->CreateShaderResourceView(instanceBuf.Get(), nullptr, &instanceSrv))) return false;

    D3D11_BUFFER_DESC sb = ib;
    sb.ByteWidth = UINT(sizeof(Shape) * kMaxShapes);
    sb.StructureByteStride = sizeof(Shape);
    if (FAILED(dev->CreateBuffer(&sb, nullptr, &shapeBuf))) return false;
    if (FAILED(dev->CreateShaderResourceView(shapeBuf.Get(), nullptr, &shapeSrv))) return false;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &sampler);

    D3D11_BLEND_DESC bl{};
    auto& rt = bl.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bl, &blend);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    dev->CreateRasterizerState(&rd, &raster);

    auto td = textureDesc(SnowCursor::S, SnowCursor::S, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &cursorTex))) return false;
    dev->CreateShaderResourceView(cursorTex.Get(), nullptr, &cursorSrv);
    return true;
}

void Renderer::shutdown() {
    shapeSrv.Reset(); shapeBuf.Reset(); vsShape.Reset(); psShape.Reset();
    instanceSrv.Reset(); instanceBuf.Reset(); cb.Reset(); sampler.Reset(); blend.Reset(); raster.Reset();
    cursorSrv.Reset(); cursorTex.Reset(); vsQuad.Reset(); vsFlake.Reset();
    psEase.Reset(); psSnow.Reset(); psFlake.Reset();
    dcomp.Reset(); factory.Reset(); ctx.Reset(); dev.Reset();
}

bool Renderer::createSurface(Surface& s, HINSTANCE inst, const wchar_t* cls) {
    DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    s.hwnd = CreateWindowExW(ex, cls, L"SnowDesk overlay", WS_POPUP, s.x, s.y, s.w, s.h, nullptr, nullptr, inst, nullptr);
    if (!s.hwnd) return false;
    SetLayeredWindowAttributes(s.hwnd, 0, 255, LWA_ALPHA);

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = s.w;
    d.Height = s.h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory->CreateSwapChainForComposition(dev.Get(), &d, nullptr, &s.swap))) return false;

    ComPtr<ID3D11Texture2D> back;
    s.swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(dev->CreateRenderTargetView(back.Get(), nullptr, &s.rtv))) return false;

    if (FAILED(dcomp->CreateTargetForHwnd(s.hwnd, TRUE, &s.target))) return false;
    if (FAILED(dcomp->CreateVisual(&s.visual))) return false;
    s.visual->SetContent(s.swap.Get());
    s.target->SetRoot(s.visual.Get());
    dcomp->Commit();

    s.gridW = s.w / CELL;
    s.gridH = s.h / CELL;
    auto raw = textureDesc(s.gridW, s.gridH, DXGI_FORMAT_R8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    if (FAILED(dev->CreateTexture2D(&raw, nullptr, &s.cellsTex))) return false;
    if (FAILED(dev->CreateShaderResourceView(s.cellsTex.Get(), nullptr, &s.cellsSrv))) return false;
    if (FAILED(dev->CreateTexture2D(&raw, nullptr, &s.occluderTex))) return false;
    if (FAILED(dev->CreateShaderResourceView(s.occluderTex.Get(), nullptr, &s.occluderSrv))) return false;

    // 16 bits: with 8 the slow end of the easing would round to no movement at all.
    auto eased = textureDesc(s.gridW, s.gridH, DXGI_FORMAT_R16_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    const float clear[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 2; i++) {
        if (FAILED(dev->CreateTexture2D(&eased, nullptr, &s.easedTex[i]))) return false;
        if (FAILED(dev->CreateShaderResourceView(s.easedTex[i].Get(), nullptr, &s.easedSrv[i]))) return false;
        if (FAILED(dev->CreateRenderTargetView(s.easedTex[i].Get(), nullptr, &s.easedRtv[i]))) return false;
        ctx->ClearRenderTargetView(s.easedRtv[i].Get(), clear);
    }
    s.uploadAll = true;
    return true;
}

void Renderer::destroySurface(Surface& s) {
    for (int i = 0; i < 2; i++) {
        s.easedRtv[i].Reset();
        s.easedSrv[i].Reset();
        s.easedTex[i].Reset();
    }
    s.occluderSrv.Reset(); s.occluderTex.Reset(); s.cellsSrv.Reset(); s.cellsTex.Reset();
    s.visual.Reset(); s.target.Reset(); s.rtv.Reset(); s.swap.Reset();
    if (ctx) {
        ctx->ClearState();
        ctx->Flush();
    }
    if (s.hwnd) DestroyWindow(s.hwnd);
    s.hwnd = nullptr;
    s.shown = false;
}

void Renderer::setConstants(const Surface& s, float qx, float qy, float qw, float qh, float tx, float ty, const DrawParams& p, float rise, float fall, bool occlude) {
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    Constants c{};
    c.target[0] = qw > 0 && rise > 0 ? float(s.gridW) : float(s.w);
    c.target[1] = qw > 0 && rise > 0 ? float(s.gridH) : float(s.h);
    c.texel[0] = tx;
    c.texel[1] = ty;
    c.quadPos[0] = qx;
    c.quadPos[1] = qy;
    c.quadSize[0] = qw;
    c.quadSize[1] = qh;
    c.fade = p.fade;
    c.time = p.time;
    c.riseRate = rise;
    c.fallRate = fall;
    c.occlude = occlude ? 1.0f : 0.0f;
    memcpy(m.pData, &c, sizeof(c));
    ctx->Unmap(cb.Get(), 0);
}

void Renderer::uploadCells(Surface& s, World& world, SnowCursor* cursor) {
    world.takeDirty(dirtyBoxes);
    if (s.uploadAll) dirtyBoxes.assign(1, { 0, 0, s.gridW, s.gridH });
    for (const CellBox& b : dirtyBoxes) {
        D3D11_BOX box{ UINT(b.l), UINT(b.t), 0, UINT(b.r), UINT(b.b), 1 };
        ctx->UpdateSubresource(s.cellsTex.Get(), 0, &box, world.visibleCells() + size_t(b.t) * s.gridW + b.l, s.gridW, 0);
    }
    if (world.consumeOccluderDirty() || s.uploadAll) {
        ctx->UpdateSubresource(s.occluderTex.Get(), 0, nullptr, world.occluderCells(), s.gridW, 0);
    }
    s.uploadAll = false;
    if (cursor && cursor->dirty) {
        ctx->UpdateSubresource(cursorTex.Get(), 0, nullptr, cursor->shown.data(), SnowCursor::S, 0);
        cursor->dirty = false;
    }
}

void Renderer::easeCells(Surface& s, const DrawParams& p) {
    int prev = s.easedIndex, cur = 1 - prev;
    float rise = rateFor(p.dt, p.quickEase ? kQuickSeconds : kRiseSeconds);
    float fall = rateFor(p.dt, p.quickEase ? kQuickSeconds : kFallSeconds);

    ID3D11RenderTargetView* rtv = s.easedRtv[cur].Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{ 0, 0, float(s.gridW), float(s.gridH), 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    setConstants(s, 0, 0, float(s.gridW), float(s.gridH), 0, 0, p, rise, fall, false);

    ID3D11ShaderResourceView* now = s.cellsSrv.Get();
    ID3D11ShaderResourceView* was = s.easedSrv[prev].Get();
    ctx->PSSetShaderResources(0, 1, &now);
    ctx->PSSetShaderResources(3, 1, &was);
    ctx->VSSetShader(vsQuad.Get(), nullptr, 0);
    ctx->PSSetShader(psEase.Get(), nullptr, 0);
    ctx->Draw(6, 0);

    ID3D11ShaderResourceView* none[1] = { nullptr };
    ctx->PSSetShaderResources(0, 1, none);
    ctx->PSSetShaderResources(3, 1, none);
    s.easedIndex = cur;
}

bool Renderer::draw(Surface& s, World& world, SnowCursor* cursor, const DrawParams& p) {
    uploadCells(s, world, cursor);

    int count = world.buildInstances(instances.data(), int(instances.size()));
    D3D11_MAPPED_SUBRESOURCE map{};
    if (count > 0 && SUCCEEDED(ctx->Map(instanceBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, instances.data(), sizeof(Instance) * count);
        ctx->Unmap(instanceBuf.Get(), 0);
    }

    ctx->RSSetState(raster.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ID3D11SamplerState* smp = sampler.Get();
    ctx->PSSetSamplers(0, 1, &smp);
    ID3D11Buffer* cbs = cb.Get();
    ctx->VSSetConstantBuffers(0, 1, &cbs);
    ctx->PSSetConstantBuffers(0, 1, &cbs);

    if (!p.flakesOnly) easeCells(s, p);

    const float clear[4] = { 0, 0, 0, 0 };
    ID3D11RenderTargetView* rtv = s.rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->ClearRenderTargetView(rtv, clear);
    D3D11_VIEWPORT vp{ 0, 0, float(s.w), float(s.h), 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(blend.Get(), nullptr, 0xFFFFFFFF);

    ID3D11ShaderResourceView* occ = s.occluderSrv.Get();
    ctx->PSSetShaderResources(2, 1, &occ);

    ctx->VSSetShader(vsQuad.Get(), nullptr, 0);
    ctx->PSSetShader(psSnow.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = s.easedSrv[s.easedIndex].Get();
    ctx->PSSetShaderResources(0, 1, &srv);
    setConstants(s, 0, 0, float(s.w), float(s.h), 1.0f / s.gridW, 1.0f / s.gridH, p, 0, 0, true);
    if (!p.flakesOnly) ctx->Draw(6, 0);

    if (p.shapes && !p.shapes->empty()) {
        size_t shapes = std::min(p.shapes->size(), kMaxShapes);
        if (SUCCEEDED(ctx->Map(shapeBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            memcpy(map.pData, p.shapes->data(), sizeof(Shape) * shapes);
            ctx->Unmap(shapeBuf.Get(), 0);
            setConstants(s, 0, 0, float(s.w), float(s.h), 0, 0, p, 0, 0, true);
            ctx->VSSetShader(vsShape.Get(), nullptr, 0);
            ctx->PSSetShader(psShape.Get(), nullptr, 0);
            ID3D11ShaderResourceView* ss = shapeSrv.Get();
            ctx->VSSetShaderResources(4, 1, &ss);
            ctx->DrawInstanced(6, UINT(shapes), 0, 0);
            ID3D11ShaderResourceView* none = nullptr;
            ctx->VSSetShaderResources(4, 1, &none);
        }
    }

    if (count > 0) {
        setConstants(s, 0, 0, float(s.w), float(s.h), 0, 0, p, 0, 0, !p.flakesInFront);
        ctx->VSSetShader(vsFlake.Get(), nullptr, 0);
        ctx->PSSetShader(psFlake.Get(), nullptr, 0);
        ID3D11ShaderResourceView* is = instanceSrv.Get();
        ctx->VSSetShaderResources(1, 1, &is);
        ctx->DrawInstanced(6, count, 0, 0);
        ID3D11ShaderResourceView* none = nullptr;
        ctx->VSSetShaderResources(1, 1, &none);
    }

    // The pointer sits above every window, so its snow is the one thing that is never hidden.
    if (cursor && cursor->visible && cursor->anythingShown()) {
        srv = cursorSrv.Get();
        ctx->PSSetShaderResources(0, 1, &srv);
        float texel = 0.6f / SnowCursor::S;
        setConstants(s, cursor->originX(), cursor->originY(), float(SnowCursor::S), float(SnowCursor::S), texel, texel, p, 0, 0, false);
        ctx->VSSetShader(vsQuad.Get(), nullptr, 0);
        ctx->PSSetShader(psSnow.Get(), nullptr, 0);
        ctx->Draw(6, 0);
    }

    ID3D11ShaderResourceView* none[3] = { nullptr, nullptr, nullptr };
    ctx->PSSetShaderResources(0, 3, none);

    HRESULT hr = s.swap->Present(1, 0);
    return !(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET);
}
