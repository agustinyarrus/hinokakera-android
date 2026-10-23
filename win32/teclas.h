// Teclas: código de SDL -> tecla virtual de Windows (VK_*) y código de DirectInput (DIK_*, scancode del set 1).
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct { SDL_Scancode sdl; uint8_t vk, dik; } Tecla;
extern const Tecla TECLAS[];
const Tecla *tecla_sdl(SDL_Scancode s);
const Tecla *tecla_vk(uint32_t vk);
