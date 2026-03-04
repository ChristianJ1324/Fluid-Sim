#include "FluidSimulation.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

FluidSimulation::FluidSimulation(int width, int height, int depth)
    : m_Width(width), m_Height(height), m_Depth(depth) {
  initTextures();
  initShaders();
  initQuad();
}

FluidSimulation::~FluidSimulation() {
  glDeleteTextures(2, m_TexVelocity);
  glDeleteTextures(2, m_TexDensity);
  glDeleteTextures(2, m_TexPressure);
  glDeleteTextures(1, &m_TexDivergence);
  glDeleteTextures(1, &m_TexVorticity);

  glDeleteProgram(m_ProgAdvect);
  glDeleteProgram(m_ProgDivergence);
  glDeleteProgram(m_ProgJacobi);
  glDeleteProgram(m_ProgSubtract);
  glDeleteProgram(m_ProgVorticity);
  glDeleteProgram(m_ProgVorticityForce);
  glDeleteProgram(m_ProgSplat);
  glDeleteProgram(m_ProgRaymarch);

  glDeleteVertexArrays(1, &m_QuadVAO);
  glDeleteBuffers(1, &m_QuadVBO);
}

void FluidSimulation::initTextures() {
  auto create3DTexture = [&](GLuint &tex, GLenum internalFormat) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);

    // Linear filtering is REQUIRED for advection
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Clamp to edge to prevent wrapping at boundaries
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glTexImage3D(GL_TEXTURE_3D, 0, internalFormat, m_Width, m_Height, m_Depth,
                 0, internalFormat == GL_RGBA16F ? GL_RGBA : GL_RED, GL_FLOAT,
                 nullptr);
  };

  for (int i = 0; i < 2; ++i) {
    create3DTexture(m_TexVelocity[i], GL_RGBA16F); // XYZ + unused W
    create3DTexture(m_TexDensity[i], GL_R16F);     // Single channel density
    create3DTexture(m_TexPressure[i], GL_R16F);
  }

  create3DTexture(m_TexDivergence, GL_R16F);
  create3DTexture(m_TexVorticity, GL_RGBA16F); // 3D vector + length
}

void FluidSimulation::initShaders() {
  // Assuming shaders are loaded relative to the working directory
  m_ProgAdvect = compileComputeShader("shaders/advect.comp");
  m_ProgDivergence = compileComputeShader("shaders/divergence.comp");
  m_ProgJacobi = compileComputeShader("shaders/jacobi.comp");
  m_ProgSubtract = compileComputeShader("shaders/subtract.comp");
  m_ProgVorticity = compileComputeShader("shaders/vorticity.comp");
  m_ProgVorticityForce = compileComputeShader("shaders/vorticity_force.comp");
  m_ProgSplat = compileComputeShader("shaders/splat.comp");

  m_ProgRaymarch =
      compileRenderShader("shaders/quad.vert", "shaders/raymarch.frag");
}

void FluidSimulation::initQuad() {
  float vertices[] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f,
                      -1.0f, 1.0f,  0.0f, 1.0f, 1.0f,  0.0f};
  glGenVertexArrays(1, &m_QuadVAO);
  glGenBuffers(1, &m_QuadVBO);
  glBindVertexArray(m_QuadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
}

void FluidSimulation::dispatchCompute(GLuint prog, int x, int y, int z) {
  glUseProgram(prog);
  glUniform3f(glGetUniformLocation(prog, "u_GridSize"), (float)m_Width,
              (float)m_Height, (float)m_Depth);

  // Each work group handles 8x8x8 voxels
  GLuint numGroupsX = (GLuint)ceil(x / 8.0f);
  GLuint numGroupsY = (GLuint)ceil(y / 8.0f);
  GLuint numGroupsZ = (GLuint)ceil(z / 8.0f);

  glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void FluidSimulation::update(float dt) {
  // 1. Advect Velocity
  glUseProgram(m_ProgAdvect);
  glUniform1f(glGetUniformLocation(m_ProgAdvect, "u_Dt"), dt);
  glUniform1f(glGetUniformLocation(m_ProgAdvect, "u_Dissipation"),
              m_DissipationVelocity);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]); // velocity field
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]); // quantity to advect
  glBindImageTexture(2, m_TexVelocity[1], 0, GL_TRUE, 0, GL_WRITE_ONLY,
                     GL_RGBA16F);

  dispatchCompute(m_ProgAdvect, m_Width, m_Height, m_Depth);
  swapVolumes(m_TexVelocity);

  // 2. Advect Density
  glUseProgram(m_ProgAdvect);
  glUniform1f(glGetUniformLocation(m_ProgAdvect, "u_Dt"), dt);
  glUniform1f(glGetUniformLocation(m_ProgAdvect, "u_Dissipation"),
              m_DissipationDensity);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]); // velocity field
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_3D, m_TexDensity[0]); // quantity to advect
  glBindImageTexture(2, m_TexDensity[1], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

  dispatchCompute(m_ProgAdvect, m_Width, m_Height, m_Depth);
  swapVolumes(m_TexDensity);

  // 2.5 Compute Vorticity & Apply Confinement Force
  glUseProgram(m_ProgVorticity);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]);
  glBindImageTexture(1, m_TexVorticity, 0, GL_TRUE, 0, GL_WRITE_ONLY,
                     GL_RGBA16F);
  dispatchCompute(m_ProgVorticity, m_Width, m_Height, m_Depth);

  glUseProgram(m_ProgVorticityForce);
  glUniform1f(glGetUniformLocation(m_ProgVorticityForce, "u_Dt"), dt);
  glUniform1f(glGetUniformLocation(m_ProgVorticityForce, "u_Epsilon"),
              m_VorticityEpsilon);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_3D, m_TexVorticity);
  glBindImageTexture(2, m_TexVelocity[1], 0, GL_TRUE, 0, GL_WRITE_ONLY,
                     GL_RGBA16F);
  dispatchCompute(m_ProgVorticityForce, m_Width, m_Height, m_Depth);
  swapVolumes(m_TexVelocity);

  // 3. Compute Divergence
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]);
  glBindImageTexture(1, m_TexDivergence, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
  dispatchCompute(m_ProgDivergence, m_Width, m_Height, m_Depth);

  // clear pressure before jacobi (optional, but helps stability sometimes)
  // For performance, we can skip clearing or clear using a compute shader.
  // For now we just use the previous pressure from last frame as initial guess.

  // 4. Jacobi Iteration (Pressure Solver)
  glUseProgram(m_ProgJacobi);
  int iterations =
      20; // Lowered from 40 to prevent TDR gpu crashes under heavy load
  for (int i = 0; i < iterations; i++) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, m_TexPressure[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, m_TexDivergence);
    glBindImageTexture(2, m_TexPressure[1], 0, GL_TRUE, 0, GL_WRITE_ONLY,
                       GL_R16F);

    dispatchCompute(m_ProgJacobi, m_Width, m_Height, m_Depth);
    swapVolumes(m_TexPressure);
  }

  // 5. Subtract Gradient (Projection Step)
  glUseProgram(m_ProgSubtract);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_3D, m_TexPressure[0]);
  glBindImageTexture(2, m_TexVelocity[1], 0, GL_TRUE, 0, GL_WRITE_ONLY,
                     GL_RGBA16F);

  dispatchCompute(m_ProgSubtract, m_Width, m_Height, m_Depth);
  swapVolumes(m_TexVelocity);
}

void FluidSimulation::splatDensity(const glm::vec3 &pos, float amount,
                                   float radius) {
  glUseProgram(m_ProgSplat);
  glUniform3fv(glGetUniformLocation(m_ProgSplat, "u_SplatPos"), 1, &pos[0]);
  glUniform4f(glGetUniformLocation(m_ProgSplat, "u_SplatData"), amount, 0.0f,
              0.0f, 0.0f);
  glUniform1f(glGetUniformLocation(m_ProgSplat, "u_SplatRadius"), radius);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexDensity[0]);
  glBindImageTexture(1, m_TexDensity[1], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);

  dispatchCompute(m_ProgSplat, m_Width, m_Height, m_Depth);
  swapVolumes(m_TexDensity);
}

void FluidSimulation::splatVelocity(const glm::vec3 &pos, const glm::vec3 &vel,
                                    float radius) {
  glUseProgram(m_ProgSplat);
  glUniform3fv(glGetUniformLocation(m_ProgSplat, "u_SplatPos"), 1, &pos[0]);
  glUniform4f(glGetUniformLocation(m_ProgSplat, "u_SplatData"), vel.x, vel.y,
              vel.z, 0.0f);
  glUniform1f(glGetUniformLocation(m_ProgSplat, "u_SplatRadius"), radius);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexVelocity[0]);
  glBindImageTexture(1, m_TexVelocity[1], 0, GL_TRUE, 0, GL_WRITE_ONLY,
                     GL_RGBA16F);

  dispatchCompute(m_ProgSplat, m_Width, m_Height, m_Depth);
  swapVolumes(m_TexVelocity);
}

void FluidSimulation::render(const glm::mat4 &view, const glm::mat4 &proj,
                             const glm::vec3 &cameraPos) {
  glUseProgram(m_ProgRaymarch);

  glm::mat4 invView = glm::inverse(view);
  glm::mat4 invProj = glm::inverse(proj);

  glUniformMatrix4fv(glGetUniformLocation(m_ProgRaymarch, "u_InvView"), 1,
                     GL_FALSE, &invView[0][0]);
  glUniformMatrix4fv(glGetUniformLocation(m_ProgRaymarch, "u_InvProj"), 1,
                     GL_FALSE, &invProj[0][0]);
  glUniform3fv(glGetUniformLocation(m_ProgRaymarch, "u_CameraPos"), 1,
               &cameraPos[0]);

  glUniform3fv(glGetUniformLocation(m_ProgRaymarch, "u_FluidColor"), 1,
               &m_FluidColor[0]);
  glUniform3fv(glGetUniformLocation(m_ProgRaymarch, "u_LightColor"), 1,
               &m_LightColor[0]);
  glUniform3fv(glGetUniformLocation(m_ProgRaymarch, "u_LightDir"), 1,
               &m_LightDir[0]);
  glUniform1f(glGetUniformLocation(m_ProgRaymarch, "u_Absorption"),
              m_Absorption);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_3D, m_TexDensity[0]);
  glUniform1i(glGetUniformLocation(m_ProgRaymarch, "u_Density"), 0);

  glBindVertexArray(m_QuadVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

std::string FluidSimulation::readShaderFile(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open shader file: " << path << std::endl;
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

GLuint FluidSimulation::compileComputeShader(const std::string &path) {
  std::string code = readShaderFile(path);
  const char *c_str = code.c_str();

  GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(shader, 1, &c_str, nullptr);
  glCompileShader(shader);

  // Check compilation
  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(shader, 512, nullptr, infoLog);
    std::cerr << "Compute Shader Compilation Error (" << path << "):\n"
              << infoLog << std::endl;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, shader);
  glLinkProgram(program);
  glDeleteShader(shader);

  return program;
}

GLuint FluidSimulation::compileRenderShader(const std::string &vertPath,
                                            const std::string &fragPath) {
  std::string vCode = readShaderFile(vertPath);
  std::string fCode = readShaderFile(fragPath);
  const char *v_str = vCode.c_str();
  const char *f_str = fCode.c_str();

  GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vShader, 1, &v_str, nullptr);
  glCompileShader(vShader);

  GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fShader, 1, &f_str, nullptr);
  glCompileShader(fShader);

  GLuint program = glCreateProgram();
  glAttachShader(program, vShader);
  glAttachShader(program, fShader);
  glLinkProgram(program);

  glDeleteShader(vShader);
  glDeleteShader(fShader);

  return program;
}
