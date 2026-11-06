// DAO: why a draw failed in the Vulkan backend. Upstream, IssueDraw and the
// pipeline cache return false from many places without a word, and the PM4
// layer then logs only "Failed in backend". Each of those places reports its
// reason here; every distinct (reason, detail, guest primitive, host vertex
// shader type) goes to the log once, so a tester's log names the cause without
// flooding. The lines go to the runtime's log (dao.log / sod.log) through
// dao_stats' sink, or to Xenia's log as errors when no sink is set.
#ifndef XENIA_GPU_VULKAN_DAO_DRAW_FAIL_H_
#define XENIA_GPU_VULKAN_DAO_DRAW_FAIL_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/dao_vulkan_stats.h"

namespace xe::gpu::vulkan::dao_draw_fail {

enum class Reason : uint32_t {
  kNoVertexShader,
  kBeginSubmission,
  kPrimitiveProcessing,
  // detail: the host vertex shader type the backend can't draw.
  kHostVertexShaderType,
  kShaderTranslation,
  kSampler,
  kRenderTargets,
  // detail: the host primitive type.
  kPipelineState,
  kPipelineLayout,
  kGeometryShader,
  kRenderPass,
  kTessellationShaders,
  // detail: the RequirementUnmet that failed.
  kPipelineRequirements,
  kVertexShaderInvalid,
  kPixelShaderInvalid,
  // detail: the pipeline topology.
  kPrimitiveRestart,
  // detail: the VkResult (as a signed value).
  kPipelineCreation,
  // The pipeline's creation failed earlier (reported then); the description
  // stays cached with no pipeline, so every draw with it fails here.
  kPipelineNotCreated,
  kBindings,
  // detail: the processed index buffer type.
  kIndexBuffer,
};

// What ArePipelineRequirementsMet rejects, as kPipelineRequirements' detail.
enum class RequirementUnmet : uint32_t {
  kNone,
  kZpdHybrid,
  kVizSurveyShader,
  kVertexShaderStage,
  kGeometryShader,
  kTriangleFans,
  kDepthClamp,
  kPointPolygons,
  kFillModeNonSolid,
  kConstantAlphaBlend,
};

inline const char* ReasonName(Reason reason) {
  switch (reason) {
    case Reason::kNoVertexShader:
      return "no vertex shader";
    case Reason::kBeginSubmission:
      return "submission not started";
    case Reason::kPrimitiveProcessing:
      return "primitive processing";
    case Reason::kHostVertexShaderType:
      return "unsupported host vertex shader type";
    case Reason::kShaderTranslation:
      return "shader translation";
    case Reason::kSampler:
      return "sampler";
    case Reason::kRenderTargets:
      return "render targets";
    case Reason::kPipelineState:
      return "pipeline state (primitive type)";
    case Reason::kPipelineLayout:
      return "pipeline layout";
    case Reason::kGeometryShader:
      return "geometry shader";
    case Reason::kRenderPass:
      return "render pass";
    case Reason::kTessellationShaders:
      return "tessellation shaders";
    case Reason::kPipelineRequirements:
      return "pipeline requirements";
    case Reason::kVertexShaderInvalid:
      return "vertex shader invalid";
    case Reason::kPixelShaderInvalid:
      return "pixel shader invalid";
    case Reason::kPrimitiveRestart:
      return "primitive restart with this topology";
    case Reason::kPipelineCreation:
      return "vkCreateGraphicsPipelines";
    case Reason::kPipelineNotCreated:
      return "pipeline not created (failed earlier)";
    case Reason::kBindings:
      return "bindings (descriptors or constants)";
    case Reason::kIndexBuffer:
      return "index buffer type";
  }
  return "?";
}

inline const char* RequirementName(RequirementUnmet requirement) {
  switch (requirement) {
    case RequirementUnmet::kNone:
      return "none";
    case RequirementUnmet::kZpdHybrid:
      return "hybrid occlusion query";
    case RequirementUnmet::kVizSurveyShader:
      return "VIZ survey shader";
    case RequirementUnmet::kVertexShaderStage:
      return "vertex shader stage";
    case RequirementUnmet::kGeometryShader:
      return "geometryShader";
    case RequirementUnmet::kTriangleFans:
      return "triangleFans";
    case RequirementUnmet::kDepthClamp:
      return "depthClamp";
    case RequirementUnmet::kPointPolygons:
      return "pointPolygons";
    case RequirementUnmet::kFillModeNonSolid:
      return "fillModeNonSolid";
    case RequirementUnmet::kConstantAlphaBlend:
      return "constantAlphaColorBlendFactors";
  }
  return "?";
}

// The draw being issued, for the reports from inside the pipeline cache (set
// by IssueDraw; the command processor thread only, as are the reports, except
// the asynchronous pipeline creation threads, which the mutex covers).
struct DrawContext {
  uint32_t guest_primitive = UINT32_MAX;
  uint32_t host_vertex_shader_type = UINT32_MAX;
};
inline DrawContext& CurrentDraw() {
  static DrawContext context;
  return context;
}

inline void Report(Reason reason, int64_t detail = 0) {
  const DrawContext context = CurrentDraw();
  static std::mutex mutex;
  static std::unordered_set<std::string> reported;
  std::string key = std::to_string(uint32_t(reason)) + ':' +
                    std::to_string(detail) + ':' +
                    std::to_string(context.guest_primitive) + ':' +
                    std::to_string(context.host_vertex_shader_type);
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!reported.insert(std::move(key)).second) {
      return;
    }
  }
  std::string detail_text;
  if (reason == Reason::kPipelineRequirements) {
    detail_text = RequirementName(RequirementUnmet(detail));
  } else {
    detail_text = std::to_string(detail);
  }
  const std::string line = fmt::format(
      "Draw failed in the backend: {} ({}); guest primitive {}, host vertex "
      "shader type {} (logged once for this combination)",
      ReasonName(reason), detail_text, int64_t(int32_t(context.guest_primitive)),
      int64_t(int32_t(context.host_vertex_shader_type)));
  if (dao_stats::ReportSink()) {
    dao_stats::ReportSink()(line.c_str());
  } else {
    XELOGE("{}", line);
  }
}

}  // namespace xe::gpu::vulkan::dao_draw_fail

#endif  // XENIA_GPU_VULKAN_DAO_DRAW_FAIL_H_
