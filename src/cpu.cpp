#include "cpu.h"
#include <SDL3/SDL_log.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdio.h>

// define nibbles
#define OP_X ((opcode & 0x0F00) >> 8)
#define OP_Y ((opcode & 0x00F0) >> 4)
#define OP_N (opcode & 0x000F)
#define OP_NN (opcode & 0x00FF)
#define OP_NNN (opcode & 0x0FFF)

uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

void cpu::init() {
  pc = 0x200;                            // Program Counter to start of program
  opcode = 0x00;                         // Reset current opcode
  I = 0;                                 // Reset index register
  sp = 0;                                // Reset stack pointer
  memset(memory, 0, sizeof(memory));     // Clear memory
  memset(stack, 0, sizeof(stack));       // Clear stack
  memset(gfx, 0, sizeof(gfx));           // Reset display
  memset(key, 0, sizeof(key));           // Reset keys
  memset(prevKeys, 0, sizeof(prevKeys)); // Reset Prevkeys

  // Load font into memory
  for (int i = 0; i < 80; i++) {
    memory[i] = chip8_fontset[i];
  }
  breakIPF = false;
  draw = false;
  running = true;
}

bool cpu::loadRom(const char *romName) {
  FILE *rom = fopen(romName, "rb");
  if (!rom) {
    SDL_Log("ERROR: incorrect file path");
    fclose(rom);
    return false;
  }

  // check size of rom fits
  fseek(rom, 0, SEEK_END);
  const size_t romSize = ftell(rom);
  const size_t maxSize = (sizeof(memory) - 0x200);
  rewind(rom);

  if (romSize > maxSize) {
    SDL_Log("ERROR: Rom size is too big");
    fclose(rom);
    return false;
  }

  // Load rom into memory
  if (fread(&memory[0x200], romSize, 1, rom) != 1) {
    SDL_Log("ERROR: loading rom into memory");
    fclose(rom);
    return false;
  };

  fclose(rom);
  return true;
}

void cpu::executeCycle() {
  // fetch
  breakIPF = false; // reset breakloop
  opcode = memory[pc] << 8 | memory[pc + 1];
  pc += 2;
  // std::cout << std::hex << opcode << std::endl;

  // decode & execute
  switch (opcode & 0xF000) {
  case (0x0000): {
    switch (opcode & 0x000F) {
    case (0x0): // 00E0: Clear Screen
      std::memset(gfx, 0, 64 * 32);
      breakIPF = true;
      break;
    case (0xE): // 00EE: return from subroutine
      sp--;
      pc = stack[sp];
      break;
    }
    break;
  }
  case (0x1000): // 1NNN: jump to NNN
  {
    pc = OP_NNN;
    break;
  }
  case (0x2000): // 2NNN: jump to NNN, push PC to stack
  {
    stack[sp] = pc;
    sp++;
    pc = OP_NNN;
    break;
  }
  case (0x3000): // 3XNN: skip instruction if VX == NN
  {
    if (V[OP_X] == OP_NN) {
      pc += 2;
    }
    break;
  }

  case (0x4000): // 4XNN: skip instruction if VX != NN
  {
    if (V[OP_X] != OP_NN) {
      pc += 2;
    }
    break;
  }

  case (0x5000): // 5XY0: skip instruction if VX == VY
  {
    if (V[OP_X] == V[OP_Y]) {
      pc += 2;
    }
    break;
  }
  case (0x6000): // 6XNN: set V[X] to NN;
  {
    V[OP_X] = OP_NN;
    break;
  }
  case (0x7000): // 7XNN: V[X] += NNk
  {
    V[OP_X] += OP_NN;
    break;
  }

  case (0x8000): // Logic functions
  {
    switch (opcode & 0x000F) {
    case (0x0): // 8XY0: VX = VY
    {
      V[OP_X] = V[OP_Y];
      break;
    }
    case (0x1): // 8XY1: VX |= VY
    {
      V[OP_X] |= V[OP_Y];
      V[0xF] = 0;
      break;
    }
    case (0x2): // 8XY2: VX &= VY
    {
      V[OP_X] &= V[OP_Y];
      V[0xF] = 0;
      break;
    }
    case (0x3): // 8XY3: VX ^= VY
    {
      V[OP_X] ^= V[OP_Y];
      V[0xF] = 0;
      break;
    }

    case (0x4): // 8XY4: VX += VY (carry flag VF)
    {
      int x = V[OP_X] + V[OP_Y];
      V[OP_X] += V[OP_Y];
      V[0xF] = (x > 255) ? 1 : 0;
      break;
    }

    case (0x5): // 8XY5: VX -= VY
    {
      uint16_t temp = (V[OP_X] >= V[OP_Y]) ? 1 : 0;
      V[OP_X] -= V[OP_Y];
      V[0xF] = temp;
      break;
    }
    case (0x6): // AMBIGUOUS! 8XY6: VX = VY, VX >> 1, VF = bitshifted num
    {
      uint16_t temp = (V[OP_X] & 0b1);
      V[OP_X] = V[OP_Y];
      V[OP_X] >>= 1;
      V[0xF] = temp;
      break;
    }

    case (0x7): // 8XY7: VX = VY - VX
    {
      uint16_t temp = (V[OP_X] <= V[OP_Y]) ? 1 : 0;
      V[OP_X] = V[OP_Y] - V[OP_X];
      V[0xF] = temp;
      break;
    }

    case (0xE): // AMBIGUOUS! 8XYE: VX = VY, VX << 1, VF = bitshifted num
    {
      V[OP_X] = V[OP_Y];
      uint16_t temp = (((V[OP_X] & 0x80) >> 7) & 0b1);
      V[OP_X] <<= 1;
      V[0xF] = temp;
      break;
    }
    }
    break;
  }

  case (0x9000): { // 9XY0: skip insturction if VX != VY
    if (V[OP_X] != V[OP_Y]) {
      pc += 2;
    }
    break;
  }

  case (0xA000): // ANNN: I = NNN
  {
    I = OP_NNN;
    break;
  }

  case (0xB000): { // BNNN: PC = V0 + NNN
    pc = OP_NNN + V[0x0];
    break;
  }

  case (0xC000): { // CXNN: VX = rand() & NN
    int randNum = rand() % 256;
    V[OP_X] = randNum & OP_NN;
    break;
  }

  case (0xD000): // DXYN: Display V[X] = xpos, V[Y] = ypos, N = height
  {
    // CLIPPING : OFF
    uint16_t x = V[OP_X] % 64;
    uint16_t y = V[OP_Y] % 32;
    uint16_t n = OP_N;
    uint16_t pixel = 0;
    V[0xF] = 0;
    for (int height = 0; height < n; height++) {
      if (y + height > 32) {
        continue;
      }
      pixel = memory[I + height];
      for (int bit = 0; bit < 8; bit++) {
        if (x + bit > 64) {
          continue;
        }
        if ((pixel & (0x80 >> bit)) != 0) {
          uint16_t drawX = (x + bit) % 64;
          uint16_t drawY = (y + height) % 32;
          uint16_t index = drawX + (drawY * 64);
          if (gfx[index] == 1) {
            V[0xF] = 1;
          }
          gfx[index] ^= 1;
        }
      }
    }
    breakIPF = true;
    draw = true;
    break;
  }

    // CLIPPING : ON
    // uint16_t x = V[OP_X];
    // uint16_t y = V[OP_Y];
    // uint16_t n = OP_N;
    // uint16_t pixel = 0;
    // V[0xF] = 0;
    //
    // for (int height = 0; height < n; height++) {
    //   if (y + height >= 32) { // Clip vertically
    //     break;
    //   }
    //   pixel = memory[I + height];
    //   for (int bit = 0; bit < 8; bit++) {
    //     if (x + bit >= 64) { // Clip horizontally
    //       continue;
    //     }
    //     uint16_t drawX = x + bit;
    //     uint16_t drawY = y + height;
    //     uint16_t index = drawX + (drawY * 64);
    //
    //     if ((pixel & (0x80 >> bit)) != 0) {
    //       if (gfx[index] == 1) {
    //         V[0xF] = 1; // Set collision flag
    //       }
    //       gfx[index] ^= 1; // Toggle the pixel
    //     }
    //   }
    // }
    // breakIPF = true;
    // break;
    //

  case (0xE000): {
    switch (opcode & 0x00FF) {
    case (0x9E): { // EX9E: skip next instructin if button in VX pressed
      if (key[V[OP_X]] != 0) {
        pc += 2;
      }
      break;
    }
    case (0xA1): { // EXA1: skip instruction if button in VX NOT pressed
      if (key[V[OP_X]] == 0) {
        pc += 2;
      }
      break;
    }
    }
    break;
  }

  case (0xF000): {
    switch (opcode & 0x00FF) {
    case (0x0A): { // FX0A: Vx = get_key()
      bool keyReleased = false;
      for (int i = 0; i < 16; i++) {
        if (prevKeys[i] == 1 && key[i] == 0) {
          V[OP_X] = i;
          keyReleased = true;
          breakIPF = true;
          break;
        }
      }
      if (!keyReleased) {
        pc -= 2;
        return;
      }
      break;
    }
    case (0x07): { // FX07: VX = delay_timer
      V[OP_X] = delay_timer;
      break;
    }
    case (0x15): { // FX15: delay_timer = VX
      delay_timer = V[OP_X];
      break;
    }
    case (0x18): { // FX18: sound_timer = VX
      sound_timer = V[OP_X];
      break;
    }
    case (0x1E): { // FX1E: I += VX
      I += V[OP_X];
      break;
    }
    case (0x29): {
      I = (V[OP_X] * 0x5);
      break;
    }

    case (0x33): { // FX33: Binary-coded decimal conversion
      int hundreds = 0, tens = 0, ones = 0;
      int number = V[OP_X];
      hundreds = number / 100;
      tens = (number / 10) % 10;
      ones = number % 10;
      memory[I] = hundreds;
      memory[I + 1] = tens;
      memory[I + 2] = ones;
      break;
    }
    case (0x55): { // FX55: store memory from V0 to VX
      for (int i = 0; i <= OP_X; i++) {
        memory[I] = V[i];
        I++; // I gets incremented due to classic chip8 implementation
      }
      break;
    }
    case (0x65): { // FX65: store memory from V0 to VX
      for (int i = 0; i <= OP_X; i++) {
        V[i] = memory[I];
        I++; // I gets incremented due to classic chip8 implementation
      }
      break;
    }
    }
    break;
  }
  default: {
    SDL_Log("ERROR: unrecognized opcode");
    running = false;
  }
  }
}

void cpu::keyDown(int pressedKey) { key[pressedKey] = 1; }
void cpu::keyUp(int pressedKey) { key[pressedKey] = 0; }

bool cpu::timers() {
  if (sound_timer > 0) {
    sound_timer--;
  }
  if (delay_timer > 0) {
    delay_timer--;
  }
  return (sound_timer > 0);
}
