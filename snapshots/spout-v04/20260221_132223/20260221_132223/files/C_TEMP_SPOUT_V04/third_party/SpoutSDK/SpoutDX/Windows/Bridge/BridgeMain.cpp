#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <cstdint>
#include <vector>
#include <thread>
#include <chrono>

#include "SpoutSender.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static bool CreateDevice(ID3D11Device** outDev, ID3D11DeviceContext** outCtx) {
  *outDev = nullptr;
  *outCtx = nullptr;
  D3D_FEATURE_LEVEL fl;
  return SUCCEEDED(D3D11CreateDevice(
    nullptr,
    D3D_DRIVER_TYPE_HARDWARE,
    nullptr,
    0,
    nullptr, 0,
    D3D11_SDK_VERSION,
    outDev,
    &fl,
    outCtx));
}

int main() {
  const int W = 1280;
  const int H = 720;
  const char* kOutSenderName = "SEG_MASK_TO_TD_SPOUTSENDER";

  std::printf("### BUILD MARKER: SPOUTSENDER_NO_INPUT_V2 ###\n");
  fflush(stdout);

  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  if (!CreateDevice(&dev, &ctx)) {
    std::printf("Failed to create D3D11 device\n");
    return 1;
  }

  SpoutSender sender;

  if (!sender.CreateSender(kOutSenderName, W, H)) {
    std::printf("CreateSender failed for '%s'\n", kOutSenderName);
    return 1;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = W;
  desc.Height = H;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

  ID3D11Texture2D* tex = nullptr;
  if (FAILED(dev->CreateTexture2D(&desc, nullptr, &tex)) || !tex) {
    std::printf("Failed to create texture\n");
    return 1;
  }

  std::vector<uint8_t> bgra((size_t)W * (size_t)H * 4);
  for (size_t i = 0; i < bgra.size(); i += 4) {
    bgra[i + 0] = 255; // B
    bgra[i + 1] = 0;   // G
    bgra[i + 2] = 255; // R
    bgra[i + 3] = 255; // A
  }

  while (true) {
    D3D11_BOX box = {};
    box.left = 0; box.top = 0; box.front = 0;
    box.right = (UINT)W; box.bottom = (UINT)H; box.back = 1;

    ctx->UpdateSubresource(tex, 0, &box, bgra.data(), (UINT)(W * 4), 0);

    // NOTE: adjust this line if the header expects a different signature
    sender.SendTexture(tex, dev);

    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}