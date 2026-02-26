# RT-MediaPipe

Real-time computer vision application built with [MediaPipe](https://github.com/google-ai-edge/mediapipe).

## Status

> **Work in progress.** The project is in early development; no source code has been committed yet.

## Overview

RT-MediaPipe aims to provide real-time inference pipelines powered by MediaPipe models (e.g. selfie segmentation / `selfie_multiclass_256x256`), running locally via an ONNX-based runtime.

## Planned Stack

- **MediaPipe** – pre-trained models (`.tflite` → `.onnx`)
- **Python** – application logic
- **OpenCV** – video capture and display

## Related

- [`ichasovshik/model-tools`](https://github.com/ichasovshik/model-tools) – TFLite → ONNX conversion workflows used to prepare models for this project
