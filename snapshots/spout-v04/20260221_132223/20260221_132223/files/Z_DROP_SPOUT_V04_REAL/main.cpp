#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "SpoutDX.h"
#include "json.hpp"

#include <openvino/openvino.hpp>

using Microsoft::WRL::ComPtr;
using json = nlohmann::json;

static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static LONG WINAPI SehFilter(EXCEPTION_POINTERS* ep)
{
  std::cerr << "\n[FATAL] SEH exception code=0x" << std::hex
            << (unsigned long)ep->ExceptionRecord->ExceptionCode << std::dec << "\n";
  return EXCEPTION_EXECUTE_HANDLER;
}

static std::string ExeDir()
{
  char path[MAX_PATH]{0};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::filesystem::path p(path);
  return p.parent_path().string();
}

struct Config
{
  std::string inputSenderName = "PHASE2_TD_OUT_V04";
  std::string senderName = "PHASE2_TD_MASK_V04";
  int outW = 1280;
  int outH = 720;

  std::string modelPath;
  std::string device = "AUTO"; // <- IMPORTANT (AUTO/GPU/CPU)
  bool debugDiag = true;
};

static std::optional<Config> LoadConfig(const std::string& cfgPath)
{
  Config c;
  try {
    std::ifstream f(cfgPath);
    if (!f.is_open()) return std::nullopt;
    json j;
    f >> j;

    if (j.contains("inputSenderName")) c.inputSenderName = j["inputSenderName"].get<std::string>();
    if (j.contains("senderName")) c.senderName = j["senderName"].get<std::string>();
    if (j.contains("size") && j["size"].is_array() && j["size"].size() == 2) {
      c.outW = j["size"][0].get<int>();
      c.outH = j["size"][1].get<int>();
    }
    if (j.contains("modelPath")) c.modelPath = j["modelPath"].get<std::string>();
    if (j.contains("device")) c.device = j["device"].get<std::string>();
    if (j.contains("debugDiag")) c.debugDiag = j["debugDiag"].get<bool>();

    return c;
  } catch (...) {
    return std::nullopt;
  }
}

static Config LoadConfigOrDefaults()
{
  const std::string cfgPath = ExeDir() + "\\sender_config.json";
  auto cfg = LoadConfig(cfgPath);
  if (!cfg) {
    std::cout << "Config not found, using defaults: " << cfgPath << "\n";
    Config c;
    return c;
  }
  std::cout << "Loaded config: " << cfgPath << "\n";
  return *cfg;
}

static void PrintConfig(const Config& c)
{
  std::cout
    << "Config: inputSenderName=" << c.inputSenderName
    << " senderName=" << c.senderName
    << " size=" << c.outW << "x" << c.outH
    << " modelPath=" << c.modelPath
    << " device=" << c.device
    << " debugDiag=" << (c.debugDiag ? "true" : "false")
    << "\n";
}

// Simple argmax over C classes at each pixel (expects output NHWC float)
static void ArgMaxToLabels(const float* probsNHWC, int H, int W, int C, std::vector<uint8_t>& labelsHW)
{
  labelsHW.resize(H * W);
  for (int i = 0; i < H * W; i++) {
    const float* p = probsNHWC + i * C;
    int best = 0;
    float bestv = p[0];
    for (int c = 1; c < C; c++) {
      if (p[c] > bestv) { bestv = p[c]; best = c; }
    }
    labelsHW[i] = (uint8_t)best;
  }
}

static void ResizeNearestBGRAtoRGB8(
  const uint8_t* srcBGRA, int srcW, int srcH, int srcStrideBytes,
  uint8_t* dstRGB, int dstW, int dstH)
{
  for (int y = 0; y < dstH; y++) {
    int sy = (int)((int64_t)y * srcH / dstH);
    const uint8_t* srcRow = srcBGRA + sy * srcStrideBytes;
    uint8_t* dstRow = dstRGB + y * dstW * 3;
    for (int x = 0; x < dstW; x++) {
      int sx = (int)((int64_t)x * srcW / dstW);
      const uint8_t* p = srcRow + sx * 4; // BGRA
      dstRow[x * 3 + 0] = p[2]; // R
      dstRow[x * 3 + 1] = p[1]; // G
      dstRow[x * 3 + 2] = p[0]; // B
    }
  }
}

struct PadInfo { int padBottom = 0; int padRight = 0; };

static void PrepareInput256(
  const uint8_t* srcBGRA, int srcW, int srcH, int srcStrideBytes,
  std::vector<float>& outNHWC, PadInfo& pad)
{
  constexpr int T = 256;
  int rW = T, rH = T;

  if (srcH < srcW) {
    rW = T;
    rH = (int)std::floor((double)srcH / ((double)srcW / T));
  } else {
    rH = T;
    rW = (int)std::floor((double)srcW / ((double)srcH / T));
  }
  if (rW < 1) rW = 1;
  if (rH < 1) rH = 1;

  pad.padRight  = T - rW;
  pad.padBottom = T - rH;

  std::vector<uint8_t> resizedRGB(rW * rH * 3);
  ResizeNearestBGRAtoRGB8(srcBGRA, srcW, srcH, srcStrideBytes, resizedRGB.data(), rW, rH);

  outNHWC.assign(1 * T * T * 3, 0.0f);
  for (int y = 0; y < rH; y++) {
    for (int x = 0; x < rW; x++) {
      int srcIdx = (y * rW + x) * 3;
      int dstIdx = (y * T + x) * 3;
      outNHWC[dstIdx + 0] = resizedRGB[srcIdx + 0] / 255.0f;
      outNHWC[dstIdx + 1] = resizedRGB[srcIdx + 1] / 255.0f;
      outNHWC[dstIdx + 2] = resizedRGB[srcIdx + 2] / 255.0f;
    }
  }
}

static void ResizeLabelsToOut(
  const std::vector<uint8_t>& labels256, const PadInfo& pad,
  int outW, int outH, std::vector<uint8_t>& outLabels)
{
  constexpr int T = 256;
  int unpadW = T - pad.padRight;
  int unpadH = T - pad.padBottom;
  if (unpadW < 1) unpadW = 1;
  if (unpadH < 1) unpadH = 1;

  outLabels.resize(outW * outH);
  for (int y = 0; y < outH; y++) {
    int sy = (int)((int64_t)y * unpadH / outH);
    for (int x = 0; x < outW; x++) {
      int sx = (int)((int64_t)x * unpadW / outW);
      outLabels[y * outW + x] = labels256[sy * T + sx];
    }
  }
}

struct RGB { uint8_t r, g, b; };
static RGB ClassColor(int cls)
{
  switch (cls) {
    case 0: return {0, 0, 0};
    case 1: return {255, 0, 255};
    case 2: return {255, 170, 0};
    case 3: return {255, 220, 0};
    case 4: return {0, 255, 0};
    case 5: return {0, 255, 255};
    default: return {255, 255, 255};
  }
}

static void LabelsToBGRA(const std::vector<uint8_t>& labels, int W, int H, std::vector<uint8_t>& outBGRA)
{
  outBGRA.resize(W * H * 4);
  for (int i = 0; i < W * H; i++) {
    int cls = labels[i];
    RGB c = ClassColor(cls);
    outBGRA[i * 4 + 0] = c.b;
    outBGRA[i * 4 + 1] = c.g;
    outBGRA[i * 4 + 2] = c.r;
    outBGRA[i * 4 + 3] = (cls == 0) ? 0 : 255;
  }
}

int main()
{
  SetUnhandledExceptionFilter(SehFilter);

  try {
    std::cout << "### BUILD MARKER: SPOUTMASKSENDER_DX11_V04_REAL_SEGMENTATION ###\n";

    Config cfg = LoadConfigOrDefaults();
    PrintConfig(cfg);

    if (cfg.modelPath.empty() || !std::filesystem::exists(cfg.modelPath)) {
      std::cerr << "ERROR: modelPath missing or file not found: " << cfg.modelPath << "\n";
      return 2;
    }

    std::cout << "STEP 1: OpenVINO init (IR)\n";
    std::filesystem::create_directories("C:\\Temp\\ov_cache");

    ov::Core core;
    core.set_property(ov::cache_dir("C:\\Temp\\ov_cache"));

    auto model = core.read_model(cfg.modelPath);
    std::cout << "read_model OK\n";

    // IMPORTANT: use device from config (AUTO/GPU/CPU)
    const std::string dev = cfg.device.empty() ? "AUTO" : cfg.device;
    std::cout << "Compiling device: " << dev << "\n";
    auto compiled = core.compile_model(model, dev);

    ov::InferRequest infer = compiled.create_infer_request();
    std::cout << "STEP 1 done\n";

    std::cout << "STEP 2: D3D11 device\n";
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION, &d3d, &fl, &ctx);
    if (FAILED(hr)) {
      std::cerr << "D3D11CreateDevice failed hr=0x" << std::hex << (uint32_t)hr << std::dec << "\n";
      return 3;
    }

    std::cout << "STEP 3: SpoutDX OpenDirectX11\n";
    spoutDX spout;
    if (!spout.OpenDirectX11(d3d.Get())) {
      std::cerr << "Spout OpenDirectX11 failed\n";
      return 4;
    }

    std::cout << "STEP 4: Spout sender setup\n";
    spout.SetSenderName(cfg.senderName.c_str());
    spout.SetSenderFormat(DXGI_FORMAT_B8G8R8A8_UNORM);

    std::cout << "STEP 5: Spout receiver setup\n";
    spout.SetReceiverName(cfg.inputSenderName.c_str());

    std::cout << "STEP 6: Create output texture\n";
    ComPtr<ID3D11Texture2D> outTex;
    {
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = cfg.outW;
      desc.Height = cfg.outH;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DYNAMIC;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      hr = d3d->CreateTexture2D(&desc, nullptr, &outTex);
      if (FAILED(hr)) {
        std::cerr << "CreateTexture2D outTex failed hr=0x" << std::hex << (uint32_t)hr << std::dec << "\n";
        return 6;
      }
    }

    ComPtr<ID3D11Texture2D> stagingTex;
    unsigned int stagingW = 0, stagingH = 0;

    auto ensureStaging = [&](unsigned int w, unsigned int h) -> bool {
      if (stagingTex && stagingW == w && stagingH == h) return true;
      stagingTex.Reset();

      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = w;
      desc.Height = h;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_STAGING;
      desc.BindFlags = 0;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

      HRESULT r = d3d->CreateTexture2D(&desc, nullptr, &stagingTex);
      if (FAILED(r)) {
        std::cerr << "CreateTexture2D stagingTex failed hr=0x" << std::hex << (uint32_t)r << std::dec << "\n";
        return false;
      }
      stagingW = w; stagingH = h;
      return true;
    };

    std::vector<float> in256;
    PadInfo pad{};
    std::vector<uint8_t> labels256, labelsOut, outBGRA;

    ID3D11Texture2D* recvTexRaw = nullptr;

    std::cout << "READY. Waiting for frames from: " << cfg.inputSenderName << "\n";
    auto lastDiag = std::chrono::steady_clock::now();

    while (true) {
      bool got = spout.ReceiveTexture(&recvTexRaw);

      if (cfg.debugDiag) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastDiag > std::chrono::seconds(1)) {
          lastDiag = now;
          std::cout
            << "[diag] got=" << got
            << " recvTex=" << (void*)recvTexRaw
            << " connected=" << spout.IsConnected()
            << " updated=" << spout.IsUpdated()
            << " frameNew=" << spout.IsFrameNew()
            << " sender='" << (spout.GetSenderName() ? spout.GetSenderName() : "(null)") << "'"
            << " w=" << spout.GetSenderWidth()
            << " h=" << spout.GetSenderHeight()
            << " fmt=" << (int)spout.GetSenderFormat()
            << "\n";
        }
      }

      if (!got || !recvTexRaw) {
        SleepMs(5);
        continue;
      }

      unsigned int inW = spout.GetSenderWidth();
      unsigned int inH = spout.GetSenderHeight();
      if (inW == 0 || inH == 0) {
        SleepMs(1);
        continue;
      }

      if (!ensureStaging(inW, inH)) {
        SleepMs(1);
        continue;
      }

      ctx->CopyResource(stagingTex.Get(), recvTexRaw);

      D3D11_MAPPED_SUBRESOURCE mapped{};
      hr = ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
      if (FAILED(hr) || !mapped.pData) {
        SleepMs(1);
        continue;
      }

      const uint8_t* bgra = reinterpret_cast<const uint8_t*>(mapped.pData);
      int stride = (int)mapped.RowPitch;

      PrepareInput256(bgra, (int)inW, (int)inH, stride, in256, pad);
      ctx->Unmap(stagingTex.Get(), 0);

      ov::Tensor inputTensor(ov::element::f32, {1, 256, 256, 3}, in256.data());
      infer.set_input_tensor(inputTensor);
      infer.infer();
      ov::Tensor outTensor = infer.get_output_tensor();

      const float* probs = outTensor.data<const float>();
      ArgMaxToLabels(probs, 256, 256, 6, labels256);

      ResizeLabelsToOut(labels256, pad, cfg.outW, cfg.outH, labelsOut);
      LabelsToBGRA(labelsOut, cfg.outW, cfg.outH, outBGRA);

      D3D11_MAPPED_SUBRESOURCE outMap{};
      hr = ctx->Map(outTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &outMap);
      if (SUCCEEDED(hr) && outMap.pData) {
        uint8_t* dst = reinterpret_cast<uint8_t*>(outMap.pData);
        int dstStride = (int)outMap.RowPitch;
        for (int y = 0; y < cfg.outH; y++) {
          memcpy(dst + y * dstStride, outBGRA.data() + y * cfg.outW * 4, cfg.outW * 4);
        }
        ctx->Unmap(outTex.Get(), 0);
      }

      spout.SendTexture(outTex.Get());
      SleepMs(1);
    }

  } catch (const std::exception& e) {
    std::cerr << "[FATAL] std::exception: " << e.what() << "\n";
    return 100;
  } catch (...) {
    std::cerr << "[FATAL] unknown exception\n";
    return 101;
  }
}