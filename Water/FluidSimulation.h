#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class FluidSimulation {
public:
  FluidSimulation(int width, int height, int depth);
  ~FluidSimulation();

  // Step the simulation by dt
  void update(float dt);

  // Inject density or velocity at a normalized 3D position (0-1)
  // For density, data.x is the amount. For velocity, data.xyz is the vector.
  void splatDensity(const glm::vec3 &pos, float amount, float radius = 0.05f);
  void splatVelocity(const glm::vec3 &pos, const glm::vec3 &vel,
                     float radius = 0.05f);

  // Render the fluid volume using raymarching
  void render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &cameraPos);

public:
  // Tweakable Physics Parameters
  float m_DissipationVelocity = 1.0f;
  float m_DissipationDensity = 0.99f;
  float m_VorticityEpsilon = 2.0f;
  float m_Absorption = 25.0f; // Controls how thick the fluid looks

  // Splat Mechanics
  float m_SplatForce = 15.0f;
  float m_SplatRadius = 0.03f;

  // Colors
  glm::vec3 m_FluidColor = glm::vec3(0.2f, 0.5f, 0.9f);
  glm::vec3 m_LightColor = glm::vec3(1.0f, 0.9f, 0.8f);
  glm::vec3 m_LightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.5f));

private:
  int m_Width, m_Height, m_Depth;

  // 3D Texture IDs
  GLuint m_TexVelocity[2];
  GLuint m_TexDensity[2];
  GLuint m_TexPressure[2];
  GLuint m_TexDivergence;
  GLuint m_TexVorticity;

  // Track which texture is read/write
  int m_ReadIdx = 0;
  int m_WriteIdx = 1;

  // Shader Program IDs
  GLuint m_ProgAdvect;
  GLuint m_ProgDivergence;
  GLuint m_ProgJacobi;
  GLuint m_ProgSubtract;
  GLuint m_ProgVorticity;
  GLuint m_ProgVorticityForce;
  GLuint m_ProgSplat;
  GLuint m_ProgRaymarch;

  // Quad for rendering
  GLuint m_QuadVAO, m_QuadVBO;

  // Helper functions
  void initTextures();
  void initShaders();
  void initQuad();

  void swapVolumes(GLuint textures[2]) {
    GLuint temp = textures[0];
    textures[0] = textures[1];
    textures[1] = temp;
  }

  // Single pass wrapper
  void dispatchCompute(GLuint prog, int x, int y, int z);

  GLuint compileComputeShader(const std::string &path);
  GLuint compileRenderShader(const std::string &vertPath,
                             const std::string &fragPath);
  std::string readShaderFile(const std::string &path);
};
