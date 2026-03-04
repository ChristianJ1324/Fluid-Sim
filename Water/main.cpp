#include <GLFW/glfw3.h>
#include <cmath>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

#include "FluidSimulation.h"
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/imgui.h>

struct Camera {
  glm::vec3 Position = glm::vec3(0.0f, 1.5f, 4.0f);
  glm::vec3 Front = glm::vec3(0.0f, 0.0f, -1.0f);
  glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f);

  float Yaw = -90.0f;
  float Pitch = -15.0f;

  bool FirstMouse = true;
  float LastX = 400.0f;
  float LastY = 300.0f;

  float Speed = 5.0f;
  float Sensitivity = 0.1f;
};
Camera cam;

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn) {
  if (ImGui::GetIO().WantCaptureMouse)
    return; // Don't move camera if interacting with UI

  float xpos = static_cast<float>(xposIn);
  float ypos = static_cast<float>(yposIn);

  if (cam.FirstMouse) {
    cam.LastX = xpos;
    cam.LastY = ypos;
    cam.FirstMouse = false;
  }

  float xoffset = xpos - cam.LastX;
  float yoffset =
      cam.LastY - ypos; // reversed since y-coordinates go from bottom to top
  cam.LastX = xpos;
  cam.LastY = ypos;

  // Only update rotation if right mouse button is held
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
    cam.FirstMouse = true; // Reset so dragging doesn't jump
    return;
  }

  xoffset *= cam.Sensitivity;
  yoffset *= cam.Sensitivity;

  cam.Yaw += xoffset;
  cam.Pitch += yoffset;

  if (cam.Pitch > 89.0f)
    cam.Pitch = 89.0f;
  if (cam.Pitch < -89.0f)
    cam.Pitch = -89.0f;

  glm::vec3 front;
  front.x = cos(glm::radians(cam.Yaw)) * cos(glm::radians(cam.Pitch));
  front.y = sin(glm::radians(cam.Pitch));
  front.z = sin(glm::radians(cam.Yaw)) * cos(glm::radians(cam.Pitch));
  cam.Front = glm::normalize(front);
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  glViewport(0, 0, width, height);
}

int main() {
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    system("pause");
    return -1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow *window =
      glfwCreateWindow(800, 600, "GPU Fluid Simulation", NULL, NULL);
  if (!window) {
    std::cerr << "Failed to create GLFW window\n";
    glfwTerminate();
    system("pause");
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
  glfwSetCursorPosCallback(window, mouse_callback);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD\n";
    system("pause");
    return -1;
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 430");

  // Grid size for the fluid simulation (Lowered to prevent TDR Crashes)
  FluidSimulation sim(192, 192, 192);

  float simTime = 0.0f;
  float splatInterval = 0.1f;
  float lastSplatTime = 0.0f;

  while (!glfwWindowShouldClose(window)) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, true);

    // Camera WASD Movement
    float currentFrame = glfwGetTime();
    float deltaTime =
        currentFrame -
        lastSplatTime; // Reusing var for simplicity, will refactor
    float velocity = cam.Speed * 0.016f; // using fixed step
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
      cam.Position += cam.Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
      cam.Position -= cam.Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
      cam.Position -= glm::normalize(glm::cross(cam.Front, cam.Up)) * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
      cam.Position += glm::normalize(glm::cross(cam.Front, cam.Up)) * velocity;

    // Continuous fluid injection at the center-bottom
    simTime += 0.016f; // fixed timestep for stability
    if (simTime - lastSplatTime > splatInterval) {
      // Inject continuous pillar of fluid
      sim.splatDensity(glm::vec3(0.5f, 0.1f, 0.5f), 2.0f, 0.03f);
      sim.splatVelocity(glm::vec3(0.5f, 0.1f, 0.5f),
                        glm::vec3(0.0f, 20.0f, 0.0f), 0.03f);
      lastSplatTime = simTime;
    }

    glm::mat4 view =
        glm::lookAt(cam.Position, cam.Position + cam.Front, cam.Up);
    glm::mat4 proj =
        glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

    // Add some interaction when clicking (ensure UI isn't intercepting)
    if (!ImGui::GetIO().WantCaptureMouse &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {

      // 1. Get Mouse Position Normalized Device Coordinates (-1 to 1)
      double mouseX, mouseY;
      glfwGetCursorPos(window, &mouseX, &mouseY);

      float ndcX = (2.0f * mouseX) / 800.0f - 1.0f;
      float ndcY = 1.0f - (2.0f * mouseY) / 600.0f; // Flip Y

      // 2. Unproject to World Space Ray
      glm::vec4 clipCoords(ndcX, ndcY, -1.0f, 1.0f);
      glm::vec4 eyeCoords = glm::inverse(proj) * clipCoords;
      eyeCoords =
          glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f); // Set to forward

      glm::vec3 rayWorld =
          glm::normalize(glm::vec3(glm::inverse(view) * eyeCoords));

      // 3. Intersect Ray with a horizontal plane at y=0 (Ground of the box)
      // Plane Equation: dot(N, P - P0) = 0. For Y=0 plane, N=(0,1,0),
      // P0=(0,0,0) Ray Equation: P = Origin + t * Dir t = -dot(N, Origin) /
      // dot(N, Dir)
      float t = -cam.Position.y / rayWorld.y;

      if (t > 0.0f) { // Intersection is in front of camera
        glm::vec3 hitPoint = cam.Position + rayWorld * t;

        // 4. Map the hit point (-1 to 1 Box Domain) to normalized Splat
        // Coordinates (0 to 1) Since our box is rendered loosely at -1 to 1 in
        // the Raymarcher:
        glm::vec3 hitNormalized = hitPoint * 0.5f + 0.5f;

        // Clamp so we don't splat outside the texture
        hitNormalized =
            glm::clamp(hitNormalized, glm::vec3(0.01f), glm::vec3(0.99f));

        // Add some vertical offset so the fluid sprays "up" from the floor
        hitNormalized.y = 0.1f;

        sim.splatDensity(hitNormalized, sim.m_SplatForce,
                         sim.m_SplatRadius); // Use tuned force

        // Velocity pushes upwards and slightly towards where the ray originated
        glm::vec3 pushVel =
            glm::vec3(0.0f, sim.m_SplatForce * 5.0f, 0.0f) + (rayWorld * 2.0f);
        sim.splatVelocity(hitNormalized, pushVel, sim.m_SplatRadius);
      }
    }

    // Step simulation physics (fixed timestep is greatly preferred for Jacobis
    // and advection stability)
    sim.update(0.016f);

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // UI Window
    ImGui::Begin("Fluid Physics Settings");
    ImGui::Text("Simulation Scale: %dx%dx%d", 192, 192, 192);
    ImGui::Separator();
    ImGui::SliderFloat("Vorticity Content", &sim.m_VorticityEpsilon, 0.0f,
                       10.0f, "%.2f");
    ImGui::SliderFloat("Velocity Decay", &sim.m_DissipationVelocity, 0.9f, 1.0f,
                       "%.4f");
    ImGui::SliderFloat("Density Decay", &sim.m_DissipationDensity, 0.9f, 1.0f,
                       "%.4f");
    ImGui::SliderFloat("Render Absorption", &sim.m_Absorption, 1.0f, 100.0f,
                       "%.1f");
    ImGui::Separator();
    ImGui::SliderFloat("Mouse Radius", &sim.m_SplatRadius, 0.005f, 0.15f,
                       "%.3f");
    ImGui::SliderFloat("Mouse Force", &sim.m_SplatForce, 1.0f, 100.0f, "%.1f");
    ImGui::Separator();
    ImGui::ColorEdit3("Fluid Color", &sim.m_FluidColor[0]);
    ImGui::ColorEdit3("Light Color", &sim.m_LightColor[0]);
    ImGui::Text("Hold Right Mouse to rotate camera.");
    ImGui::Text("Use W,A,S,D to fly.");
    ImGui::End();

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    sim.render(view, proj, cam.Position);

    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwTerminate();
  return 0;
}
