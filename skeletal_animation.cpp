#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader_m.h>
#include <learnopengl/animator.h>
#include <learnopengl/model_animation.h>

#include <iostream>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ---------- Callbacks ----------
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

// ---------- Settings ----------
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
const float PLAYER_JUMP_SPEED = 5.0f;
const float PLAYER_GRAVITY = -9.8f * 2.0f; // stronger gravity for snappier jump

// ---------- Player / Camera ----------
struct Player {
    glm::vec3 pos{ 0.0f, 0.0f, 0.0f };
    float yawDeg = 0.0f;          // หมุนตัวละครรอบแกน Y
    float moveSpeed = 3.4f;       // m/s (walking)
    float runSpeed = 6.0f;        // m/s (running)
    float rollSpeed = 2.0f;       // m/s
    float height = 1.1f;          // ความสูงศีรษะโดยประมาณ

    // Jump state
    bool isGrounded = true;
    float yVelocity = 0.0f;
} player;

// กล้องแบบ third-person orbit (เมาส์หัน)
struct OrbitCam {
    float yawDeg = 0.0f;
    float pitchDeg = -5.0f;   // เงยขึ้นเล็กน้อย
    float distance = 3.0f;    // เข้าใกล้ตัวละคร
    float height = 0.35f;   // ลดตำแหน่งกล้องลง
    float lookOffset = 0.6f;  // มองเข้าใกล้ระดับอก
    float sens = 0.1f;
    float minPitch = -60.0f;
    float maxPitch = 35.0f;
    float minDist = 1.6f;    // อนุญาตให้ซูมใกล้มากขึ้น
    float maxDist = 6.0f;
} cam;

// ---------- Timing ----------
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ---------- Mouse state ----------
bool firstMouse = true;
double lastX = SCR_WIDTH / 2.0;
double lastY = SCR_HEIGHT / 2.0;

// ---------- Input edges ----------
bool prevLMB = false;
bool prevSpace = false;
bool prevE = false; // jump key edge
bool prevShift = false; // run key edge

// ---------- Animation State ----------
enum class ActionState { Idle, Moving, Running, Rolling, Attacking, Jumping };
ActionState state = ActionState::Idle;
float actionTimeLeft = 0.0f;

// ---------- GL / Content ----------
Shader* gShader = nullptr;
Model* gModel = nullptr;

Animation* gIdle = nullptr, * gWalk = nullptr, * gRun = nullptr, * gRoll = nullptr, * gAttack = nullptr, * gJump = nullptr;
Animator* gAnimator = nullptr;

// Ground geometry
GLuint groundVAO = 0, groundVBO = 0, groundEBO = 0;

// ----- helpers -----
static inline float radiansf(float d) { return d * 0.017453292519943295f; }

void PlayLoop(Animation* anim) {
    gAnimator->PlayAnimation(anim);
}
void PlayOneShot(Animation* anim, float& outSec) {
    gAnimator->PlayAnimation(anim);
    float durTicks = anim->GetDuration();
    float tps = anim->GetTicksPerSecond();
    outSec = (tps > 0.0f) ? durTicks / tps : 0.7f; // fallback
}

// เวกเตอร์ forward/right “ตามกล้อง” (ใช้กับ WASD)
glm::vec3 CameraForward() {
    float yaw = radiansf(cam.yawDeg);
    float pit = radiansf(cam.pitchDeg);
    glm::vec3 f;
    f.x = std::cos(pit) * std::sin(yaw);
    f.y = std::sin(pit);
    f.z = std::cos(pit) * std::cos(yaw);
    // ใช้เฉพาะบนระนาบ XZ สำหรับทิศเดิน
    f.y = 0.0f;
    if (glm::length(f) < 1e-6f) f = glm::vec3(0, 0, 1);
    return glm::normalize(f);
}
glm::vec3 CameraRight() {
    glm::vec3 f = CameraForward();
    return glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
}

// กล้อง: คำนวณตำแหน่งและ view
void ComputeCamera(glm::vec3& outPos, glm::mat4& outView) {
    // ทิศทางกล้องเต็ม (รวม pitch)
    float yaw = radiansf(cam.yawDeg);
    float pit = radiansf(cam.pitchDeg);
    glm::vec3 dir;
    dir.x = std::cos(pit) * std::sin(yaw);
    dir.y = std::sin(pit);
    dir.z = std::cos(pit) * std::cos(yaw);

    glm::vec3 target = player.pos + glm::vec3(0, player.height + cam.lookOffset, 0);
    outPos = target - dir * cam.distance + glm::vec3(0, cam.height, 0);
    outView = glm::lookAt(outPos, target, glm::vec3(0, 1, 0));
}

// สร้างพื้นเป็นสี่เหลี่ยมใหญ่ (ตำแหน่ง, นอร์มัล, เท็กซ์โค)
void CreateGround() {
    const float S = 100.0f; // ครึ่งหนึ่ง (รวมคือ 200x200)
    // pos(x,y,z) normal(x,y,z) tex(u,v)
    float verts[] = {
        -S, 0.0f, -S,   0,1,0,   0.0f, 0.0f,
         S, 0.0f, -S,   0,1,0,  50.0f, 0.0f,
         S, 0.0f,  S,   0,1,0,  50.0f,50.0f,
        -S, 0.0f,  S,   0,1,0,   0.0f,50.0f
    };
    unsigned int idx[] = { 0,1,2,  0,2,3 };

    glGenVertexArrays(1, &groundVAO);
    glGenBuffers(1, &groundVBO);
    glGenBuffers(1, &groundEBO);

    glBindVertexArray(groundVAO);
    glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, groundEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    // สมมติ anim_model.vs ใช้ layout:
    // location 0: position, 1: normal, 2: texcoord
    GLsizei stride = (3 + 3 + 2) * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
}

int main() {
    // ---- GLFW/GL setup ----
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Souls-like TPS (Mouse Camera)", NULL, NULL);
    if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    // จับเมาส์ (เหมือนเกม)
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    stbi_set_flip_vertically_on_load(true);
    glEnable(GL_DEPTH_TEST);

    // ---- Shaders ----
    Shader ourShader("anim_model.vs", "anim_model.fs");
    gShader = &ourShader;

    // ---- Load Model & Animations ----
    Model  ourModel(FileSystem::getPath("resources/objects/models/idle.dae"));
    gModel = &ourModel;

    Animation idleAnim(FileSystem::getPath("resources/objects/models/idle.dae"), &ourModel);
    Animation walkAnim(FileSystem::getPath("resources/objects/models/walk.dae"), &ourModel);
    Animation walkBackwardAnim(FileSystem::getPath("resources/objects/models/walk_backward.dae"), &ourModel);
    Animation runAnim(FileSystem::getPath("resources/objects/models/run.dae"), &ourModel);
    Animation strafeLeftAnim(FileSystem::getPath("resources/objects/models/strafe_left.dae"), &ourModel);
    Animation strafeRightAnim(FileSystem::getPath("resources/objects/models/strafe_right.dae"), &ourModel);
    Animation rollAnim(FileSystem::getPath("resources/objects/models/roll.dae"), &ourModel);
    Animation attackAnim(FileSystem::getPath("resources/objects/models/attack.dae"), &ourModel);
    Animation jumpAnim(FileSystem::getPath("resources/objects/models/jump.dae"), &ourModel);

    gIdle = &idleAnim;
    gWalk = &walkAnim;
    gRun = &runAnim;
    gRoll = &rollAnim;
    gAttack = &attackAnim;
    gJump = &jumpAnim;

    Animator animator(gIdle);
    gAnimator = &animator;

    // ---- Ground ----
    CreateGround();

    // -------- Main loop --------
    while (!glfwWindowShouldClose(window)) {
        // timing
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // ===== INPUT =====
        // WASD เดินตาม “ทิศกล้อง”
        glm::vec2 moveInput(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveInput.y += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveInput.y -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveInput.x += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveInput.x -= 1.0f;

        // edge buttons
        bool spaceNow = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
        bool lmbNow = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        bool eNow = (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS); // jump key
        bool shiftNow = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS); // run/sprint key

        // ===== STATE MACHINE =====
        if (state == ActionState::Rolling || state == ActionState::Attacking) {
            actionTimeLeft -= deltaTime;
            if (actionTimeLeft <= 0.0f) {
                if (glm::length(moveInput) > 0.0f) {
                    if (shiftNow) { state = ActionState::Running; PlayLoop(gRun); }
                    else { state = ActionState::Moving; PlayLoop(gWalk); }
                }
                else {                          
                    state = ActionState::Idle;   PlayLoop(gIdle);
                }
            }
        }
        else if (state == ActionState::Jumping)
        {
            // landing handled by gravity block below; keep playing jump until landed
            if (player.isGrounded)
            {
                // landed this frame
                if (glm::length(moveInput) > 0.0f) { 
                    if (shiftNow) { state = ActionState::Running; PlayLoop(gRun); }
                    else { state = ActionState::Moving; PlayLoop(gWalk); }
                }
                else { state = ActionState::Idle; PlayLoop(gIdle); }
            }
        }
        else {
            if (eNow && !prevE && player.isGrounded) {
                // start jump
                state = ActionState::Jumping;
                PlayOneShot(gJump, actionTimeLeft);
                player.yVelocity = PLAYER_JUMP_SPEED;
                player.isGrounded = false;
            }
            else if (spaceNow && !prevSpace) {
                state = ActionState::Rolling;  PlayOneShot(gRoll, actionTimeLeft);
            }
            else if (lmbNow && !prevLMB) {
                state = ActionState::Attacking; PlayOneShot(gAttack, actionTimeLeft);
            }
            else {
                // If moving and holding shift -> running
                if (glm::length(moveInput) > 0.0f) {
                    if (shiftNow) {
                        if (state != ActionState::Running) { state = ActionState::Running; PlayLoop(gRun); }
                    }
                    else {
                        if (state != ActionState::Moving) { state = ActionState::Moving; PlayLoop(gWalk); }
                    }
                }
                else {
                    if (state != ActionState::Idle) { state = ActionState::Idle;   PlayLoop(gIdle); }
                }
            }
        }

        // ===== GRAVITY / JUMP =====
        if (!player.isGrounded)
        {
            player.yVelocity += PLAYER_GRAVITY * deltaTime;
            player.pos.y += player.yVelocity * deltaTime;

            if (player.pos.y <= 0.0f)
            {
                player.pos.y = 0.0f;
                player.yVelocity = 0.0f;
                player.isGrounded = true;
                // state change will be handled at top of loop on next frame (or you can force here)
                if (state == ActionState::Jumping) {
                    if (glm::length(moveInput) > 0.0f) { 
                        if (shiftNow) { state = ActionState::Running; PlayLoop(gRun); }
                        else { state = ActionState::Moving; PlayLoop(gWalk); }
                    }
                    else { state = ActionState::Idle; PlayLoop(gIdle); }
                }
            }
        }

        // ===== MOVEMENT =====
        glm::vec3 camF = CameraForward();
        glm::vec3 camR = CameraRight();
        glm::vec3 wishDir = glm::normalize(camF * moveInput.y + camR * moveInput.x);
        if (glm::any(glm::isnan(wishDir))) wishDir = glm::vec3(0);

        if (state == ActionState::Moving || state == ActionState::Idle || state == ActionState::Jumping || state == ActionState::Running) {
            float spd = 0.0f;
            if (state == ActionState::Moving) spd = player.moveSpeed;
            else if (state == ActionState::Running) spd = player.runSpeed;
            else spd = 0.0f;
            // allow limited air-control while jumping
            if (state == ActionState::Jumping) spd *= 0.6f;
            player.pos += wishDir * spd * deltaTime;

            if (glm::length(wishDir) > 0.0f) {
                // หันตัวละครไปทิศการเคลื่อนที่ (souls-like)
                player.yawDeg = glm::degrees(std::atan2(wishDir.x, wishDir.z));
            }
        }
        else if (state == ActionState::Rolling) {
            // กลิ้งพุ่งไปข้างหน้า “ตามทิศตัวละคร” (ไม่ใช่ทิศกล้อง)
            glm::vec3 forwardChar = glm::normalize(glm::vec3(std::sin(radiansf(player.yawDeg)), 0, std::cos(radiansf(player.yawDeg))));
            player.pos += forwardChar * player.rollSpeed * deltaTime;
        }

        prevSpace = spaceNow;
        prevLMB = lmbNow;
        prevE = eNow;
        prevShift = shiftNow;

        // ===== ANIMATION STEP =====
        gAnimator->UpdateAnimation(deltaTime);

        // ===== RENDER =====
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gShader->use();

        // camera/projection
        glm::mat4 projection = glm::perspective(glm::radians(50.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 300.0f);
        glm::vec3 camPos; glm::mat4 view;
        ComputeCamera(camPos, view);

        gShader->setMat4("projection", projection);
        gShader->setMat4("view", view);

        // bone matrices
        auto transforms = gAnimator->GetFinalBoneMatrices();
        for (int i = 0; i < (int)transforms.size(); ++i) {
            gShader->setMat4("finalBonesMatrices[" + std::to_string(i) + "]", transforms[i]);
        }

        // ----- draw ground (ใช้ shader เดิม: ให้เป็นวัตถุเรียบ) -----
        glm::mat4 model = glm::mat4(1.0f);
        gShader->setMat4("model", model);
        glBindVertexArray(groundVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        // ----- draw character -----
        model = glm::mat4(1.0f);
        model = glm::translate(model, player.pos + glm::vec3(0.0f, -0.15f, 0.0f));
        model = glm::rotate(model, radiansf(player.yawDeg), glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(1.0f));
        gShader->setMat4("model", model);
        gModel->Draw(*gShader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

// ---------- Callbacks ----------
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// เมาส์ควบคุมกล้อง (yaw/pitch)
void mouse_callback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;

    cam.yawDeg += (float)xoffset * cam.sens * -1;
    cam.pitchDeg += (float)yoffset * cam.sens;
    if (cam.pitchDeg < cam.minPitch) cam.pitchDeg = cam.minPitch;
    if (cam.pitchDeg > cam.maxPitch) cam.pitchDeg = cam.maxPitch;
}

// สกอลล์เมาส์ซูม
void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    cam.distance -= (float)yoffset * 0.5f;
    if (cam.distance < cam.minDist) cam.distance = cam.minDist;
    if (cam.distance > cam.maxDist) cam.distance = cam.maxDist;
}
