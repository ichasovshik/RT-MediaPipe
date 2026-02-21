import os
import sys

IN_TFLITE = r"Z:\MediaPipe\Working-dir\Spout-v04\models\selfie_multiclass_256x256.tflite"
OUT_ONNX  = r"Z:\MediaPipe\Working-dir\Spout-v04\models\selfie_multiclass_256x256.onnx"

def main():
    try:
        import ai_edge_torch
        import torch
    except Exception as e:
        print("Failed to import ai_edge_torch/torch:", e)
        return 2

    if not os.path.exists(IN_TFLITE):
        print("Missing input:", IN_TFLITE)
        return 3

    # Convert TFLite → torch module
    # Note: API surface can vary by ai-edge-torch version.
    # We'll try the common pattern.
    try:
        m = ai_edge_torch.convert(IN_TFLITE)
    except Exception as e:
        print("ai_edge_torch.convert failed:", e)
        return 4

    m.eval()

    # MediaPipe selfie_multiclass_256x256 typically expects NHWC uint8 input [1,256,256,3]
    dummy = torch.zeros((1, 256, 256, 3), dtype=torch.uint8)

    try:
        torch.onnx.export(
            m,
            dummy,
            OUT_ONNX,
            opset_version=17,
            input_names=["input"],
            output_names=["output"],
            dynamic_axes=None,
        )
    except Exception as e:
        print("torch.onnx.export failed:", e)
        return 5

    print("Wrote:", OUT_ONNX)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())