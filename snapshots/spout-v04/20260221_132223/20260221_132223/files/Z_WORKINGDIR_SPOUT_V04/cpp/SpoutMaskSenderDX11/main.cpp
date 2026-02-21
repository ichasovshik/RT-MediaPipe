// SpoutMaskSenderDX11 - V04 REAL segmentation
// Spout-in (texture) -> CPU -> OpenVINO (TFLite selfie_multiclass_256x256) -> palette -> Spout-out
//
// Requires:
// - OpenVINO runtime (setupvars.ps1 before running/building, or OpenVINO_DIR set for CMake)
// - Spout SDK already vendored in third_party/SpoutSDK
//
// Model: selfie_multiclass_256x256.tflite
// Input: float32 [1,256,256,3] RGB in range 0..1
// Output: float32 [1,256,256,6] probabilities for classes: bg,hair,body,face,clothes,others

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
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

  std::string modelPath;     // required
  std::string device = "CPU"; // "CPU" or "GPU" if available
  std::string outputMode = "segColor"; // segColor only for now
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
    if (j.contains("outputMode")) c.outputMode = j["outputMode"].get<std::string>();

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
    c.modelPath = ""; // force error later
    return c;
  }
  std::cout << "Loaded config: " << cfgPath << "\n";
  return *cfg;
}

// Palette (RGB) for 6 classes.
// You can swap these to match torinmb exactly if needed.
// We'll use intuitive colors close to your screenshot:
// 0 background: black (alpha 0)
// 1 hair: magenta-ish
// 2 body skin: orange-ish
// 3 face skin: yellow
// 4 clothes: green
// 5 others/accessories: cyan
struct RGB { uint8_t r,g,b; };

static RGB ClassColor(int cls)
{
  switch (cls) {
    case 0: return {0,0,0};
    case 1: return {255,0,255};     // hair
    case 2: return {255,170,0};     // body skin
    case 3: return {255,220,0};     // face skin
    case 4: return {0,255,0};       // clothes
    case 5: return {0,255,255};     // others/accessories
    default: return {255,255,255};
  }
}

// Resize BGRA input -> RGB float32 0..1 with "resize + pad" to 256x256 (like notebook).
// Implemented without OpenCV (keeps dependencies minimal).
struct PadInfo { int padBottom = 0; int padRight = 0; int resizedH = 0; int resizedW = 0; };

static void ResizeNearestBGRAtoRGB8(
  const uint8_t* srcBGRA, int srcW, int srcH, int srcStrideBytes,
  uint8_t* dstRGB, int dstW, int dstH)
{
  // simple nearest resize
  for (int y = 0; y < dstH; y++) {
    int sy = (int)((int64_t)y * srcH / dstH);
    const uint8_t* srcRow = srcBGRA + sy * srcStrideBytes;
    uint8_t* dstRow = dstRGB + y * dstW * 3;
    for (int x = 0; x < dstW; x++) {
      int sx = (int)((int64_t)x * srcW / dstW);
      const uint8_t* p = srcRow + sx * 4; // BGRA
      dstRow[x*3 + 0] = p[2]; // R
      dstRow[x*3 + 1] = p[1]; // G
      dstRow[x*3 + 2] = p[0]; // B
    }
  }
}

static void PrepareInput256(
  const uint8_t* srcBGRA, int srcW, int srcH, int srcStrideBytes,
  std::vector<float>& outNHWC, PadInfo& pad)
{
  // Preserve aspect ratio, pad right or bottom to 256x256.
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
  pad.resizedW = rW;
  pad.resizedH = rH;

  std::vector<uint8_t> resizedRGB(rW * rH * 3);
  ResizeNearestBGRAtoRGB8(srcBGRA, srcW, srcH, srcStrideBytes, resizedRGB.data(), rW, rH);

  // Build padded 256x256 RGB float
  outNHWC.assign(1 * T * T * 3, 0.0f);
  for (int y = 0; y < rH; y++) {
    for (int x = 0; x < rW; x++) {
      int srcIdx = (y*rW + x)*3;
      int dstIdx = (y*T + x)*3;
      outNHWC[dstIdx + 0] = resizedRGB[srcIdx + 0] / 255.0f;
      outNHWC[dstIdx + 1] = resizedRGB[srcIdx + 1] / 255.0f;
      outNHWC[dstIdx + 2] = resizedRGB[srcIdx + 2] / 255.0f;
    }
  }
}

static void ArgMaxToLabels(
  const float* probsNHWC, int H, int W, int C,
  std::vector<uint8_t>& labelsHW)
{
  labelsHW.resize(H * W);
  for (int i = 0; i < H*W; i++) {
    const float* p = probsNHWC + i*C;
    int best = 0;
    float bestv = p[0];
    for (int c = 1; c < C; c++) {
      if (p[c] > bestv) { bestv = p[c]; best = c; }
    }
    labelsHW[i] = (uint8_t)best;
  }
}

// Unpad + resize labels back to output size with nearest
static void ResizeLabelsToOut(
  const std::vector<uint8_t>& labels256, const PadInfo& pad,
  int outW, int outH,
  std::vector<uint8_t>& outLabels)
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
      outLabels[y*outW + x] = labels256[sy*T + sx];
    }
  }
}

static void LabelsToBGRA(
  const std::vector<uint8_t>& labels, int W, int H,
  std::vector<uint8_t>& outBGRA)
{
  outBGRA.resize(W * H * 4);
  for (int i = 0; i < W*H; i++) {
    int cls = labels[i];
    RGB c = ClassColor(cls);
    outBGRA[i*4 + 0] = c.b;
    outBGRA[i*4 + 1] = c.g;
    outBGRA[i*4 + 2] = c.r;
    outBGRA[i*4 + 3] = (cls == 0) ? 0 : 255; // transparent background
  }
}

static void PrintConfig(const Config& c)
{
  std::cout
    << "Config: inputSenderName=" << c.inputSenderName
    << " senderName=" << c.senderName
    << " size=" << c.outW << "x" << c.outH
    << " modelPath=" << c.modelPath
    << " device=" << c.device
    << " outputMode=" << c.outputMode
    << "\n";
}

int main()
{
  std::cout << "### BUILD MARKER: SPOUTMASKSENDER_DX11_V04_REAL_SEGMENTATION ###\n";

  Config cfg = LoadConfigOrDefaults();
  PrintConfig(cfg);

  if (cfg.modelPath.empty() || !std::filesystem::exists(cfg.modelPath)) {
    std::cerr << "ERROR: modelPath missing or file not found: " << cfg.modelPath << "\n";
    std::cerr << "Set modelPath in sender_config.json, e.g. Z:\\MediaPipe\\Working-dir\\Spout-v04\\models\\selfie_multiclass_256x256.tflite\n";
    return 2;
  }

  // --- OpenVINO init ---
  ov::Core core;
  std::shared_ptr<ov::Model> model;
  try {
    model = core.read_model(cfg.modelPath);
  } catch (const std::exception& e) {
    std::cerr << "OpenVINO read_model failed: " << e.what() << "\n";
    return 3;
  }

  ov::CompiledModel compiled;
  try {
    compiled = core.compile_model(model, cfg.device);
  } catch (const std::exception& e) {
    std::cerr << "OpenVINO compile_model failed (device=" << cfg.device << "): " << e.what() << "\n";
    std::cerr << "Try device=\"CPU\" in config.\n";
    return 4;
  }

  ov::InferRequest infer = compiled.create_infer_request();

  // --- D3D11 device ---
  ComPtr<ID3D11Device> d3d;
  ComPtr<ID3D11DeviceContext> ctx;
  D3D_FEATURE_LEVEL fl;
  HRESULT hr = D3D11CreateDevice(
    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
    nullptr, 0, D3D11_SDK_VERSION, &d3d, &fl, &ctx);
  if (FAILED(hr)) {
    std::cerr << "D3D11CreateDevice failed: 0x" << std::hex << (uint32_t)hr << std::dec << "\n";
    return 5;
  }

  // --- Spout receiver & sender ---
  spoutDX spout;
  if (!spout.OpenDirectX11(d3d.Get())) {
    std::cerr << "Spout OpenDirectX11 failed\n";
    return 6;
  }

  // Receiver state
  char inName[256]{0};
  strncpy_s(inName, cfg.inputSenderName.c_str(), _TRUNCATE);

  unsigned int inW = 0, inH = 0;
  HANDLE inHandle = nullptr;

  // We'll receive into a texture we own.
  ComPtr<ID3D11Texture2D> recvTex;
  ComPtr<ID3D11Texture2D> stagingTex;

  auto ensureRecvTextures = [&](unsigned int w, unsigned int h) -> bool {
    if (recvTex) {
      D3D11_TEXTURE2D_DESC d{};
      recvTex->GetDesc(&d);
      if (d.Width == w && d.Height == h) return true;
    }

    recvTex.Reset();
    stagingTex.Reset();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    HRESULT r = d3d->CreateTexture2D(&desc, nullptr, &recvTex);
    if (FAILED(r)) {
      std::cerr << "CreateTexture2D recvTex failed\n";
      return false;
    }

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    r = d3d->CreateTexture2D(&desc, nullptr, &stagingTex);
    if (FAILED(r)) {
      std::cerr << "CreateTexture2D stagingTex failed\n";
      return false;
    }
    return true;
  };

  // Sender output
  if (!spout.CreateSender(cfg.senderName.c_str(), cfg.outW, cfg.outH, 0)) {
    std::cerr << "Spout CreateSender failed: " << cfg.senderName << "\n";
    return 7;
  }

  // Output texture
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
      std::cerr << "CreateTexture2D outTex failed\n";
      return 8;
    }
  }

  std::cout << "Waiting for Spout sender: " << cfg.inputSenderName << "\n";

  // Buffers
  std::vector<uint8_t> outBGRA;
  std::vector<float> in256;
  PadInfo pad{};
  std::vector<uint8_t> labels256;
  std::vector<uint8_t> labelsOut;

  while (true) {
    // Establish/maintain receiver connection and get sender size
    bool ok = spout.CreateReceiver(inName, inW, inH, false);
    if (!ok || inW == 0 || inH == 0) {
      SleepMs(100);
      continue;
    }

    if (!ensureRecvTextures(inW, inH)) {
      SleepMs(100);
      continue;
    }

    // Receive frame into recvTex
    bool received = spout.ReceiveTexture(inName, inW, inH, recvTex.Get(), D3D11_TEXTURE2D_DESC{}.Format, false, 0);
    if (!received) {
      // Sender might have closed; retry
      spout.ReleaseReceiver();
      SleepMs(50);
      continue;
    }

    // Readback to CPU
    ctx->CopyResource(stagingTex.Get(), recvTex.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
      SleepMs(1);
      continue;
    }

    const uint8_t* bgra = reinterpret_cast<const uint8_t*>(mapped.pData);
    int stride = (int)mapped.RowPitch;

    // Preprocess -> 256x256 RGB float
    PrepareInput256(bgra, (int)inW, (int)inH, stride, in256, pad);

    ctx->Unmap(stagingTex.Get(), 0);

    // Inference
    // Input tensor: [1,256,256,3] f32 NHWC
    ov::Tensor inputTensor(ov::element::f32, {1, 256, 256, 3}, in256.data());
    infer.set_input_tensor(inputTensor);
    infer.infer();
    ov::Tensor outTensor = infer.get_output_tensor();

    // Output: [1,256,256,6] f32
    const float* probs = outTensor.data<const float>();
    ArgMaxToLabels(probs, 256, 256, 6, labels256);

    // Resize back to output
    ResizeLabelsToOut(labels256, pad, cfg.outW, cfg.outH, labelsOut);

    // Colorize
    LabelsToBGRA(labelsOut, cfg.outW, cfg.outH, outBGRA);

    // Upload to outTex (dynamic)
    D3D11_MAPPED_SUBRESOURCE outMap{};
    hr = ctx->Map(outTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &outMap);
    if (SUCCEEDED(hr)) {
      uint8_t* dst = reinterpret_cast<uint8_t*>(outMap.pData);
      int dstStride = (int)outMap.RowPitch;
      const uint8_t* srcRow = outBGRA.data();
      for (int y = 0; y < cfg.outH; y++) {
        memcpy(dst + y*dstStride, srcRow + y*cfg.outW*4, cfg.outW*4);
      }
      ctx->Unmap(outTex.Get(), 0);
    }

    // Send via Spout
    spout.SendTexture(outTex.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cfg.outW, cfg.outH, false, 0);

    // tiny sleep to reduce CPU if needed
    SleepMs(1);
  }

  return 0;
}