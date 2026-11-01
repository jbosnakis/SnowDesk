#pragma once
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <windows.h>
#include <wrl/client.h>
#include <vector>
#include "playground.h"
#include "world.h"

using Microsoft::WRL::ComPtr;

// One transparent, click-through, topmost surface per monitor.
struct Surface {
    HWND hwnd = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    int gridW = 0, gridH = 0;
    bool shown = false;

    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;

    ComPtr<ID3D11Texture2D> cellsTex, occluderTex;
    ComPtr<ID3D11ShaderResourceView> cellsSrv, occluderSrv;

    // The cells eased over time. Two copies, because each frame is computed from the previous one.
    ComPtr<ID3D11Texture2D> easedTex[2];
    ComPtr<ID3D11ShaderResourceView> easedSrv[2];
    ComPtr<ID3D11RenderTargetView> easedRtv[2];
    int easedIndex = 0;
    bool uploadAll = true;
};

struct DrawParams {
    float fade = 1;
    float time = 0;
    float dt = 0;
    bool quickEase = false;     // a window is being dragged: snow must keep up with it
    const std::vector<Shape>* shapes = nullptr;
    bool flakesInFront = false;
    bool flakesOnly = false;    // no lying snow to ease or draw
};

class Renderer {
public:
    bool init();
    void shutdown();
    bool createSurface(Surface& s, HINSTANCE inst, const wchar_t* cls);
    void destroySurface(Surface& s);
    // Returns false when the device was lost and everything must be recreated.
    bool draw(Surface& s, World& world, SnowCursor* cursor, const DrawParams& params);

private:
    void uploadCells(Surface& s, World& world, SnowCursor* cursor);
    void easeCells(Surface& s, const DrawParams& params);
    void setConstants(const Surface& s, float qx, float qy, float qw, float qh, float tx, float ty, const DrawParams& p, float rise, float fall, bool occlude);

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<ID3D11VertexShader> vsQuad, vsFlake, vsShape;
    ComPtr<ID3D11PixelShader> psEase, psSnow, psFlake, psShape;
    ComPtr<ID3D11Buffer> cb, instanceBuf, shapeBuf;
    ComPtr<ID3D11ShaderResourceView> instanceSrv, shapeSrv;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Texture2D> cursorTex;
    ComPtr<ID3D11ShaderResourceView> cursorSrv;
    std::vector<Instance> instances;
    std::vector<CellBox> dirtyBoxes;
};
