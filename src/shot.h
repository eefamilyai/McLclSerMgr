#pragma once
#include <d3d11.h>

// Reads back the swap chain's current back buffer and writes it as an RGB PNG.
bool saveBackbufferPng(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* swap, const char* path);
