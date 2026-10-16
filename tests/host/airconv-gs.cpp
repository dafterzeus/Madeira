// Native regression for the real airconv parser and GS payload converter.
// DXBC tokens are assembled with DXBCParser's format macros; no fxc or GPU.
#include "dxbc_converter.hpp"
#include "DXBCParser/BlobContainer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace microsoft;
using namespace dxmt::dxbc;

static uint32_t op(D3D10_SB_OPCODE_TYPE type, uint32_t length) {
  return ENCODE_D3D10_SB_OPCODE_TYPE(type) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(length);
}

static uint32_t operand(D3D10_SB_OPERAND_TYPE type, D3D10_SB_OPERAND_INDEX_DIMENSION dim, uint32_t mask) {
  return ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
         ENCODE_D3D10_SB_OPERAND_TYPE(type) | ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(dim) |
         ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(mask << 4);
}

static std::vector<uint32_t> container(std::vector<uint32_t> code) {
  code[1] = code.size();
  // Empty signatures suffice: the GS consumes inherited register payloads.
  std::vector<uint32_t> words = {DXBC_FOURCC_NAME, 0, 0, 0, 0, 1, 0, 3, 44, 60, 76};
  words.insert(words.end(), {DXBC_InputSignature, 8, 0, 8, DXBC_OutputSignature, 8, 0, 8});
  words.insert(words.end(), {DXBC_GenericShaderEx, uint32_t(code.size() * 4)});
  words.insert(words.end(), code.begin(), code.end());
  words[6] = words.size() * 4;
  return words;
}

static std::vector<uint32_t> geometry(uint32_t semantic, uint32_t mask = 8,
                                      D3D10_SB_OPCODE_TYPE decl = D3D10_SB_OPCODE_DCL_INPUT_SIV) {
  return container({
    ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_GEOMETRY_SHADER, 5, 0), 0,
    op(D3D10_SB_OPCODE_DCL_GS_INPUT_PRIMITIVE, 1) | ENCODE_D3D10_SB_GS_INPUT_PRIMITIVE(D3D10_SB_PRIMITIVE_TRIANGLE),
    op(D3D10_SB_OPCODE_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY, 1) |
        ENCODE_D3D10_SB_GS_OUTPUT_PRIMITIVE_TOPOLOGY(D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST),
    op(D3D10_SB_OPCODE_DCL_MAX_OUTPUT_VERTEX_COUNT, 2), 1,
    op(decl, 5), operand(D3D10_SB_OPERAND_TYPE_INPUT, D3D10_SB_OPERAND_INDEX_2D, mask), 3, 7, semantic,
    op(D3D10_SB_OPCODE_RET, 1)
  });
}

static sm50_shader_t initialize(const std::vector<uint32_t> &bytes, bool success, uint32_t semantic = 0,
                                 bool error_pointer = true) {
  sm50_shader_t shader = nullptr;
  sm50_error_t error = nullptr;
  MTL_SHADER_REFLECTION reflection = {};
  int result = SM50Initialize(bytes.data(), bytes.size() * 4, &shader, &reflection, error_pointer ? &error : nullptr);
  assert((result == 0) == success);
  assert(bool(shader) == success);
  if (!success && error_pointer) {
    assert(error);
    char message[512];
    SM50GetErrorMessage(error, message, sizeof(message));
    assert(std::strstr(message, ("semantic " + std::to_string(semantic)).c_str()));
    assert(std::strstr(message, "register 7"));
    std::cout << message << '\n';
    SM50FreeError(error);
  }
  return shader;
}

int main() {
  for (auto semantic : {D3D10_SB_NAME_POSITION, D3D10_SB_NAME_CLIP_DISTANCE, D3D10_SB_NAME_CULL_DISTANCE,
                        D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX, D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX}) {
    for (auto mask : {1u, 2u, 4u, 8u, 15u}) {
      auto shader = initialize(geometry(semantic, mask), true);
      SM50Destroy(shader);
    }
  }
  // Unknown/future values and a primitive ID misdeclared as per-vertex SIV
  // must produce an initialization error, with or without an error receiver.
  for (uint32_t semantic : {0u, 6u, 7u, 8u, 9u, 10u, 11u, 31u, 255u}) {
    initialize(geometry(semantic), false, semantic);
    initialize(geometry(semantic), false, semantic, false);
  }
  initialize(geometry(D3D10_SB_NAME_PRIMITIVE_ID, 8, D3D10_SB_OPCODE_DCL_INPUT_SGV), false, D3D10_SB_NAME_PRIMITIVE_ID);

  // Packed clip .w and cull .z at a high register; primitive ID remains its
  // separate, zero-dimensional dcl_input operand.
  auto packed = geometry(D3D10_SB_NAME_CLIP_DISTANCE);
  auto insert_at = packed.end() - 1;
  packed.insert(insert_at, {
    op(D3D10_SB_OPCODE_DCL_INPUT_SIV, 5), operand(D3D10_SB_OPERAND_TYPE_INPUT, D3D10_SB_OPERAND_INDEX_2D, 4), 3, 7,
    D3D10_SB_NAME_CULL_DISTANCE,
    op(D3D10_SB_OPCODE_DCL_INPUT, 2), operand(D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID, D3D10_SB_OPERAND_INDEX_0D, 1),
    op(D3D10_SB_OPCODE_DCL_OUTPUT_SIV, 4), operand(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_INDEX_1D, 15), 0,
    D3D10_SB_NAME_POSITION,
    op(D3D10_SB_OPCODE_MOV, 6), operand(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_INDEX_1D, 15), 0,
    ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
        ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_INPUT) |
        ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) |
        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
        ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(0, 1, 2, 3), 0, 7,
    op(D3D10_SB_OPCODE_EMIT, 1), op(D3D10_SB_OPCODE_CUT, 1)
  });
  packed[6] = packed.size() * 4;
  packed[20] += 19 * 4; // SHEX blob size
  packed[22] += 19;     // tokenized program size
  auto shader = initialize(packed, true);
  std::unique_ptr<SM50ShaderInternal> parsed;
  auto gs = with_parsed_program(static_cast<SM50ShaderInternal *>(shader), parsed);
  assert(gs);

  // The VS must reserve the entire output register even when cull distance
  // has no rasterizer output on iOS. GS gets the unmodified register bits.
  auto vs_bytes = container({
    ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_VERTEX_SHADER, 5, 0), 0,
    op(D3D10_SB_OPCODE_DCL_OUTPUT_SIV, 4), operand(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_INDEX_1D, 4), 7,
    D3D10_SB_NAME_CULL_DISTANCE, op(D3D10_SB_OPCODE_RET, 1)
  });
  auto vertex = initialize(vs_bytes, true);
  auto vs = static_cast<SM50ShaderInternal *>(vertex);
  assert(vs->max_output_register == 8);
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  llvm::Module module("gs_clip_distance", context);
  auto error = convert_dxbc_geometry_shader(gs, "gs_clip_distance", vs, context, module, nullptr);
  if (error) {
    std::cerr << llvm::toString(std::move(error)) << '\n';
    return 1;
  }
  assert(!llvm::verifyModule(module, &llvm::errs()));
  unsigned vector_loads = 0;
  for (auto &function : module)
    for (auto &block : function)
      for (auto &inst : block)
        if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst))
          if (load->getType() == llvm::FixedVectorType::get(llvm::Type::getInt32Ty(context), 4))
            vector_loads++;
  // Every component of every input vertex/register, plus the mov and output.
  assert(vector_loads >= 3 * 8);
  SM50Destroy(vertex);
  SM50Destroy(shader);
  std::cout << "PASS: GS system values, masks, packed payload, primitive ID, and initialization errors\n";
}
